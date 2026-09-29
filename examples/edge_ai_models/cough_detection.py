# cough_detection.py - Barn Cough Counter. Built-in model.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.
# SW6 (upper) or tap ACK = acknowledge the red alarm. SW5 (lower) = pause/resume.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Cough Detection"  # exact name, looked up every run
BUILTIN = True  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Listening - SW6 = ACK, SW5 = pause" if buttons else "Listening"
RATE_WARN, RATE_BAD = 3, 6  # coughs/min: amber, red until ACK

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

    def ack(self):
        self.st = {UNACK: ACKED, ENDED: NORMAL}.get(self.st, self.st)


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


# ---- 4) This model's screen (T1 annunciator) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Barn Cough Counter", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "lit": 0}
    label("Detects: coughs (trained on people). Cannot: tell who, or diagnose illness.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Now")
    w["disc"] = ui.Panel(x=28, y=100, w=72, h=72, color=COL_DIM, min=COL_DIM, max=36, value=0)  # a round light
    w["verdict"] = label("Quiet", 116, 104, COL_DIM, 28)
    w["state"] = label(" ", 116, 144, COL_DIM)
    w["vu"] = [ui.Led(x=28 + 44 * i, y=190, w=36, h=20, color=COL_TEXT, value=0) for i in range(10)]
    w["legend"] = label(" ", 28, 218, COL_DIM)
    w["win"] = ui.Button("Window 1 min", x=336, y=248, w=132, h=76, color=COL_BTN, value=16)
    card(494, 64, 294, 272, "Coughs")
    w["count"] = ui.Seg7(text="0", x=508, y=96, w=120, h=56, color=COL_TEXT)
    w["rate"] = label("0.0 /min", 640, 100, COL_TEXT, 20)
    w["last"] = label("last -", 640, 130, COL_DIM)
    w["lines"] = [label(" ", 508, 166 + 24 * j, COL_TEXT) for j in range(4)]
    w["ack"] = ui.Button("ACK", x=508, y=266, w=266, h=60, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, cough, rate, rt, peak, blink):
    # peak = top score since the last draw, -1 = no new result
    s, c, p = rate.st, cough.st, max(peak, 0)
    w["hb"].value(1 if blink and peak >= 0 else 0)  # heartbeat
    put(w["disc"], None, (COL_TEXT if c == SEEN else COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG,
                          COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s])  # UNACK, ENDED flash at 1 Hz
    put(w["state"], ("cough score %d%%" % p, "! rate high %.1f /min" % rt, "!! HIGH COUGH RATE - tap ACK",
                     "ACKED - rate still high", "ENDED %s - tap ACK" % rate.end)[s],
        (COL_DIM, COL_WARN, COL_BAD, COL_TEXT, COL_TEXT)[s])
    put(w["verdict"], "COUGH" if c == SEEN else ("maybe cough" if c == HEARD else "Quiet"),
        COL_TEXT if c == SEEN else COL_DIM)
    put(w["ack"], None, COL_BAD if s in (UNACK, ENDED) else COL_BTN)
    put(w["rate"], "%.1f /min" % rt, (COL_TEXT, COL_WARN, COL_BAD)[(rt >= RATE_WARN) + (rt >= RATE_BAD)])
    meter(w, p)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = read_th(i)
    put(w["legend"], "Hearing at %d%% | Confirm at %d%%" % (th[0][1], th[1][1]))
    b5, b6 = Button(0), Button(1)
    vote, cough, rate = Vote(K, N), Episode(False), Episode(True)
    times, log, win, paused, blink, peak = [], [], 1, False, False, -1
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack = b6.pressed_now()
        for e in ui.poll() or ():
            h = e["handle"] if e["type"] == "clicked" else -1
            ack = ack or h == w["ack"].id()
            if h == w["win"].id():
                win = 6 - win  # 1 <-> 5 minutes
                put(w["win"], "Window %d min" % win)
        if ack:
            rate.ack()
        if b5.pressed_now():
            paused = not paused
            vote, cough.st = Vote(K, N), NORMAL
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
                if cough.step(p >= en, vote.hits(cf), vote.clear(ex), now):
                    times.append(now)
                    log = ["%s %s %d%% #%d" % (clock(), labels[e], max(vote.h), cough.n)] + log[:3]
                    print(log[0])
                    for j, s in enumerate(log):  # newest first
                        put(w["lines"][j], s)
                    put(w["count"], str(cough.n))
                    put(w["last"], "last " + clock())
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            while times and time.ticks_diff(now, times[0]) > 300000:
                times.pop(0)  # keep 5 min
            rt = sum(1 for t in times if time.ticks_diff(now, t) <= win * 60000) / win
            rate.step(rt >= RATE_WARN, rt >= RATE_BAD, rt < RATE_WARN, now)
            draw(w, cough, rate, rt, peak, blink)
            peak = -1
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
