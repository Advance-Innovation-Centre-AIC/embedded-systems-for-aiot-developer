# 00_ai_model_lab.py - AI Model Lab: pick, run/stop, watch live results and tune thresholds
#                   of ANY Edge AI model on the board (ห้องทดลองโมเดล AI)
#
# How to use: scroll the roller to pick a model -> tap "Run" (green) -> it turns into "Stop" (red)
#             "AI says" follows the strongest event class (not unlabeled) against its Hearing at / Confirm at
#             SW5 (lower button) = next model, SW6 (upper button) = Run/Stop
# Everything is read from the board: model list, classes, sensor, thresholds.
# Boards: TESAIoT Dev Kit (firmware 2.4.2+), BENTO Emulator (built-in models only). Eva Kit: written for it, not yet tested on a board.
# No sound on purpose: the speaker would leak into the mic models.

try:
    import buttons                   # the Eva Kit has no buttons module
except ImportError:
    buttons = None
import edge_ai
import time
import ui

# ---- 1) Settings ----
POLL_MS = 100            # read a result every ... ms
DRAW_MS = 500            # redraw every ... ms (faster = flicker)
CONF_OK = 60             # confidence (%) we trust
MAX_CLASSES = 8          # the AI core reports at most 8 classes
VU_LEDS = 16             # LEDs in the confidence meter

COL_TEXT, COL_DIM, COL_CARD = 0xE8EAED, 0x9AA3AF, 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF


# ---- 2) Board buttons and the AI core ----
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


def list_models():
    # [(index, name, source, sensor, labels)] - never reads or prints the model id
    try:
        ms = edge_ai.models()
    except Exception as e:
        print("cannot read models:", e)
        return []
    out = []
    for m in ms:
        b, s = m.get("builtin"), m.get("sensor", 0)
        out.append((m["index"], m["name"], "built-in" if b else ("Store" if b is False else "?"),
                    ("IMU", "radar", "mic")[s] if 0 <= s < 3 else "?", m.get("labels") or []))
        print(out[-1])
    return out


def read_result():
    try:
        return edge_ai.result()
    except OSError:
        return None


def stop_ai():
    try:
        edge_ai.stop()
    except OSError:
        pass


def read_th(m):
    # (enter list, confirm list) for every class: enter = "Hearing at", confirm = "Confirm at"
    try:
        t = edge_ai.thresholds(m[0])
        return t["enter"], t["confirm"]
    except Exception:
        return None


def set_th(m, k, which, pct):
    # the board keeps hearing <= confirm, so read back what it really holds
    try:
        edge_ai.thresholds(m[0], k, which, pct)
    except Exception as e:
        print("cannot set threshold:", e)
    return read_th(m)


class Stats:
    def __init__(self):
        self.n, self.lat, self.t0 = 0, 0.0, time.ticks_ms()

    def add(self, r):
        self.n += 1
        self.lat += r.get("latency_ms", 0)

    def rate(self):
        s = time.ticks_diff(time.ticks_ms(), self.t0) / 1000
        return self.n / s if s > 0 else 0

    def report(self, m):
        if self.n:
            print("[%s] %d results | %.1f /s | %.2f ms" % (m[1], self.n, self.rate(), self.lat / self.n))


# ---- 3) Screen: built once; switching models only edits what differs (no black flash) ----
# Text sizes: 16+ (Thai 20+). One text must stay under 126 bytes.
def card(x, y, w, h, title):
    ui.Panel(x=x, y=y, w=w, h=h, color=COL_CARD, min=COL_DIM, max=12, value=1)
    ui.Label(title, x=x + 14, y=y + 8, color=COL_INFO, value=16)


def fill_roller(ro, models):
    ro.clear_items()
    for m in models:             # one name at a time: a long list in one text gets cut at 126 bytes
        ro.add_option(m[1])


