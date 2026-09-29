# push_detection.py - Touchless Button. Built-in model; needs the Dev Kit radar.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3. Eva Kit and BENTO Emulator: not tested yet.
# Push a palm toward the radar, about 60 cm away (Infineon's radar example). SW6 or tap = refractory 0.3/1/2 s. SW5 = pause/resume.
# Runs Push at Hearing at 20%, Confirm at 30% (HMI Kit data: 60 cm pushes peaked at 21-39%, under the
# Dev Kit's 40/65/30). NOT yet checked on a Dev Kit. The board's own numbers are put back at the end.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Push Detection"  # exact name, looked up every run
BUILTIN = True  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1000  # no second push sooner than this (SW6 / touch: 0.3, 1, 2 s)
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Push your palm - SW6 = refractory, SW5 = pause" if buttons else "Push your palm toward the radar"
# teaching thresholds 20/30: set at the top of run() (see the header)

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
    s = MODEL + "\nis built into the TESAIoT Dev Kit.\nNot on this board."
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


def meter(w, p):
    # 10 LEDs; switch only the ones that change
    lit = (p + 5) // 10
    for i in range(min(lit, w["lit"]), max(lit, w["lit"])):
        w["vu"][i].value(1 if i < lit else 0)
    w["lit"] = lit


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


def set_th(i, k, which, pct):
    # the board keeps clear <= Hearing at <= Confirm at: show what it read back
    try:
        edge_ai.thresholds(i, k, which, pct)
    except Exception as e:
        print("cannot set threshold:", e)
    return read_th(i)


def restore_th(i, th0, th):
    # put back what this program changed; enter, confirm, exit in this order undoes the board's clamp
    if th != th0:
        for j, k in enumerate(("enter", "confirm", "exit")):
            set_th(i, 1, k, th0[j][1])


def ticks(w, th, k):
    # marks over the meter at Hearing at (grey) and Confirm at (white)
    w["t_en"].pos(28 + th[0][k] * 432 // 100, 256)
    w["t_cf"].pos(28 + th[1][k] * 432 // 100, 256)
    put(w["legend"], "Hearing at %d%% | Confirm at %d%%" % (th[0][k], th[1][k]))


# ---- 4) This model's screen (T1 annunciator, no chart, no ACK) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Touchless Button", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "lit": 0}
    label("Detects: a palm push toward the radar, ~60 cm. Cannot: other gestures. Demo.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Now")
    w["disc"] = ui.Panel(x=28, y=100, w=140, h=140, color=COL_DIM, min=COL_DIM, max=70, value=0)  # a round light
    w["verdict"] = label("Ready", 184, 110, COL_DIM, 28)
    w["state"] = label(" ", 184, 150, COL_DIM)
    w["vu"] = [ui.Led(x=28 + 44 * i, y=262, w=36, h=20, color=COL_TEXT, value=0) for i in range(10)]
    w["t_en"] = ui.Panel(x=28, y=256, w=4, h=32, color=COL_DIM)
    w["t_cf"] = ui.Panel(x=28, y=256, w=4, h=32, color=COL_TEXT)
    w["legend"] = label(" ", 28, 290, COL_DIM)
    card(494, 64, 294, 272, "Pushes")
    w["count"] = ui.Seg7(text="0", x=508, y=96, w=120, h=56, color=COL_TEXT)
    w["last"] = label("last -", 640, 110, COL_DIM)
    w["near"] = label("< 1 s apart: 0", 508, 164, COL_TEXT)
    w["lines"] = [label(" ", 508, 188 + 24 * j, COL_TEXT) for j in range(3)]
    w["rf"] = ui.Button("Refractory 1 s", x=508, y=266, w=266, h=60, color=COL_BTN, value=20)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, ep, peak, blink):
    # info kind: grey, then white for HOLD_MS; "maybe" only in dim text
    s, p = ep.st, max(peak, 0)
    w["hb"].value(1 if blink and peak >= 0 else 0)  # heartbeat, peak -1 = no new result
    put(w["disc"], None, COL_TEXT if s == SEEN else COL_DIM)
    put(w["verdict"], "PUSH" if s == SEEN else ("maybe push" if s == HEARD else "Ready"),
        COL_TEXT if s == SEEN else COL_DIM)
    put(w["state"], "score %d%%" % p)
    meter(w, p)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    global REFRACT_MS
    th0 = read_th(i)
    set_th(i, 1, "enter", 20)  # teaching numbers (see the header)
    th = set_th(i, 1, "confirm", 30)  # exit follows enter down to 20
    ticks(w, th, 1)
    b5, b6 = Button(0), Button(1)
    vote, ep = Vote(K, N), Episode(False)
    log, rf, near, last, paused, blink, peak = [], 1, 0, None, False, False, -1
    try:
        start(w, i)
        drawn = polled = time.ticks_ms()
        while True:
            b5.sample()
            b6.sample()
            tap = b6.pressed_now()
            for e in ui.poll() or ():
                tap = tap or e["handle"] == w["rf"].id()
            if tap:  # refractory 0.3 -> 1 -> 2 s
                rf = (rf + 1) % 3
                REFRACT_MS = (300, 1000, 2000)[rf]
                put(w["rf"], "Refractory %s s" % ("0.3", "1", "2")[rf])
                print(clock(), "refractory", REFRACT_MS, "ms")
            if b5.pressed_now():
                paused = not paused
                vote, ep.st = Vote(K, N), NORMAL
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
                    e, p, en, cf, ex = event_of(r, th)
                    vote.add(p)
                    peak = max(peak, p)
                    if ep.step(p >= en, vote.hits(cf), vote.clear(ex), now):
                        if last is not None and time.ticks_diff(now, last) < 1000:
                            near += 1  # a double push, or one push counted twice
                            put(w["near"], "< 1 s apart: %d" % near)
                        last = now
                        log = ["%s %s %d%% #%d" % (clock(), labels[e], max(vote.h), ep.n)] + log[:2]
                        print(log[0])
                        for j, s in enumerate(log):  # newest first
                            put(w["lines"][j], s)
                        put(w["count"], str(ep.n))
                        put(w["last"], "last " + clock())
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn, blink = now, not blink
                draw(w, ep, peak, blink)
                peak = -1
            time.sleep_ms(20)
    finally:  # the board keeps them until reboot: put its own back
        restore_th(i, th0, th)


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
