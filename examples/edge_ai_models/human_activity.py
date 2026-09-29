# human_activity.py - Activity Timeline. Needs HumanActivity from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3. Eva Kit and BENTO Emulator: not tested yet.
# Wear the board on your chest. SW6 (upper) or tap = minimum time. SW5 (lower) = pause/resume.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "HumanActivity"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 3, 5  # (not used by T4)
REFRACT_MS = 1500  # (not used by T4)
HOLD_MS = 2000  # (not used by T4)
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Wear it on your chest - SW6 = Min, SW5 = pause" if buttons else "Wear it on your chest"
DWELLS = (0, 5000, 10000)  # ms on top before an activity counts
CHIPS = NEED = ("standing", "running", "walking", "sitting", "jumping")  # unlabeled = none of them

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


def missing(t="Model not on this board",
            s="Deploy " + MODEL + "\nfrom edgeai-store.tesaiot.dev\n(sign in with GitHub) on a Dev Kit."):
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
        self.lb, self.n = lb + ["Not sure"], 0
        self.cm = [CHIPS.index(s) if s in CHIPS else -1 for s in lb] + [-1]  # chip of a class
        self.acc = self.cand = self.t0 = -1

    def step(self, c, p, now, dw):
        # True when the accepted state changes; one console line each
        if c != self.cand:
            self.cand, self.t0 = c, now
        if c == self.acc or time.ticks_diff(now, self.t0) < dw:
            return False
        k = self.cm[c] >= 0  # an activity; unlabeled and Not sure get no number
        self.acc, self.n = c, self.n + k
        print("%s %s %d%%" % (clock(), self.lb[c], p) + (" #%d" % self.n if k else ""))
        return True


def setv(o, v):
    # a value only when it changed
    k = o.id() + 1000
    if _sent.get(k) != v:
        _sent[k] = v
        o.value(v)


def build():
    ui.screen()
    time.sleep_ms(150)
    label("Activity Timeline", 12, 4, COL_TEXT, 24)
    label("Detects 5 activities when worn. Cannot: work lying on a desk (reads sitting).", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Now")
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "chips": [], "rows": [], "bars": []}
    w["verdict"] = label("Not sure", 28, 96, COL_DIM, 28)
    for j, s in enumerate(CHIPS):
        x = 28 + 88 * j
        w["chips"].append((ui.Panel(x=x, y=170, w=80, h=56, color=COL_CARD, min=COL_DIM, max=8, value=1),
                           label(s, x + 5, 188, COL_DIM)))
    w["bar"] = ui.Bar(x=28, y=236, w=436, h=10, max=100)
    w["bar"].color(COL_TEXT)
    w["cand"] = label(" ", 28, 250, COL_DIM)
    card(494, 64, 294, 272, "Time per activity")
    for j, s in enumerate(CHIPS):  # m:ss and share of the counted time
        w["rows"].append(label(s + " 0:00", 508, 96 + 34 * j, COL_DIM))
        w["bars"].append(ui.Bar(x=508, y=116 + 34 * j, w=266, h=8, max=100))
        w["bars"][j].color(COL_TEXT)
    w["dw"] = ui.Button("Min 5 s", x=508, y=266, w=266, h=60, color=COL_BTN, value=20)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, d, dw, now, blink, tp):
    # tp = (top class, %, Confirm at) of a new result, else None
    a, cd, cm = d.acc, d.cand, d.cm
    w["hb"].value(1 if blink and tp else 0)  # heartbeat
    put(w["verdict"], d.lb[a], COL_TEXT if cm[a] >= 0 else COL_DIM)  # unlabeled = no activity: dim
    for j, (pn, t) in enumerate(w["chips"]):  # accepted = white, candidate = grey-blue
        k = 1 if j == cm[a] else 2 if j == cm[cd] else 0
        put(pn, None, (COL_CARD, COL_TEXT, COL_BTN)[k])
        put(t, None, (COL_DIM, COL_CARD, COL_TEXT)[k])
    el = time.ticks_diff(now, d.t0) if cd != a and dw else 0
    setv(w["bar"], min(100, el * 100 // (dw or 1)))
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
    dw, ms, paused, blink, tp = 1, [0] * 5, False, False, None
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
            put(w["dw"], "Min %d s" % (DWELLS[dw] // 1000))
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
                d.step(c if p >= th[c] else -1, p, now, DWELLS[dw])
        dt = time.ticks_diff(now, drawn)
        if dt >= DRAW_MS:
            drawn, blink, k = now, not blink, d.cm[d.acc]
            if k >= 0:
                ms[k] += dt  # time in the accepted activity (none while paused)
            draw(w, d, DWELLS[dw], now, blink, tp)
            for j, s in enumerate(CHIPS):
                t = ms[j] // 1000
                put(w["rows"][j], "%s %d:%02d" % (s, t // 60, t % 60), COL_TEXT if j == k else COL_DIM)
                setv(w["bars"][j], ms[j] * 100 // (sum(ms) or 1))
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
