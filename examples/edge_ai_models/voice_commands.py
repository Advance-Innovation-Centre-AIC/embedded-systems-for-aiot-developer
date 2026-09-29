# voice_commands.py - Steer the Cart. Needs "Voice Commands" (experimental) from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3. Eva Kit and BENTO Emulator: not tested yet.
# Say a word. Safe stop: tap it or SW6 (upper). SW5 (lower) = pause/resume.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Voice Commands"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 2, 4  # word = K of the last N results at Confirm at
REFRACT_MS = 1000  # no second word sooner than this
HOLD_MS = 2000  # a word stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Say a word - SW6 = Safe stop, SW5 = pause" if buttons else "Say a word"
DIRS = {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0)}

COL_TEXT, COL_DIM, COL_CARD, COL_BTN = 0xE8EAED, 0x9AA3AF, 0x171B22, 0x5E6878
COL_OK, COL_WARN, COL_BAD, COL_INFO, COL_ALARM_BG = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF, 0x3A1416
NORMAL, HEARD, UNACK, ACKED, ENDED, SEEN = range(6)


# ---- 2) Board buttons and the AI core ----
class Stop(Exception):
    pass  # ends the program with a message on screen


class Button:
    # one event per press (holding does not repeat)
    def __init__(self, index):
        self.index, self.down, self.clicked = index, False, False

    def sample(self):
        if buttons is None:
            return
        now = buttons.pressed(self.index)
        if now and not self.down:
            self.clicked = True
        self.down = now

    def pressed_now(self):
        fired, self.clicked = self.clicked, False
        return fired


def find_model():
    # never reads the model id
    try:
        ms = edge_ai.models()
    except Exception as e:
        print("cannot read models:", e)
        return
    for m in ms:
        b = m.get("builtin")  # None on the emulator
        if m["name"] == MODEL and (b is not False if BUILTIN else b is False):
            return m["index"], m["labels"], len(ms)


def missing():
    s = "Deploy " + MODEL + "\nfrom edgeai-store.tesaiot.dev\n(sign in with GitHub) on a Dev Kit."
    print(s)
    ui.screen()
    time.sleep_ms(150)
    ui.Panel(x=96, y=110, w=600, h=180, color=COL_CARD, min=COL_BAD, max=12, value=1)
    label("Model not on this board", 120, 126, COL_BAD, 24)
    label(" ", 120, 170, COL_TEXT, 20).text(s)  # .text() fits 126 bytes
    ui.poll()


def start(w, i, n0=0):
    # on resume: Stop if a model was added or removed meanwhile
    put(w["status"], "Loading " + MODEL + " ...", COL_WARN)
    ui.poll()
    _rd[1] = None
    try:
        if n0 and edge_ai.count() != n0:
            raise Stop("Model list changed - run again")
        edge_ai.select(i)  # loads and starts it: up to 15 s
    except OSError as e:
        print("select:", e)
        raise Stop("Cannot start " + MODEL)
    put(w["status"], HELP, COL_TEXT)


def stop_ai():
    try:
        edge_ai.stop()
    except OSError:
        pass


def read_th(i):
    # per class: Hearing at, Confirm at, clear below
    try:
        t = edge_ai.thresholds(i)
        return t["enter"], t["confirm"], t["exit"]
    except Exception:
        return [[v] * 8 for v in DEF_TH]


_rd = [None, None]  # last seq, when result() began to fail


def read_result(i):
    # only NEW results of model i; Stop after 3 s of errors
    try:
        r = edge_ai.result()
        _rd[1] = None
    except OSError:
        t = time.ticks_ms()
        _rd[1] = _rd[1] or t
        if time.ticks_diff(t, _rd[1]) >= 3000:
            raise Stop("AI core not answering - run again")
        return
    if r and r["label"] is not None and r["seq"] != _rd[0] and r.get("index", i) == i:
        _rd[0] = r["seq"]
        return r