def build_screen(models):
    ui.screen()
    time.sleep_ms(150)
    ui.Label("AI Model Lab", x=12, y=4, color=COL_TEXT, value=24)
    w = {"led": ui.Led(x=752, y=6, w=26, h=26, color=COL_OK, value=0)}
    w["ro"] = ui.Roller(models[0][1], x=12, y=40, w=210, h=232, color=COL_TEXT)
    fill_roller(w["ro"], models)
    w["ro"].prop(ui.PROP_VISIBLE_ROWS, 7)
    w["go"] = ui.Button("Run", x=12, y=282, w=210, h=58, color=COL_OK, value=24)
    card(234, 40, 282, 300, "AI says")
    w["hits"] = ui.Label(" ", x=320, y=48, color=COL_DIM, value=16)
    # status disc: a round Panel, because an Led keeps its creation hue on the board (only brightness changes)
    w["sig"] = ui.Panel(x=250, y=72, w=44, h=44, color=COL_DIM, min=COL_DIM, max=22, value=0)
    w["verdict"] = ui.Label("-", x=308, y=78, color=COL_TEXT, value=28)
    w["pct"] = ui.Label("-", x=250, y=122, color=COL_TEXT, value=16)
    w["vu"] = [ui.Led(x=250 + i * 16, y=146, w=12, h=24, value=0,
                      color=COL_OK if i < 10 else (COL_WARN if i < 14 else COL_BAD)) for i in range(VU_LEDS)]
    w["lit"] = 0
    ui.Label("Class", x=250, y=184, color=COL_INFO, value=16)
    w["dd"] = ui.Dropdown(x=310, y=174, w=190, h=38)
    w["en_l"] = ui.Label(" ", x=250, y=220, color=COL_TEXT, value=16)
    w["en"] = ui.Slider(x=262, y=246, w=226, h=10, min=0, max=100, value=0)
    w["cf_l"] = ui.Label(" ", x=250, y=264, color=COL_TEXT, value=16)
    w["cf"] = ui.Slider(x=262, y=290, w=226, h=10, min=0, max=100, value=0)
    w["info"] = ui.Label(" ", x=250, y=310, color=COL_DIM, value=16)
    card(528, 40, 260, 300, "Class scores")
    w["names"] = [ui.Label(" ", x=542, y=80, color=COL_TEXT, value=16) for i in range(MAX_CLASSES)]
    w["bars"] = [ui.Bar(x=542, y=100, w=232, h=8, min=0, max=100, value=0) for i in range(MAX_CLASSES)]
    w["status"] = ui.Label(" ", x=12, y=350, color=COL_WARN, value=16)
    ui.poll()
    return w


