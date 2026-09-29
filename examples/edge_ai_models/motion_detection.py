# motion_detection.py - Gesture Switch. Built-in model.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3. Eva Kit and BENTO Emulator: not tested yet.
# A circle in the air flips the lamp. SW6 (upper) or tap = dwell. SW5 (lower) = pause/resume.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Motion Detection"  # exact name, looked up every run
BUILTIN = True  # False = deployed from the Store
K, N = 3, 5  # (not used by T4)
REFRACT_MS = 1500  # (not used by T4)
HOLD_MS = 2000  # (not used by T4)
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Move the board - SW6 = Dwell, SW5 = pause" if buttons else "Move the board"
DWELLS = (0, 500, 1000)  # ms on top before a gesture counts (a USB cable tug moves it too)
CHIPS = NEED = ("idle", "circle", "shaking")  # NEED: the classes this example needs

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


def missing(t="Model not on this board", s=MODEL + "\nis built into the TESAIoT Dev Kit.\nNot on this board."):
    print(s)
    ui.screen()
    time.sleep_ms(150)
    ui.Panel(x=96, y=110, w=600, h=180, color=COL_CARD, min=COL_BAD, max=12, value=1)
    label(t, 120, 126, COL_BAD, 24)
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


def odd(labels):
    # a model whose classes are not the ones this example needs: say so on the card, never guess
    x = [s for s in NEED if s not in labels]
    if x:
        missing("Model classes differ from this example", MODEL + " has no class\n" + ", ".join(x))
    return x


# ---- 4) This model's screen (T4 state + dwell) ----
class Dwell:
    # a class counts after dw ms on top at Confirm at; -1 = Not sure
    def __init__(self, lb):
        self.lb, self.chat, self.n, self.v = lb + ["Not sure"], 0, 0, 0
        self.cm = [CHIPS.index(s) if s in CHIPS else -1 for s in lb] + [-1]  # chip of a class
        self.acc = self.cand = self.t0 = -1

    def step(self, c, p, now, dw):
        # True when the accepted state changes; one console line each
        if c != self.cand:
            self.chat += self.cand != self.acc  # dropped before its dwell = chatter
            self.cand, self.t0 = c, now
        if c == self.acc or time.ticks_diff(now, self.t0) < dw:
            return False
        self.acc, self.n = c, self.n + (c >= 0)
        print("%s %s %d%%" % (clock(), self.lb[c], p) + (" #%d" % self.n if c >= 0 else ""))
        return True


def build():
    ui.screen()
    time.sleep_ms(150)
    label("Gesture Switch", 12, 4, COL_TEXT, 24)
    label("Detects: idle, circle, shaking. Cannot: other gestures or who moved it.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Now")
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "chips": []}
    w["verdict"] = label("Not sure", 28, 96, COL_DIM, 28)
    for j, s in enumerate(CHIPS):
        x = 28 + 148 * j
        w["chips"].append((ui.Panel(x=x, y=170, w=140, h=56, color=COL_CARD, min=COL_DIM, max=8, value=1),
                           label(s, x + 10, 186, COL_DIM, 20)))
    w["bar"] = ui.Bar(x=28, y=236, w=436, h=10, max=100)
    w["bar"].color(COL_TEXT)
    w["cand"] = label(" ", 28, 250, COL_DIM)
    card(494, 64, 294, 272, "Lamp")
    w["lamp"] = ui.Panel(x=560, y=100, w=120, h=120, color=COL_CARD, min=COL_DIM, max=60, value=2)
    w["tc"] = label(" ", 508, 236, COL_TEXT)
    w["dw"] = ui.Button("Dwell 0.5 s", x=508, y=266, w=266, h=60, color=COL_BTN, value=20)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, d, dw, now, blink, tp):
    # tp = (top class, %, Confirm at) of a new result, else None
    a, cd, cm = d.acc, d.cand, d.cm
    w["hb"].value(1 if blink and tp else 0)  # heartbeat
    put(w["verdict"], d.lb[a], COL_TEXT if a >= 0 else COL_DIM)
    for j, (pn, t) in enumerate(w["chips"]):  # accepted = white, candidate = grey-blue
        k = 1 if j == cm[a] else 2 if j == cm[cd] else 0
        put(pn, None, (COL_CARD, COL_TEXT, COL_BTN)[k])
        put(t, None, (COL_DIM, COL_CARD, COL_TEXT)[k])
    el = time.ticks_diff(now, d.t0) if cd != a and dw else 0
    v = min(100, el * 100 // (dw or 1))
    if v != d.v:
        d.v = v
        w["bar"].value(v)
    if el:
        put(w["cand"], "%s %.1f/%.1f s" % (d.lb[cd], el / 1000, dw / 1000))
    elif tp:
        put(w["cand"], "top %s %d%% | Confirm at %d%%" % tp)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    if odd(labels):  # a model with other classes: a card, not wrong answers
        return
    th = read_th(i)[1]  # Confirm at, per class
    b5, b6, d = Button(0), Button(1), Dwell(labels)
    dw, lamp, tog, paused, blink, tp = 1, False, 0, False, False, None
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        tap = b6.pressed_now()
        for e in ui.poll() or ():
            tap = tap or e["type"] == "clicked" and e["handle"] == w["dw"].id()
        if tap:
            dw = (dw + 1) % len(DWELLS)
            put(w["dw"], "Dwell %.1f s" % (DWELLS[dw] / 1000))
        if b5.pressed_now():
            paused = not paused
            d.acc = d.cand = -1  # start again from Not sure
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
                c = sc.index(max(sc))
                p = int(sc[c] * 100 + 0.5)
                tp = (labels[c], p, th[c])
                if d.step(c if p >= th[c] else -1, p, now, DWELLS[dw]):
                    s = d.lb[d.acc]
                    if s == "circle":  # each accepted circle flips the lamp
                        lamp, tog = not lamp, tog + 1
                        put(w["lamp"], None, COL_TEXT if lamp else COL_CARD)
                    if s == "shaking":  # farm: a shaken pole = tamper?
                        put(w["status"], "Shaken at %s - tamper?" % clock(), COL_WARN)
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            draw(w, d, DWELLS[dw], now, blink, tp)
            put(w["tc"], "Lamp %s | toggles %d | chatter %d" % ("ON" if lamp else "OFF", tog, d.chat))
            tp = None
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
