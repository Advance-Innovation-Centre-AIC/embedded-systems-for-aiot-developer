# environment_sounds.py - Confusion Lab. Needs "Environment Sounds" (experimental) from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.
# Tap a tile, then play that sound for 10 s. SW6 or ACK = chainsaw alarm. SW5 = pause.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Environment Sounds"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Tap a tile, then play that sound - SW6 = ACK, SW5 = pause" if buttons else "Tap a tile, then play that sound"
TRUTH_MS = 10000  # a tile tap = "I play this sound for 10 s"

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

    def ack(self):
        self.st = {UNACK: ACKED, ENDED: NORMAL}.get(self.st, self.st)


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


# ---- 4) This model's screen (T5 tiles) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Confusion Lab", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "wp": 0}
    label("Experimental. Detects 7 sounds + background. Others get a wrong guess.", 12, 38, COL_DIM)
    card(12, 64, 776, 272, "Sounds")
    w["tile"] = [ui.Button(" ", x=28 + 188 * (c % 4), y=96 + 92 * (c // 4), w=180, h=84, color=COL_BTN, value=20)
                 for c in range(8)]
    w["win"] = ui.Panel(x=184, y=100, w=20, h=20, color=COL_DIM, min=COL_DIM, max=10, value=0)  # on the winning tile: white = confirmed
    w["truth"] = label("Truth: tap a tile", 28, 284, COL_DIM)
    w["right"] = label("right 0 of 0", 28, 308, COL_TEXT)
    w["saw"] = label(" ", 316, 292, COL_DIM)
    w["ack"] = ui.Button("ACK", x=560, y=280, w=212, h=48, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, labels, eps, saw, top, blink):
    for c in range(len(eps)):
        put(w["tile"][c], "%s\n%d" % (labels[c], eps[c].n) if c else labels[0])
    if top != w["wp"]:  # move it only when the winner changes
        w["wp"] = top
        w["win"].pos(184 + 188 * (top % 4), 100 + 92 * (top // 4))
    put(w["win"], None, COL_TEXT if eps[top].st in (SEEN, UNACK, ACKED) else COL_DIM)
    s = eps[saw].st if saw > 0 else NORMAL  # the chainsaw alarm
    put(w["saw"], ("chainsaw: Quiet", "! chainsaw?", "!! CHAINSAW - ACK", "ACKED - still on",
                   "ENDED %s - ACK" % eps[saw].end)[s],
        (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG, COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s])
    put(w["ack"], None, COL_BAD if s in (UNACK, ENDED) else COL_BTN)


def close(w, mx, t, row0, labels):
    # score one truth window; the matrix goes to the console only
    s = "%s: right %d of %d" % (labels[t], mx[t][t] - row0[t], sum(mx[t]) - sum(row0))
    print(clock(), "truth", s)
    print("truth\\heard   ", " ".join("%4s" % x[:4] for x in labels))
    for c, r in enumerate(mx):
        print("%-14s" % labels[c], " ".join("%4d" % v for v in r))
    put(w["truth"], s, COL_DIM)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = read_th(i)
    nl = len(labels)
    saw = labels.index("chainsaw") if "chainsaw" in labels else -1
    b5, b6 = Button(0), Button(1)
    vs, eps = [Vote(K, N) for c in range(nl)], [Episode(c == saw) for c in range(nl)]
    mx = [[0] * nl for c in range(nl)]  # [truth][top class] per result
    truth, t_in, row0, top, paused, blink, nres = -1, 0, 0, 0, 0, 0, 0
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack = b6.pressed_now()
        for e in ui.poll() or ():
            h = e["handle"] if e["type"] == "clicked" else -1
            ack = ack or h == w["ack"].id()
            for c in range(nl):
                if h == w["tile"][c].id():  # the sound you will play now
                    if truth >= 0:
                        close(w, mx, truth, row0, labels)
                    truth, t_in, row0 = c, time.ticks_ms(), mx[c][:]
                    print(clock(), "truth", labels[c])
        if ack:
            eps[saw].ack()
        if b5.pressed_now():
            paused, vs = not paused, [Vote(K, N) for c in range(nl)]
            if paused:
                stop_ai()
                put(w["status"], "Paused - SW5 resumes", COL_WARN)
            else:
                start(w, i, n0)
        now = time.ticks_ms()
        if not paused and time.ticks_diff(now, polled) >= POLL_MS:
            polled = now
            r = read_result(i)
            if r:
                sc = r["scores"]
                nres, top = nres + 1, sc.index(max(sc))
                if truth >= 0:
                    mx[truth][top] += 1
                for c in range(1, nl):
                    v, p = vs[c], int(sc[c] * 100 + 0.5)
                    v.add(p)
                    if eps[c].step(p >= th[0][c], v.hits(th[1][c]), v.clear(th[2][c]), now):
                        print("%s %s %d%% #%d" % (clock(), labels[c], max(v.h), eps[c].n))
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            w["hb"].value(1 if blink and nres else 0)  # heartbeat: results since the last draw
            nres = 0
            left = TRUTH_MS - time.ticks_diff(now, t_in)
            if truth >= 0 and left <= 0:
                close(w, mx, truth, row0, labels)
                truth = -1
            elif truth >= 0:
                put(w["truth"], "Truth: %s %d s" % (labels[truth], (left + 999) // 1000), COL_TEXT)
            draw(w, labels, eps, saw, top, blink)
            put(w["right"], "right %d of %d" % (sum(x[c] for c, x in enumerate(mx)), sum(map(sum, mx))))
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