def apply_model(w, m):
    # one row per class (few classes = thicker bars); hide the spare rows
    n = max(1, min(len(m[4]), MAX_CLASSES))
    rh = min(62, 250 // n)
    for i in range(MAX_CLASSES):
        lb, b = w["names"][i], w["bars"][i]
        if i < n:
            lb.pos(542, 80 + i * rh)
            lb.text(m[4][i] if i < len(m[4]) else "-")
            b.pos(542, 102 + i * rh)
            b.size(232, 14 if rh >= 44 else 8)
            b.value(0)
            b.color(COL_DIM)
            lb.show()
            b.show()
        else:
            lb.hide()
            b.hide()
    w["dd"].clear_items()
    for lb in m[4][:MAX_CLASSES]:
        w["dd"].add_option(lb)
    w["info"].text("%s | %s | %d classes" % (m[2], m[3], len(m[4])))
    w["verdict"].text("-")
    w["verdict"].color(COL_TEXT)
    w["pct"].text("-")
    w["sig"].color(COL_DIM)
    meter(w, 0)


def meter(w, conf):
    # switch only the LEDs that change
    lit = (conf * VU_LEDS + 50) // 100
    for i in range(min(lit, w["lit"]), max(lit, w["lit"])):
        w["vu"][i].value(1 if i < lit else 0)
    w["lit"] = lit


def show_th(w, th, k):
    if th and k < len(th[0]):
        w["en_l"].text("Hearing at %d%%" % th[0][k])
        w["cf_l"].text("Confirm at %d%%" % th[1][k])
        w["en"].value(th[0][k])
        w["cf"].value(th[1][k])
    else:
        w["en_l"].text("No thresholds")


def tune_class(w, m):
    # start at class 1: class 0 is the resting class of almost every model
    k = 1 if len(m[4]) > 1 else 0
    w["dd"].value(k)
    th = read_th(m)
    show_th(w, th, k)
    return k, th, {}


def event_of(r, th):
    # the strongest EVENT class, not class 0 (unlabeled / idle / background), with its thresholds
    sc = r["scores"]
    e = max(range(1, len(sc)), key=lambda i: sc[i]) if len(sc) > 1 else 0
    en, cf = (th[0][e], th[1][e]) if th and e < len(th[0]) else (CONF_OK // 2, CONF_OK)
    return e, int(sc[e] * 100), en, cf


def clock():
    t = time.localtime()
    return "%02d:%02d:%02d" % t[3:6]


def show_result(w, r, labels, th):
    # grey = below Hearing at, amber = heard, green = confirmed
    e, p, en, cf = event_of(r, th)
    col, word = (COL_OK, "confirmed") if p >= cf else ((COL_WARN, "heard") if p >= en else (COL_DIM, "quiet"))
    w["sig"].color(col)
    w["verdict"].text(labels[e] if e < len(labels) else "-")
    w["verdict"].color(col if p >= en else COL_TEXT)
    w["pct"].text("%d%%  %s" % (p, word))
    meter(w, p)
    for i, b in enumerate(w["bars"]):
        if i < len(r["scores"]) and i < len(labels):
            p = int(r["scores"][i] * 100)
            b.value(p)
            b.color(COL_OK if i == r["top"] else COL_DIM)
            w["names"][i].text("%s  %d%%" % (labels[i], p))


def say(w, text, color):
    w["status"].color(color)
    w["status"].text(text)


# ---- 4) Main program ----
def run(w, models, cur):
    # re-check the model list first: a model may have been unloaded or deployed meanwhile
    try:
        if edge_ai.count() != len(models):
            return None
    except OSError:
        pass
    say(w, "Loading " + models[cur][1] + " ...", COL_WARN)
    ui.poll()
    try:
        edge_ai.select(models[cur][0])      # select() loads the model and starts it
        return True
    except OSError as e:
        print("select", models[cur][1], ":", e)
        return None


def main():
    models = list_models()
    if not models:
        ui.screen()
        ui.Label("No models on this board", x=12, y=12, color=COL_BAD, value=24)
        ui.poll()
        return
    cur, running = 0, False              # pick first, then tap Run
    w = build_screen(models)
    apply_model(w, models[cur])
    k, th, pend = tune_class(w, models[cur])
    nxt, go = Button(0), Button(1)
    st, last_seq, fresh, shown = Stats(), None, None, None
    hits, conf_on = 0, False
    drawn = polled = time.ticks_ms()
    try:
        while True:
            want = None
            for e in ui.poll() or ():
                h, t = e["handle"], e["type"]
                if h == w["ro"].id() and t == "value_changed":
                    want = ("pick", e["value"])
                elif h == w["go"].id() and t == "clicked":
                    want = "toggle"
                elif h == w["dd"].id() and t == "value_changed":
                    k = e["value"]
                    show_th(w, th, k)
                elif t == "value_changed" and h in (w["en"].id(), w["cf"].id()):
                    pend["enter" if h == w["en"].id() else "confirm"] = e["value"]   # sent at the next redraw
            for b in (nxt, go):
                b.sample()
            if nxt.pressed_now():
                want = ("pick", (cur + 1) % len(models))
            if go.pressed_now():
                want = "toggle"
            if want:
                was = running
                if was:
                    st.report(models[cur])
                    stop_ai()
                if isinstance(want, tuple):  # new model = stop the old one, wait for Run
                    cur, running = want[1], False
                    w["ro"].value(cur)
                    apply_model(w, models[cur])
                    k, th, pend = tune_class(w, models[cur])
                else:
                    running = not was
                if running and run(w, models, cur) is None:
                    models, cur, running = list_models() or models, 0, False   # list changed: reload it
                    fill_roller(w["ro"], models)
                    w["ro"].value(0)
                    apply_model(w, models[0])
                    k, th, pend = tune_class(w, models[0])
                    say(w, "Model list changed - pick again, then Run", COL_BAD)
                    shown = "x"
                w["go"].text("Stop" if running else "Run")
                w["go"].color(COL_BAD if running else COL_OK)
                w["led"].value(1 if running else 0)
                st, last_seq, fresh = Stats(), None, None
                hits, conf_on = 0, False
                w["hits"].text(" ")
            m = models[cur]
            now = time.ticks_ms()
            if running and time.ticks_diff(now, polled) >= POLL_MS:
                polled = now
                r = read_result()
                if r and r["label"] is not None and r["seq"] != last_seq and r.get("index", m[0]) == m[0]:
                    last_seq, fresh = r["seq"], r
                    st.add(r)
                    e, p, en, cf = event_of(r, th)
                    if p >= cf and not conf_on:      # count once per event, when it first reaches Confirm at
                        hits += 1
                        t = clock()
                        w["hits"].text("%dx | %s" % (hits, t))
                        print(t, m[4][e] if e < len(m[4]) else e, "%d%%" % p, "#%d" % hits)
                    conf_on = p >= cf
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                for which in pend:
                    th = set_th(m, k, which, pend[which])
                if pend:
                    pend = {}
                    show_th(w, th, k)
                if fresh:
                    show_result(w, fresh, m[4], th)
                    fresh = None
                if not running:
                    note = None if shown == "x" else "Pick a model on the roller, then tap Run"
                elif st.n:
                    note = "Running | %.1f results/s" % st.rate()
                else:
                    note = "Running - waiting for the first result..."
                if note and note != shown:
                    say(w, note, COL_OK if running else COL_WARN)
                    shown = note
                drawn = now
            time.sleep_ms(20)
    finally:                                 # stopping the program always releases the AI core
        if running:
            st.report(models[cur])
        stop_ai()


main()

# Try next: 1) keep the board still for 30 s on every model - the resting class should win.
# 2) lower "Confirm at" by 10 % steps - how much easier does the green light fire, and how many false alarms?
