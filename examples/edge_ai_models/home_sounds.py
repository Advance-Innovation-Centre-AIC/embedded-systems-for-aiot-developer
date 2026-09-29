# home_sounds.py - Three-Sound Board. Needs HomeSounds from the Edge AI Store.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3. Eva Kit and BENTO Emulator: not tested yet.
# Tap a tile = mute/unmute it. SW6 (upper) or ACK = acknowledge the tap alarm. SW5 (lower) = pause.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "HomeSounds"  # exact name, looked up every run
BUILTIN = False  # False = deployed from the Store
K, N = 3, 5  # event = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (40, 65, 30)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Listening - tap a tile = mute, SW6 = ACK, SW5 = pause" if buttons else "Listening - tap a tile = mute"
TAP_S = 30  # water_tap running this long = alarm

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


# ---- 4) This model's screen (T5 tiles) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Three-Sound Board", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0)}
    label("Detects: baby cry, cough, running tap. Other sounds read as unlabeled.", 12, 38, COL_DIM)
    card(12, 64, 776, 272, "Sounds")
    w["tile"] = [ui.Button(" ", x=x, y=96, w=236, h=130, color=COL_BTN, value=20) for x in (28, 280, 532)]
    w["disc"] = [disc(x, 236, 28) for x in (28, 280, 532)]
    w["st"] = [label("Quiet", x + 36, 240, COL_DIM) for x in (28, 280, 532)]
    w["log"] = label(" ", 28, 280, COL_TEXT)
    w["ack"] = ui.Button("ACK", x=508, y=270, w=264, h=56, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    w["health"] = label(" ", 12, 370, COL_DIM)
    ui.poll()
    return w


def draw(w, labels, eps, last, mute, tap, on, blink):
    # on = seconds the tap has run (0 = not running); put() sends only changes
    for c in range(1, len(eps)):
        s = eps[c].st
        put(w["tile"][c - 1], "%s %dx\nlast %s%s" % (labels[c], eps[c].n, last[c] or "-", "\nMUTED" if mute[c] else ""))
        if mute[c]:
            d, t, k = COL_DIM, "muted", COL_DIM
        elif c == tap:  # alarm: grey > amber > red until ACK
            d = (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG, COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s]
            k = (COL_DIM, COL_WARN, COL_BAD, COL_TEXT, COL_TEXT)[s]
            t = ("Quiet", "! running %d s" % on if on else "! hearing", "!! RUNNING %d s - ACK" % on,
                 "ACKED - running %d s" % on, "ENDED %s - ACK" % eps[c].end)[s]
        else:  # info: white while recognised
            d = k = COL_TEXT if s == SEEN else COL_DIM
            t = labels[c].upper() if s == SEEN else ("maybe " + labels[c] if s == HEARD else "Quiet")
        put(w["disc"][c - 1], None, d)
        put(w["st"][c - 1], t, k)
    put(w["ack"], None, COL_BAD if eps[tap].st in (UNACK, ENDED) else COL_BTN)  # tap -1: never latched


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = read_th(i)
    nc = min(len(labels), 4)
    tap = labels.index("water_tap") if "water_tap" in labels[:nc] else -1
    b5, b6 = Button(0), Button(1)
    vs, eps = [Vote(K, N) for c in range(nc)], [Episode(c == tap) for c in range(nc)]
    mute, last, t_on, paused, blink, nres, lat = [0] * nc, [""] * nc, None, 0, 0, 0, 0
    start(w, i)
    drawn = polled = h0 = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack = b6.pressed_now()
        for e in ui.poll() or ():
            h = e["handle"] if e["type"] == "clicked" else -1
            ack = ack or h == w["ack"].id()
            for c in range(1, nc):
                if h == w["tile"][c - 1].id():  # mute / unmute this sound
                    mute[c], vs[c], eps[c].st = not mute[c], Vote(K, N), NORMAL
                    t_on = None if c == tap else t_on
                    print(clock(), labels[c], "muted" if mute[c] else "on")
        if ack:
            eps[tap].ack()
        if b5.pressed_now():
            paused, t_on, vs = not paused, None, [Vote(K, N) for c in range(nc)]
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
                nres, lat = nres + 1, lat + r.get("latency_ms", 0)
                for c in range(1, nc):
                    v, p, ep = vs[c], int(r["scores"][c] * 100 + 0.5), eps[c]
                    v.add(p)
                    hit, clr, on = v.hits(th[1][c]), v.clear(th[2][c]), 0
                    if c == tap:  # running = confirmed once, until cleared
                        t_on = None if clr else (now if t_on is None and hit else t_on)
                        on = t_on is not None
                        hit = on and time.ticks_diff(now, t_on) >= TAP_S * 1000
                    if not mute[c] and ep.step(p >= th[0][c] or on, hit, clr, now):
                        last[c] = clock()
                        s = "%s %s %d%% #%d" % (last[c], labels[c], max(v.h), ep.n)
                        print(s)
                        put(w["log"], s)
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            w["hb"].value(1 if blink and nres else 0)  # heartbeat
            draw(w, labels, eps, last, mute, tap, time.ticks_diff(now, t_on) // 1000 if t_on is not None else 0, blink)
            dt = time.ticks_diff(now, h0)
            if dt >= 2000:
                put(w["health"], "%.1f results/s | %.1f ms" % (nres * 1000 / dt, lat / max(nres, 1)))
                h0, nres, lat = now, 0, 0
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