class Vote:
    # the last N scores; K of them at or above a level = confirmed
    def __init__(self, k, n):
        self.k, self.n, self.h = k, n, []

    def add(self, p):
        self.h = (self.h + [p])[-self.n:]

    def hits(self, lv):
        return sum(1 for p in self.h if p >= lv) >= self.k

    def clear(self, lv):
        return len(self.h) == self.n and max(self.h) < lv


class Episode:
    # alarm (latch): NORMAL > HEARD > UNACK > ACKED or ENDED, needs ACK
    # info: NORMAL > HEARD > SEEN for HOLD_MS, no ACK
    def __init__(self, latch):
        self.latch, self.st, self.n, self.t, self.end = latch, NORMAL, 0, None, ""

    def step(self, heard, confirm, clear, now):
        # True once per new event; refractory from the last event (info) or the episode's last confirm
        s, t = self.st, self.t
        if confirm:
            if s not in (UNACK, ACKED) and (t is None or time.ticks_diff(now, t) >= REFRACT_MS):
                self.st, self.n, self.t = UNACK if self.latch else SEEN, self.n + 1, now
                return True
            if s in (UNACK, ACKED):
                self.t = now
        if s == SEEN:
            if time.ticks_diff(now, self.t) < HOLD_MS:
                return False
            s = NORMAL
        if clear:
            if s == UNACK:
                s, self.end = ENDED, clock()
            elif s != ENDED:
                s = NORMAL
        elif heard and s == NORMAL:
            s = HEARD
        self.st = s
        return False


def event_of(r, th):
    # the strongest EVENT class (class 0 = nothing) + its thresholds
    sc = r["scores"]
    e = 0
    for c in range(1, len(sc)):
        if sc[c] > sc[e] or not e:
            e = c
    return e, int(sc[e] * 100 + 0.5), th[0][e], th[1][e], th[2][e]


# ---- 3) Screen helpers ----
def label(text, x, y, col, size=16):
    return ui.Label(text, x=x, y=y, color=col, value=size)


def card(x, y, w, h, title):
    p = ui.Panel(x=x, y=y, w=w, h=h, color=COL_CARD, min=COL_DIM, max=12, value=1)
    label(title, x + 14, y + 8, COL_INFO)
    return p


def clock():
    t = time.localtime()
    return "%02d:%02d:%02d" % t[3:6]


_sent = {}


def put(o, txt=None, col=None):
    # send only what changed: a full display queue drops commands
    k = o.id()
    if txt is not None and _sent.get(k) != txt:
        _sent[k] = txt
        o.text(txt)
    if col is not None and _sent.get(~k) != col:
        _sent[~k] = col
        o.color(col)


# ---- 4) This model's screen (T6 keywords) ----
class Cart:
    # the dot on the 5 x 5 yard; the fence stops it
    def __init__(self, dot):
        self.dot, self.x, self.y, self.d, self.go = dot, 2, 2, "up", False

    def step(self):
        x, y = self.x + DIRS[self.d][0], self.y + DIRS[self.d][1]
        if 0 <= x < 5 and 0 <= y < 5:
            self.x, self.y = x, y
            self.dot.pos(32 + 45 * x, 100 + 45 * y)
        else:
            self.go = False

    def hear(self, wd):
        # a direction turns (and steps when halted); go and stop switch driving
        if wd in DIRS:
            self.d = wd
            if not self.go:
                self.step()
        else:
            self.go = wd == "go"


