# siren_detection_store.py - Siren A/B. Needs SirenDetection from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.
# Play a siren clip, tap Swap, play it again. SW6/tap ACK = acknowledge, SW5 = pause.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "SirenDetection"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Listening - SW6 = ACK, SW5 = pause" if buttons else "Listening"
NAMES = (MODEL, "Siren Detection")  # A = Store, B = built-in: one space apart, two models

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


def disc(x, y, d):
    # a round Panel: an Led keeps its first colour on the board
    return ui.Panel(x=x, y=y, w=d, h=d, color=COL_DIM, min=COL_DIM, max=d // 2, value=0)


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


# ---- 4) This model's screen (T3 A/B) ----
class Side:
    # one card: model (index, labels) or None, alarm, tallies
    def __init__(self, m):
        self.m, self.ep, self.top, self.dly = m, Episode(True), 0, -1
        self.th = m and read_th(m[0])
        self.rest()

    def rest(self):
        # a card that stops starts fresh
        self.vote, self.t0, self.ep.st = Vote(K, N), None, NORMAL


def build():
    ui.screen()
    time.sleep_ms(150)
    label("Siren: Store vs Built-in", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "c": []}
    label("Same siren, two models, one at a time. Each counts at its own Confirm at.", 12, 38, COL_DIM)
    for k in (0, 1):
        x = 28 + 394 * k
        card(x - 16, 64, 382, 272, ("A: %s (Store)", "B: %s (built-in)")[k] % NAMES[k])
        w["c"].append([disc(x, 100, 56), label("paused", x + 68, 108, COL_DIM, 28), label(" ", x, 168, COL_TEXT, 20),
                       label(" ", x, 198, COL_TEXT), label(" ", x, 222, COL_DIM)])
    w["swap"] = ui.Button("Swap: run B", x=28, y=272, w=350, h=56, color=COL_BTN, value=20)
    w["ack"] = ui.Button("ACK", x=422, y=272, w=350, h=56, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, sides, on, peak, blink):
    # on = the listening card (-1 = paused)
    w["hb"].value(1 if blink and peak >= 0 else 0)
    for k, sd in enumerate(sides):
        c, s, r = w["c"][k], sd.ep.st, k == on
        if sd.m:
            put(c[0], None, (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG,
                             COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s])
            put(c[1], ("Quiet", "! hearing", "!! SIREN", "ACKED", "ENDED " + sd.ep.end)[s] if r else "paused",
                (COL_DIM, COL_WARN, COL_BAD, COL_BAD, COL_TEXT)[s])
            put(c[2], "now %d%% | max %d%%" % (max(peak, 0) if r else 0, sd.top))
            put(c[3], "sirens %d | Confirm at %d%%" % (sd.ep.n, sd.th[1][1]))
            put(c[4], "delay %.1f s" % (sd.dly / 1000) if sd.dly >= 0 else "delay -")
    put(w["ack"], None, COL_BAD if sides[max(on, 0)].ep.st in (UNACK, ENDED) else COL_BTN)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    global MODEL, BUILTIN
    MODEL, BUILTIN = NAMES[1], True  # B: same exact-name rule
    sides = [Side((i, labels)), Side(find_model())]
    MODEL, BUILTIN = NAMES[0], False
    if not sides[1].m:
        put(w["c"][1][1], "not on this board")
        put(w["swap"], "B not on this board")
    b5, b6 = Button(0), Button(1)
    k, paused, blink, peak = 0, False, False, -1
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack, swap, sd = b6.pressed_now(), False, sides[k]
        for e in ui.poll() or ():
            h = e["handle"]
            ack = ack or h == w["ack"].id()
            swap = swap or h == w["swap"].id()
        if ack:
            sd.ep.ack()
        if swap and sides[1 - k].m and not paused:
            sd.rest()
            stop_ai()
            k = 1 - k
            sd, MODEL = sides[k], NAMES[k]  # start() shows this name
            print(clock(), "swap: run", MODEL)
            start(w, sd.m[0], n0)  # "Loading <name> ..." first; select() blocks up to 15 s
            put(w["swap"], "Swap: run " + "AB"[1 - k])
            ui.poll()  # taps made during the load are dropped: no swap back, no ACK of the new card
            b5.sample()
            b6.sample()
            b5.pressed_now()
            b6.pressed_now()
        if b5.pressed_now():
            paused = not paused
            sd.rest()
            if paused:
                stop_ai()
                put(w["status"], "Paused - SW5 resumes", COL_WARN)
            else:
                start(w, sd.m[0], n0)
        now = time.ticks_ms()
        if not paused and time.ticks_diff(now, polled) >= POLL_MS:
            polled = now
            r = read_result(sd.m[0])
            if r:
                v = sd.vote
                e, p, en, cf, ex = event_of(r, sd.th)
                v.add(p)
                peak, sd.top = max(peak, p), max(sd.top, p)
                if v.clear(ex):
                    sd.t0 = None
                elif p >= cf and sd.t0 is None:
                    sd.t0 = now  # onset = first score at Confirm at: delay = the price of the rule
                if sd.ep.step(p >= en, v.hits(cf), v.clear(ex), now):
                    sd.dly = time.ticks_diff(now, sd.t0 or now)
                    print("%s %s %s %d%% #%d" % (clock(), "AB"[k], sd.m[1][e], max(v.h), sd.ep.n))
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            draw(w, sides, -1 if paused else k, peak, blink)
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
