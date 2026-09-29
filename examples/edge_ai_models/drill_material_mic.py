# drill_material_mic.py - Smart Drill. Needs DrillMaterialMic from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.
# INSTRUCTOR ONLY: clamp the work, eye + ear protection. SW6 (upper) or tap = ACK. SW5 (lower) = pause.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "DrillMaterialMic"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 2, 3  # breakthrough = 2 of the last 3 results at Confirm at
REFRACT_MS = 1500  # no new breakthrough sooner than this
HOLD_MS = 2000  # (not used here)
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Listening - SW6 = ACK, SW5 = pause" if buttons else "Listening"
DWELLS = (500,)  # ms on top before a material counts
CHIPS = ("air", "plastic", "wood")  # wood_out counts as wood
NEED = CHIPS + ("plastic_out", "wood_out")  # the classes this example needs
WORDS = ("THROUGH", "POWER CUT")  # Notify only (clears itself when the sound ends), Cut power (latched until ACK)

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
        s = [x.split("_")[0] for x in lb]
        self.cm = [CHIPS.index(x) if x in CHIPS else -1 for x in s] + [-1]  # chip of a class
        self.acc = self.cand = self.t0 = -1

    def step(self, c, p, now, dw):
        # one console line when the accepted state changes
        if c != self.cand:
            self.cand, self.t0 = c, now
        if c != self.acc and time.ticks_diff(now, self.t0) >= dw:
            self.acc, self.n = c, self.n + (c >= 0)
            print("%s %s %d%%" % (clock(), self.lb[c], p) + (" #%d" % self.n if c >= 0 else ""))


def build():
    ui.screen()
    time.sleep_ms(150)
    label("Smart Drill", 12, 4, COL_TEXT, 24)
    label("Instructor only. Detects: air, plastic, wood + breakthrough. Cannot: metal.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Now")
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "chips": []}
    w["verdict"] = label("Not sure", 28, 96, COL_DIM, 28)
    for j, s in enumerate(CHIPS):
        x = 28 + 148 * j
        w["chips"].append((ui.Panel(x=x, y=170, w=140, h=56, color=COL_CARD, min=COL_DIM, max=8, value=1),
                           label(s, x + 10, 186, COL_DIM, 20)))
    card(494, 64, 294, 272, "Breakthrough")
    w["disc"] = ui.Panel(x=508, y=100, w=56, h=56, color=COL_DIM, min=COL_DIM, max=28, value=0)  # a round light
    w["bv"] = label("not through", 576, 108, COL_DIM, 28)
    w["count"] = ui.Seg7(text="0", x=508, y=170, w=120, h=56, color=COL_TEXT)
    w["last"] = label("last -", 640, 186, COL_DIM)
    w["ack"] = ui.Button("ACK", x=508, y=266, w=130, h=60, color=COL_BTN, value=24)
    w["cut"] = ui.Button("Cut power ON", x=648, y=266, w=126, h=60, color=COL_BTN, value=16)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, d, s, blink, new):
    # s = the breakthrough latch. Cut power: UNACK and ENDED flash until ACK (an interlock).
    # Notify only: acknowledged at once, so run() passes SEEN = "through now": white, clears itself
    a, cm = d.acc, d.cm
    w["hb"].value(1 if blink and new else 0)  # heartbeat
    put(w["verdict"], d.lb[a], COL_TEXT if a >= 0 else COL_DIM)
    for j, (pn, t) in enumerate(w["chips"]):  # accepted white, candidate grey-blue
        k = 1 if j == cm[a] else 2 if j == cm[d.cand] else 0
        put(pn, None, (COL_CARD, COL_TEXT, COL_BTN)[k])
        put(t, None, (COL_DIM, COL_CARD, COL_TEXT)[k])
    put(w["disc"], None, (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG, COL_BAD,
                          COL_DIM if blink else COL_ALARM_BG, COL_TEXT)[s])
    put(w["bv"], ("not through", "! near exit", "!! POWER CUT", "ACKED", "ENDED", "THROUGH")[s],
        (COL_DIM, COL_WARN, COL_BAD, COL_TEXT, COL_TEXT, COL_TEXT)[s])
    put(w["ack"], None, COL_BAD if s in (UNACK, ENDED) else COL_BTN)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    if odd(labels):  # a model with other classes: a card, not wrong answers
        return
    th, outs = read_th(i), [j for j, s in enumerate(labels) if s.endswith("_out")]
    b5, b6, d = Button(0), Button(1), Dwell(labels)
    vote, ep, cut, paused, blink, new = Vote(K, N), Episode(True), 1, False, False, False
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack = b6.pressed_now()
        for e in ui.poll() or ():
            h = e["handle"] if e["type"] == "clicked" else -1
            ack = ack or h == w["ack"].id()
            if h == w["cut"].id():
                cut = 1 - cut
                put(w["cut"], ("Notify only", "Cut power ON")[cut])
        if b5.pressed_now():
            paused, vote = not paused, Vote(K, N)
            d.acc = d.cand = -1
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
                sc, new = r["scores"], True
                c = sc.index(max(sc))
                p = int(sc[c] * 100 + 0.5)
                d.step(c if p >= th[1][c] else -1, p, now, DWELLS[0])
                o = outs[0]  # the strongest *_out class, with its own thresholds
                for j in outs:
                    if sc[j] > sc[o]:
                        o = j
                q = int(sc[o] * 100 + 0.5)
                vote.add(q)
                if ep.step(q >= th[0][o], vote.hits(th[1][o]), vote.clear(th[2][o]), now):
                    print("%s %s %d%% %s #%d" % (clock(), labels[o], max(vote.h), WORDS[cut], ep.n))
                    put(w["count"], str(ep.n))
                    put(w["last"], "last " + clock())
        if ack or not cut:  # Notify only: nothing to acknowledge
            ep.ack()
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            draw(w, d, SEEN if ep.st == ACKED and not cut else ep.st, blink, new)
            new = False
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