def build():
    ui.screen()
    time.sleep_ms(150)
    label("Steer the Cart", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0)}
    label("Experimental. English only: left right up down stop go. Cannot: other words.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Feed yard")
    ui.Panel(x=28, y=96, w=225, h=225, color=COL_CARD, min=COL_DIM, value=1)
    for k in range(1, 5):  # grid lines
        ui.Panel(x=27 + 45 * k, y=96, w=2, h=225, color=COL_BTN)
        ui.Panel(x=28, y=95 + 45 * k, w=225, h=2, color=COL_BTN)
    w["dot"] = ui.Panel(x=122, y=190, w=37, h=37, color=COL_INFO, max=18)  # cell (2, 2)
    w["word"] = label("-", 266, 100, COL_DIM, 28)
    w["mode"] = label(" ", 266, 140, COL_TEXT)
    w["log"] = label(" ", 266, 170, COL_TEXT)
    w["safe"] = ui.Button("Safe stop: ON", x=266, y=262, w=200, h=64, color=COL_BTN, value=20)
    card(494, 64, 294, 272, "Top 3 scores")
    w["rl"] = [label(" ", 508, 100 + 40 * j, COL_DIM, 20) for j in range(3)]
    w["legend"] = label(" ", 508, 230, COL_DIM)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = read_th(i)
    sp = labels.index("stop") if "stop" in labels else 0
    put(w["legend"], "Hearing at %d%% | Confirm at %d%%\nSafe stop: stop at %d%%, 1 of %d" % (
        th[0][1], th[1][1], th[0][sp], N))
    b5, b6, ep, cart = Button(0), Button(1), Episode(False), Cart(w["dot"])
    log, vs, wd, top, safe, paused, blink = [], 0, "", None, True, False, False
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        tog = b6.pressed_now()
        for e in ui.poll() or ():
            tog = tog or e["type"] == "clicked" and e["handle"] == w["safe"].id()
        if tog:
            safe = not safe
            put(w["safe"], "Safe stop: ON" if safe else "Safe stop: OFF")
        if b5.pressed_now():
            paused, cart.go, vs = not paused, False, 0
            if paused:
                stop_ai()
                put(w["status"], "Paused - SW5 resumes", COL_WARN)
            else:
                start(w, i, n0)
        if not vs:  # fresh votes: the last N scores of each class
            vs, held, ep.st = [Vote(K, N) for c in labels], 0, NORMAL
        now = time.ticks_ms()
        r = None
        if not paused and time.ticks_diff(now, polled) >= POLL_MS:
            polled, r = now, read_result(i)
        if r:  # a word = K of its last N at Confirm at, once per utterance
            top = [int(s * 100 + 0.5) for s in r["scores"]]
            for v, p in zip(vs, top):
                v.add(p)
            e, p, en, cf, ex = event_of(r, th)
            if held and not vs[held].hits(th[2][held]):
                held = 0  # that word ended: it may count again
            ok = e != held and vs[e].hits(cf)
            if safe and cart.go and sp and max(vs[sp].h) >= th[0][sp]:
                e, ok, ep.t = sp, True, None  # Safe stop: no refractory wait
            if ep.step(p >= en, ok, all(v.clear(ex) for v in vs[1:]), now):
                held, wd = e, labels[e]
                cart.hear(wd)
                if cart.go:
                    vs[sp].h = []  # Safe stop hears only stop scores newer than this go (red team 19:25)
                log = ["%s %s %d%% #%d" % (clock(), wd, max(vs[e].h), ep.n)] + log[:2]
                print(log[0])
                put(w["log"], "\n".join(log))
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            if cart.go and blink:
                cart.step()  # every other draw = 1 cell/s
            w["hb"].value(1 if blink and top else 0)  # heartbeat
            put(w["word"], wd.upper() if ep.st == SEEN else "-", COL_TEXT)  # heard: top row, grey
            put(w["mode"], ("Going %s, 1 cell/s" if cart.go else "Stopped, facing %s") % cart.d)
            for j, c in enumerate(sorted(range(len(top)), key=lambda c: -top[c])[:3] if top else ()):
                put(w["rl"][j], "%s %d%%" % (labels[c], top[c]), COL_TEXT if c and top[c] >= th[1][c] else COL_DIM)
            top = None
        time.sleep_ms(20)


def main():
    m = find_model()
    if not m:
        return missing()
    w = build()
    gc.collect()
    print("free heap:", gc.mem_free())
    try:
        run(w, *m)
    except Stop as e:
        put(w["status"], str(e), COL_BAD)
        print(e)
    finally:  # always release the AI core
        stop_ai()


main()
