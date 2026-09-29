# alarm_detection.py - Alarm Listener. Built-in model.
# Tested: TESAIoT Dev Kit, fw 2.4.2, IDE Run 3/3 (2026-09-29). Eva Kit and BENTO Emulator: not tested yet.

try:
    import buttons
except ImportError:  # none on the Eva Kit
    buttons = None
import edge_ai
import gc
import time
import ui

# ---- 1) Settings ----
MODEL = "Alarm Detection"  # exact name, looked up every run
BUILTIN = True  # False = deployed from the Store
K, N = 2, 3  # a beep burst = K of the last N results at Confirm at
REFRACT_MS = 1500  # no second event sooner than this
HOLD_MS = 2000  # an event stays on screen this long
DEF_TH = (70, 85, 55)  # Hearing at, Confirm at, clear below (fallback)
POLL_MS, DRAW_MS = 100, 500
HELP = "Listening - SW6 = ACK, SW5 = pause" if buttons else "Listening"
GAP_MS, T3_MS, SNOOZE_S = 300, 4000, 60  # quiet between bursts; 2 bursts in T3_MS = alarm; shelving

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


# ---- 4) This model's screen (T1 annunciator + takeover) ----
def build():
    ui.screen()
    time.sleep_ms(150)
    label("Alarm Listener", 12, 4, COL_TEXT, 24)
    w = {"hb": ui.Led(x=756, y=6, w=26, h=26, color=COL_OK, value=0), "lit": 0}
    label("Teaching demo - NOT a safety device. Detects alarm beeps, not smoke or CO.", 12, 38, COL_DIM)
    w["cl"] = card(12, 64, 470, 272, "Now")
    w["disc"] = disc(28, 100, 72)
    w["verdict"] = label("Quiet", 116, 104, COL_DIM, 28)
    w["state"] = label(" ", 116, 144, COL_DIM)
    w["vu"] = [ui.Led(x=28 + 44 * i, y=190, w=36, h=20, color=COL_TEXT, value=0) for i in range(10)]
    w["legend"] = label(" ", 28, 218, COL_DIM)
    w["snz"] = ui.Button("Snooze %d s" % SNOOZE_S, x=336, y=248, w=132, h=76, color=COL_BTN, value=16)
    w["cr"] = card(494, 64, 294, 272, "Alarms")
    w["count"] = ui.Seg7(text="0", x=508, y=96, w=120, h=56, color=COL_TEXT)
    w["lines"] = [label(" ", 508, 166 + 24 * j, COL_TEXT) for j in range(4)]
    w["ack"] = ui.Button("ACK", x=508, y=266, w=266, h=60, color=COL_BTN, value=24)
    w["status"] = label(" ", 12, 346, COL_WARN)
    ui.poll()
    return w


def draw(w, ep, peak, blink, nb):
    # peak -1 = no new result; nb = bursts in the last T3_MS
    s, p = ep.st, max(peak, 0)
    w["hb"].value(1 if blink and peak >= 0 else 0)  # heartbeat
    for k in ("cl", "cr"):  # takeover until ACK
        put(w[k], None, COL_ALARM_BG if s in (UNACK, ENDED) else COL_CARD)
    put(w["disc"], None, (COL_DIM, COL_WARN, COL_BAD if blink else COL_ALARM_BG,
                          COL_BAD, COL_DIM if blink else COL_ALARM_BG)[s])  # UNACK, ENDED flash at 1 Hz
    put(w["state"], ("score %d%%" % p, "! hearing %d%% - bursts %d" % (p, nb), "!! ALARM SOUND - tap ACK",
                     "ACKED - still hearing", "ENDED %s - tap ACK" % ep.end)[s],
        (COL_DIM, COL_WARN)[s] if s < 2 else COL_TEXT)
    put(w["verdict"], ("Quiet", "Hearing", "ALARM SOUND", "ALARM SOUND", "Quiet")[s],
        (COL_DIM, COL_WARN, COL_BAD, COL_BAD, COL_DIM)[s])
    put(w["ack"], None, COL_BAD if s in (UNACK, ENDED) else COL_BTN)
    meter(w, p)


# ---- 5) Main program ----
def run(w, i, labels, n0):
    th = read_th(i)
    put(w["legend"], "Hearing at %d%% | Confirm at %d%%" % (th[0][1], th[1][1]))
    b5, b6 = Button(0), Button(1)
    vote, al = Vote(K, N), Episode(True)
    log, bursts, paused, blink, peak = [], [], False, False, -1
    was, off, sh = False, None, None  # in a burst; burst ended at; shelved at
    start(w, i)
    drawn = polled = time.ticks_ms()
    while True:
        b5.sample()
        b6.sample()
        ack = b6.pressed_now()
        for e in ui.poll() or ():
            h = e["handle"] if e["type"] == "clicked" else -1
            ack = ack or h == w["ack"].id()
            if h == w["snz"].id():
                sh = time.ticks_ms()  # shelve
        if b5.pressed_now():
            paused = not paused
            vote, was = Vote(K, N), False
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
                on = vote.hits(cf)  # in a beep burst
                if on and not was and (off is None or time.ticks_diff(now, off) >= GAP_MS):
                    bursts.append(now)  # a shorter dip is the same burst
                elif was and not on:
                    off = now
                was = on
                bursts = [t for t in bursts if time.ticks_diff(now, t) <= T3_MS]
                # alarm = 2 bursts in T3_MS; over after T3_MS without one
                if al.step(p >= en, len(bursts) >= 2, not bursts and vote.clear(ex), now):
                    log = ["%s %s %d%% #%d" % (clock(), labels[e], max(vote.h), al.n)] + log[:3]
                    print(log[0])
                    for j, s in enumerate(log):  # newest first
                        put(w["lines"][j], s)
                    put(w["count"], str(al.n))
        if ack or sh is not None:
            al.ack()  # shelved: counted, never flashes
        if time.ticks_diff(now, drawn) >= DRAW_MS:
            drawn, blink = now, not blink
            left = 0 if sh is None else SNOOZE_S - time.ticks_diff(now, sh) // 1000
            if left <= 0 and sh is not None:
                sh = None
                if al.st == ACKED:
                    al.st = UNACK  # still sounding when Snooze ends: announce it again (red team 19:25)
            if not paused:
                put(w["status"], "SHELVED %d s - still counting" % left if left > 0 else HELP,
                    COL_WARN if left > 0 else COL_TEXT)
            draw(w, al, peak, blink, len(bursts))
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
