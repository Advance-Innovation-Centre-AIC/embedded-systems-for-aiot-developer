# anomalous_vibration.py - Pump Health. Needs AnomalousVibration from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.
# Tape it to a guarded fan. Sliders set Alert/Danger. SW6/tap ACK = acknowledge, SW5 = pause.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "AnomalousVibration"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Running - SW6 = ACK, SW5 = pause" if buttons else "Running"

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


# ---- 4) This model's screen (T2 gauge) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Pump Health", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "nd": -1}
    label("Detects: vibration unlike its training machine. Cannot: name the fault.", 12, 38, COL_DIM)
    card(12, 64, 470, 272, "Anomaly")
    g = w["gauge"] = ui.Scale(x=28, y=96, w=170, h=170, color=COL_TEXT, max=100)
    g.prop(ui.PROP_SCALE_MODE, ui.SCALE_ROUND_IN)
    g.ticks(11, 2)
    g.prop(ui.PROP_SCALE_NEEDLE_COLOR, COL_TEXT)
    w["disc"] = ui.Panel(x=214, y=100, w=56, h=56, color=COL_DIM, min=COL_DIM, max=28, value=0)  # a round light
    w["verdict"] = label("Normal", 280, 108, COL_DIM, 28)
    w["state"] = label(" ", 214, 166, COL_DIM)
    w["lines"] = [label(" ", 214, 194 + 24 * j, COL_TEXT) for j in range(2)]
    card(494, 64, 294, 272, "Thresholds")
    for k, y in (("enter", 96), ("confirm", 150)):
        w["l" + k] = label(" ", 508, y, COL_TEXT)
        w[k] = ui.Slider(x=520, y=y + 28, w=250, h=12, max=100)
        w[k].color(COL_INFO)  # Slider colour: after creation
    w["ack"] = ui.Button("ACK", x=508, y=266, w=266, h=60, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, ep, th, peak, top, na, al, blink):
    s, p = ep.st, max(peak, 0)
    v = 2 if s in (UNACK, ACKED) else 1 if al else 0
    w["hb"].value(1 if blink and peak >= 0 else 0)
    put(w["disc"], None, (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG,
                          COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s])
    put(w["state"], ("score %d%%" % p, "! ALERT - score %d%%" % p, "!! DANGER - tap ACK",
                     "ACKED - still high", "ENDED %s - tap ACK" % ep.end)[s],
        (COL_DIM, COL_WARN, COL_BAD, COL_BAD, COL_TEXT)[s])
    put(w["verdict"], ("Normal", "ALERT", "DANGER")[v], (COL_DIM, COL_WARN, COL_BAD)[v])
    put(w["ack"], None, COL_BAD if s in (UNACK, ENDED) else COL_BTN)
    put(w["lines"][0], "Alerts %d | Dangers %d" % (na, ep.n))
    put(w["lines"][1], "Peak %d%%" % top)
    if w["nd"] != p:  # needle = (length << 16) | value
        w["nd"] = p
        w["gauge"].prop(ui.PROP_SCALE_NEEDLE, 60 << 16 | p)
    if w.get("th") is not th:  # new read-back
        w["th"] = th
        put(w["lenter"], "Hearing at %d%% = Alert" % th[0][1])
        put(w["lconfirm"], "Confirm at %d%% = Danger" % th[1][1])
        w["enter"].value(th[0][1])
        w["confirm"].value(th[1][1])


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = th0 = read_th(i)
    b5, b6 = Button(0), Button(1)
    vote, ep = Vote(K, N), Episode(True)
    pend, na, top, al, paused, blink, peak = {}, 0, 0, False, False, False, -1
    start(w, i)
    drawn = polled = time.ticks_ms()
    try:
        while True:
            b5.sample()
            b6.sample()
            ack = b6.pressed_now()
            for e in ui.poll() or ():
                h = e["handle"]
                ack = ack or h == w["ack"].id()
                if e["type"] == "value_changed":  # a slider: set at the next draw
                    pend["enter" if h == w["enter"].id() else "confirm"] = e["value"]
            if ack:
                ep.ack()
            if b5.pressed_now():
                paused = not paused
                vote, al = Vote(K, N), False
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
                    p, en, cf, ex = int(r["scores"][1] * 100 + 0.5), th[0][1], th[1][1], th[2][1]  # class 1: anomaly
                    vote.add(p)
                    peak, top = max(peak, p), max(top, p)
                    s, al = al, p >= en or al and not vote.clear(ex)  # Alert: not latched
                    if al and not s:
                        na += 1
                        print(clock(), "alert %d%%" % p)
                    if ep.step(p >= en, vote.hits(cf), vote.clear(ex), now):
                        print("%s %s %d%% #%d" % (clock(), labels[1], max(vote.h), ep.n))
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn, blink = now, not blink
                for k in pend:
                    th = set_th(i, 1, k, pend[k])
                pend = {}
                draw(w, ep, th, peak, top, na, al, blink)
                peak = -1
            time.sleep_ms(20)
    finally:  # the sliders' numbers would outlive the program: put the originals back
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
