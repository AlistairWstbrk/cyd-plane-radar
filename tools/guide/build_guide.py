"""Build the Plane Radar user guide and the in-box quick-start card.

    python tools/guide/build_guide.py

Inputs
  sim/sim_out/*.png            device screenshots (run sim/plane_radar_sim.exe first)
  docs/guide/shots/web_*.png   web-page screenshots (tools/guide/capture.py)
  include/config.h             kGuideUrl: the address the QR codes point to

Outputs
  docs/guide.pdf               full guide (black and white, US Letter): contents with page numbers, bookmarks, index
  docs/quick-start.pdf         one-page card for the box (4.25 x 5.5 in)
  docs/quick-start-letter.pdf  the same card 4-up on US Letter, with cut marks

Needs Google Chrome (HTML to PDF), PyMuPDF (fitz), Pillow and segno.
"""
import base64
import html
import io
import re
import subprocess
import tempfile
from pathlib import Path

import fitz
import segno
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SIM = ROOT / "sim" / "sim_out"
SHOTS = ROOT / "docs" / "guide" / "shots"
DOCS = ROOT / "docs"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
GUIDE_URL = re.search(r'kGuideUrl\[\] = "([^"]+)"', CFG)[1]
VERSION = re.search(r'kFirmwareVersion\[\] = "([^"]+)"', CFG)[1]


# ------------------------------------------------------------------ assets ---

def dev(name):
    """Device screenshot, upscaled 2x for print. Returns a file URI."""
    SHOTS.mkdir(parents=True, exist_ok=True)
    out = SHOTS / f"dev_{name}_bw.png"
    src = SIM / f"{name}.png"
    if not out.exists() or out.stat().st_mtime < src.stat().st_mtime:
        im = Image.open(src).convert("L")
        im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(out, optimize=True)
    return out.as_uri()


def web(name, crop=None):
    """Web screenshot (optionally a vertical slice y0..y1 in source pixels)."""
    src = SHOTS / f"web_{name}.png"
    if not crop:
        out = SHOTS / f"web_{name}_bw.png"
        Image.open(src).convert("L").save(out, optimize=True)
        return out.as_uri()
    out = SHOTS / f"web_{name}_{crop[0]}_{crop[1]}_bw.png"
    im = Image.open(src).convert("L")
    im.crop((0, crop[0], im.width, min(crop[1], im.height))).save(out, optimize=True)
    return out.as_uri()


def qr_svg(url, scale=4):
    buf = io.BytesIO()
    segno.make(url, error="m").save(buf, kind="svg", scale=scale, border=2, dark="#000", light="#fff")
    return "data:image/svg+xml;base64," + base64.b64encode(buf.getvalue()).decode()


def esc(s):
    return html.escape(s, quote=True)


# ------------------------------------------------------------- page bits ---

CSS = """
@page { size: Letter; margin: 0.6in 0.7in 0.75in 0.7in; }
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font: 10.5pt/1.5 "Segoe UI", system-ui, sans-serif; color: #000; margin: 0; }
h1 { font-size: 22pt; line-height: 1.15; margin: 0 0 6pt; color: #000; break-after: avoid; }
h1 .n { color: #000; margin-right: 8pt; }
h2 { font-size: 13.5pt; margin: 18pt 0 4pt; color: #000; break-after: avoid; }
h3 { font-size: 11pt; margin: 12pt 0 2pt; break-after: avoid; }
p { margin: 0 0 7pt; }
ul, ol { margin: 0 0 8pt; padding-left: 18pt; }
li { margin: 2pt 0; }
a { color: inherit; text-decoration: none; }
b, strong { color: #000; }
.chapter { break-before: page; }
.lead { font-size: 11.5pt; color: #333; margin-bottom: 12pt; }
.k { display: inline-block; padding: 0 5pt; border: 1px solid #888; border-radius: 4pt; background: #f2f2f2;
     font-weight: 600; font-size: 9.5pt; line-height: 1.45; white-space: nowrap; }
.path { font-weight: 600; color: #000; white-space: nowrap; }
.note { border-left: 3pt solid #000; background: #f2f2f2; padding: 6pt 10pt; margin: 8pt 0 10pt; break-inside: avoid; }
.warn { border-left: 3pt solid #000; background: #e6e6e6; padding: 6pt 10pt; margin: 8pt 0 10pt; break-inside: avoid; }
.row { display: flex; gap: 16pt; align-items: flex-start; margin: 8pt 0 12pt; break-inside: avoid; }
.row > .txt { flex: 1; min-width: 0; }
figure { margin: 0; break-inside: avoid; }
figcaption { font-size: 8.5pt; color: #444; margin-top: 4pt; text-align: center; }
.dev { display: inline-block; background: #2a2a2a; padding: 7pt; border-radius: 9pt; box-shadow: 0 0 0 1pt #000; }
.dev img { display: block; width: 3.05in; border-radius: 2pt; }
.dev.sm img { width: 2.35in; }
.phone { display: inline-block; background: #2a2a2a; padding: 6pt 5pt; border-radius: 14pt; }
.phone img { display: block; width: 2.2in; border-radius: 8pt; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12pt 16pt; margin: 8pt 0 12pt; }
.grid2 figure { text-align: center; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 12pt; font-size: 9.5pt; break-inside: auto; }
th, td { text-align: left; vertical-align: top; padding: 4pt 6pt; border-bottom: 1px solid #bbb; }
th { background: #e6e6e6; font-weight: 700; color: #000; }
tr { break-inside: avoid; }
td:first-child { font-weight: 600; white-space: nowrap; }
.annot { position: relative; display: inline-block; }
.annot .pin { position: absolute; width: 15pt; height: 15pt; margin: -7.5pt 0 0 -7.5pt; border-radius: 50%;
  background: #fff; color: #000; font: 700 8.5pt/15pt "Segoe UI", sans-serif; text-align: center;
  box-shadow: 0 0 0 1.5pt #000; }
.legend { list-style: none; padding: 0; margin: 0; }
.legend li { display: flex; gap: 7pt; margin: 0 0 5pt; }
.legend .pin { flex: none; width: 15pt; height: 15pt; border-radius: 50%; background: #fff; color: #000;
  font: 700 8.5pt/15pt "Segoe UI", sans-serif; text-align: center; box-shadow: 0 0 0 1pt #000; margin-top: 1pt; }
/* cover */
.cover { height: 9.4in; display: flex; flex-direction: column; justify-content: space-between; }
.cover .t { font-size: 34pt; font-weight: 800; color: #000; margin: 0.5in 0 0; line-height: 1.05; }
.cover .s { font-size: 14pt; color: #333; margin-top: 8pt; }
.cover .hero { text-align: center; }
.cover .hero .dev img { width: 5.2in; }
.cover .foot { display: flex; justify-content: space-between; align-items: flex-end; font-size: 9.5pt; color: #444; }
.cover .foot img { width: 1.05in; display: block; }
/* contents + index */
.toc { break-before: page; }
.toc ol { list-style: none; padding: 0; margin: 0; }
.toc li { display: flex; align-items: baseline; margin: 0; }
.toc li.c { font-weight: 700; margin-top: 5pt; font-size: 10.5pt; }
.toc li.s { padding-left: 18pt; font-size: 9.5pt; line-height: 1.38; color: #222; }
.toc li .dots { flex: 1; border-bottom: 1px dotted #777; margin: 0 5pt; transform: translateY(-3pt); }
.toc li .pg { min-width: 16pt; text-align: right; font-variant-numeric: tabular-nums; }
.index { break-before: page; }
.index .cols { column-count: 2; column-gap: 24pt; font-size: 9.5pt; }
.index .letter { font-weight: 800; color: #000; margin: 8pt 0 2pt; break-after: avoid; }
.index .e { display: flex; break-inside: avoid; }
.index .e .dots { flex: 1; border-bottom: 1px dotted #999; margin: 0 4pt; transform: translateY(-3pt); }
"""

TOC = []      # (level, id, title)
INDEX = []    # (term, id)


def chapter(cid, num, title, lead=""):
    TOC.append((1, cid, f"{num}. {title}"))
    s = f'<section class="chapter"><h1 id="{cid}"><span class="n">{num}</span>{esc(title)}</h1>'
    if lead:
        s += f'<p class="lead">{lead}</p>'
    return s


def sec(sid, title):
    TOC.append((2, sid, title))
    return f'<h2 id="{sid}">{esc(title)}</h2>'


def ix(term, sid):
    INDEX.append((term, sid))
    return ""


def devfig(name, cap, cls=""):
    return (f'<figure><div class="dev {cls}"><img src="{dev(name)}"></div>'
            f'<figcaption>{cap}</figcaption></figure>')


def phonefig(src, cap):
    return f'<figure><div class="phone"><img src="{src}"></div><figcaption>{cap}</figcaption></figure>'


def row(fig, text):
    return f'<div class="row">{fig}<div class="txt">{text}</div></div>'


def annot(name, pins, width_in=4.2):
    """Screenshot with numbered pins at (x, y) in 320x240 screen pixels."""
    p = "".join(f'<span class="pin" style="left:{x / 3.2:.2f}%;top:{y / 2.4:.2f}%">{n}</span>' for n, x, y in pins)
    return (f'<div class="dev"><div class="annot"><img src="{dev(name)}" style="width:{width_in}in">{p}</div></div>')


def legend(items):
    return '<ul class="legend">' + "".join(f'<li><span class="pin">{n}</span><span>{t}</span></li>' for n, t in items) + "</ul>"


def table(head, rows):
    h = "".join(f"<th>{c}</th>" for c in head)
    b = "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in rows)
    return f"<table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table>"


def K(s):
    return f'<span class="k">{esc(s)}</span>'


def P(s):
    return f'<span class="path">{s}</span>'


# ------------------------------------------------------------------ content ---

def body():
    out = []
    A = out.append

    # 1 ---------------------------------------------------------------------
    A(chapter("welcome", 1, "Welcome",
              "Plane Radar shows the aircraft flying around you on a 2.8-inch touchscreen, live, like an "
              "air-traffic radar scope. It needs only power and your Wi-Fi."))
    A(ix("Overview", "welcome"))
    A(row(devfig("20_radar_classic", "The radar screen"),
          "<p>Each aircraft appears as an arrow pointing where it is heading, with a tag showing its "
          "callsign, aircraft type and altitude. Your home airport is at the centre and its runways are "
          "drawn to scale. Aircraft positions update every few seconds and move smoothly in between.</p>"
          "<p>Tap any aircraft to see its details, including when it will pass closest to you and at what "
          "height. Everything can be set up on the touchscreen or from your phone.</p>"))
    A(sec("need", "What you need"))
    A(ix("Power supply", "need"))
    A("<ul><li>A USB power supply (5 V, 1 A or more; a phone charger is fine) and a cable that fits the "
      "port on the back of the radar.</li>"
      "<li>A 2.4 GHz Wi-Fi network with internet access, and its password. (5 GHz-only networks will not "
      "work.)</li>"
      "<li>A phone, tablet or computer for the first setup.</li></ul>")
    A(ix("Wi-Fi requirements", "need"))
    A(sec("data", "Where the data comes from"))
    A("<p>Aircraft positions come from <b>adsb.fi</b>, a free community network of ADS-B receivers. "
      "Coverage depends on volunteers near you: busy areas are covered well, remote ones may show "
      "fewer aircraft. Military and some private aircraft may not appear. Airport and runway data comes "
      "from <b>OurAirports</b>.</p>")
    A('<div class="warn">Plane Radar is for fun and interest only. Do not use it for navigation or '
      "flight safety.</div>")
    A(ix("Data source (adsb.fi)", "data"))
    A(ix("Coverage", "data"))

    # 2 ---------------------------------------------------------------------
    A(chapter("start", 2, "Getting started",
              "First-time setup takes about five minutes: calibrate the touchscreen, connect it to Wi-Fi "
              "and choose your home airport."))
    A(sec("calib", "Power on and calibrate the touchscreen"))
    A(ix("Touch calibration", "calib"))
    A(ix("Calibration", "calib"))
    A("<p>Plug in the USB cable. The Plane Radar logo appears, then on the very first start the screen asks "
      "you to <b>tap the tip of four arrows</b>, one in each corner. Use a fingertip or a stylus and tap "
      "precisely on each tip. This is done once and remembered.</p>")
    A('<div class="note">To redo calibration later, open '
      f'{P("Settings &rsaquo; System &rsaquo; Touch")}, or keep a finger on the screen while the radar '
      "starts up.</div>")
    A(sec("wifi", "Connect to Wi-Fi and choose your home airport"))
    A(ix("Wi-Fi setup", "wifi"))
    A(ix("Setup screen (yellow)", "wifi"))
    A(ix("Home airport: first setup", "wifi"))
    A(ix("QR codes", "wifi"))
    A(row(devfig("03_setup_portal", "The yellow setup screen"),
          "<p>When the radar has no Wi-Fi saved, it shows the yellow setup screen and creates its own "
          "Wi-Fi network called <b>PlaneRadar-Setup</b>.</p><ol>"
          "<li>Scan the left QR code (<b>1. JOIN WI-FI</b>) with your phone camera and join the network. "
          "Or join <b>PlaneRadar-Setup</b> by hand, using the <b>Password</b> printed under it.</li>"
          "<li>The setup page usually opens by itself. If not, scan the right QR code (<b>2. OPEN PAGE</b>) "
          "or open <b>http://192.168.4.1</b> in a browser.</li>"
          "<li>Tap <b>Wi-Fi &amp; home airport</b>.</li></ol>"))
    A(row(phonefig(web("wifi_pick"), "Setup page on a phone"),
          "<ol start='4'><li>Tap your home network in the list and type its password.</li>"
          "<li>Under <b>Home airport</b>, type the name, city or code of the airport nearest you, for "
          "example <i>Tampa</i>, <i>KTPA</i> or <i>TPA</i>. Tap it in the list. The radar is centred on "
          "this airport.</li>"
          "<li>Choose your distance units and tap <b>Save</b>.</li></ol>"
          "<p>The radar joins your Wi-Fi and the setup network disappears. Your phone returns to its usual "
          "network by itself.</p>"
          '<div class="note">Not near an airport? Pick the closest one now. Later you can move the centre '
          "to any airport, or to a private strip by coordinates (see chapter 8).</div>"))
    A(sec("login", "Your password and web login"))
    A(ix("Password", "login"))
    A(ix("Web login", "login"))
    A(ix("Device code", "login"))
    A(ix("admin (user name)", "login"))
    A(row(devfig("60_info", "Device info shows the web login"),
          "<p>Every radar has its own random 8-character code. It is used twice:</p><ul>"
          "<li>as the password of the <b>PlaneRadar-Setup</b> Wi-Fi, and</li>"
          "<li>as the password of the web pages once the radar is on your network. The user name is "
          "<b>admin</b>.</li></ul>"
          f"<p>You can always find it on the radar under {P('Settings &rsaquo; Airports &amp; night &rsaquo; Device info')} "
          "(row <b>WEB LOGIN</b>). It stays the same after Wi-Fi resets and updates.</p>"))
    A(sec("connecting", "First connection"))
    A(ix("Connecting screen", "connecting"))
    A(row(devfig("02_connecting", "Connecting to Wi-Fi", "sm"),
          "<p>On every start the radar shows <i>Connecting to Wi-Fi</i> with the network name and a timer. "
          "It normally connects in a few seconds and switches to the radar. The first aircraft appear "
          "within about 10 seconds.</p><p>If the saved network cannot be reached after three attempts, "
          "the yellow setup screen opens so you can choose another network.</p>"))

    # 3 ---------------------------------------------------------------------
    A(chapter("radar", 3, "The radar screen",
              "The left part of the screen is the radar scope. The right panel shows the time, the data "
              "status, one aircraft in detail and the buttons."))
    A(sec("layout", "Screen layout"))
    A(ix("Radar screen layout", "layout"))
    A(ix("Range rings", "layout"))
    A(ix("Rim dots", "layout"))
    A(ix("Speed vector", "layout"))
    A(ix("Trails", "layout"))
    A('<div class="row">' + annot("20_radar_classic", [
        (1, 120, 120), (2, 92, 70), (3, 205, 124), (4, 155, 158), (5, 100, 222),
        (6, 280, 16), (7, 252, 37), (8, 282, 118), (9, 260, 197), (10, 300, 197),
        (11, 260, 234), (12, 300, 234)], 4.0) + '<div class="txt">' + legend([
        (1, "<b>Centre</b>: your home airport. Its runways are drawn to scale."),
        (2, "<b>Aircraft</b>: the arrow points along its track; the line ahead shows speed and direction (longer is faster); the fading line behind is its trail."),
        (3, "<b>Range</b>: distance of the 3rd ring."),
        (4, "<b>Alert ring</b> (dashed red): aircraft inside it trigger a proximity alert, if turned on."),
        (5, "<b>Rim dots</b>: aircraft just outside the outer ring."),
        (6, "<b>Clock</b> (12 or 24 hour)."),
        (7, "<b>Data status</b> and the number of aircraft received."),
        (8, "<b>Aircraft card</b>: the nearest aircraft, or the one you selected. Tap it for details."),
        (9, "<b>Zoom out</b>."), (10, "<b>Zoom in</b>."),
        (11, "<b>Traffic list</b>."), (12, "<b>Settings</b>."),
    ]) + "</div></div>")
    A("<p>Compass letters <b>N E S W</b> mark north at the top (the radar is always north-up). Airport codes "
      "around the scope mark other airports in range.</p>")
    A(sec("select", "Selecting an aircraft"))
    A(ix("Select an aircraft", "select"))
    A(ix("Deselect", "select"))
    A(row(devfig("21_radar_selected", "DLH1074 selected"),
          "<p><b>Tap an aircraft</b> on the scope to select it. It gets a yellow highlight, its full tag and a "
          "longer speed line, and the card on the right shows it with the heading <b>SELECTED</b>.</p>"
          "<p>The card shows altitude (ALT), vertical speed (V/S), ground speed (SPD), track (TRK), distance "
          "(DST) and transponder code (SQK).</p>"
          "<p><b>Tap empty space</b> on the scope to deselect. The card then goes back to the nearest "
          "aircraft (<b>NEAREST</b>).</p>"))
    A(sec("detail", "Aircraft details and the overhead prediction"))
    A(ix("Aircraft details", "detail"))
    A(ix("Closest approach", "detail"))
    A(ix("Overhead prediction", "detail"))
    A(row(devfig("29_detail_inbound", "Aircraft details"),
          "<p><b>Tap the aircraft card</b> (marked with <b>&gt;</b>) to open the details page: callsign, type, "
          "registration, size, altitude, vertical speed, ground speed, track, distance, bearing, squawk and "
          "how old the last position is. A small plot shows where it is relative to you.</p>"
          "<p>The <b>CLOSEST APPROACH</b> line predicts when the aircraft will pass nearest to you, for "
          "example <i>Overhead in 1:04 at 7500ft</i>, or how far to one side it will pass. "
          "The page updates every second. Tap the back arrow to return.</p>"))
    A(sec("labels", "Labels, zoom and range"))
    A(ix("Labels", "labels"))
    A(ix("Zoom", "labels"))
    A(ix("Range", "labels"))
    A(ix("Long-press", "labels"))
    A(row(devfig("26_labels_callsign", "Labels set to callsign only", "sm"),
          f"<ul><li>{K('-')} and {K('+')} zoom out and in. Ranges: 2, 5, 10, 15, 25, 40 and 60 km "
          "(shown in your chosen units).</li>"
          "<li><b>Press and hold</b> on the scope to switch labels between full (callsign, type, altitude), "
          "callsign only, and off.</li>"
          "<li>A short press of the <b>BOOT</b> button also steps through the ranges.</li></ul>"
          "<p>When many aircraft are close together, labels that would overlap are hidden automatically. "
          "Tap an aircraft to see its label.</p>"))
    A(sec("status", "Data status and messages"))
    A(ix("Status: LIVE, WAIT, STALE, OFFLINE", "status"))
    A(ix("NO WI-FI / FEED DOWN", "status"))
    A(table(["Status", "Meaning"], [
        ["LIVE (green)", "Data is arriving normally."],
        ["WAIT (amber)", "Connected, waiting for the first data after starting."],
        ["STALE (amber)", "No new data for 30 seconds. Positions are estimated until it recovers."],
        ["OFFLINE (red)", "No Wi-Fi. The radar reconnects by itself every 15 seconds."],
        ["SIM (blue)", "Demo traffic is on (see Settings &rsaquo; Demo traffic)."],
    ]))
    A(row(devfig("27_radar_offline_empty", "No Wi-Fi", "sm"),
          "<p>When there are no aircraft to show, the card says why:</p><ul>"
          "<li><b>NO TRAFFIC</b>: everything works, the sky is just quiet within the range shown.</li>"
          "<li><b>NO WI-FI</b>: reconnecting to your network.</li>"
          "<li><b>WAITING</b>: connected, first data on its way.</li>"
          "<li><b>FEED DOWN</b>: the data service is not answering; it retries by itself.</li></ul>"))
    A(sec("alerts", "Alerts, emergencies and the LED"))
    A(ix("Proximity alert", "alerts"))
    A(ix("Emergency squawk", "alerts"))
    A(ix("LED", "alerts"))
    A("<ul><li><b>Proximity alert</b>: set an alert radius under "
      f"{P('Settings &rsaquo; Units &amp; alerts &rsaquo; Alert radius')} (2 to 20 km). A dashed red ring "
      "shows it; an aircraft inside it (not on the ground) turns the card red with <b>PROXIMITY</b> and "
      "flashes the LED red.</li>"
      "<li><b>Emergencies</b>: aircraft squawking 7500, 7600 or 7700 are highlighted and the card shows "
      "<b>EMERGENCY</b>.</li>"
      "<li><b>Status LED</b> (on the back): a short green blink on each data update, amber every 3 seconds "
      "when offline. Both LED uses can be turned off.</li></ul>")

    # 4 ---------------------------------------------------------------------
    A(chapter("list", 4, "Traffic list", "Every aircraft received, in a sortable table."))
    A(ix("Traffic list", "list"))
    A(ix("Sort", "list"))
    A(row(devfig("30_list", "Traffic list sorted by distance"),
          f"<p>Tap the <b>list</b> button (bottom left of the panel) to open it. Each row shows callsign, "
          "type, altitude, speed and distance; the coloured bar on the left matches the aircraft's colour "
          "on the radar.</p><ul>"
          f"<li>Tap the button at the top right ({K('by DIST')}) to sort by distance, altitude or callsign.</li>"
          "<li>Drag up and down to scroll.</li>"
          "<li>Tap a row to select that aircraft and jump back to the radar.</li></ul>"))

    # 5 ---------------------------------------------------------------------
    A(chapter("airports", 5, "Airports",
              "About 1,200 major airports are drawn automatically. You can find any of 6,000 airports by "
              "name, add their runways, and move the radar centre to one."))
    A(sec("search", "Finding an airport on the radar"))
    A(ix("Find airport", "search"))
    A(ix("Search (on the device)", "search"))
    A(ix("Keyboard", "search"))
    A(ix("Nearby airports", "search"))
    A('<div class="grid2">' + devfig("50b_search_nearby", "Nothing typed: nearest airports", "sm")
      + devfig("56_search_tampa", "Typing TAMPA", "sm") + "</div>")
    A(f"<p>Open {P('Settings &rsaquo; Airports &amp; night &rsaquo; Find airport')}, or "
      f"{P('Airports &rsaquo; FIND')}. With nothing typed, the list shows the airports nearest the radar. "
      "Type a name, city, ICAO code (KTPA) or IATA code (TPA); results update as you type. Several words "
      "narrow the search (ST PETE, NEW YORK).</p>"
      f"<ul><li>{K('DEL')} deletes a letter; {K('SPACE')} adds a space.</li>"
      f"<li>{K('LIST')} hides the keyboard to show more results.</li>"
      "<li>Tap a result to open it.</li></ul>")
    A(sec("home", "Save an airport or make it home"))
    A(ix("Home airport: change on the device", "home"))
    A(ix("HOME button", "home"))
    A(ix("Runways", "home"))
    A('<div class="grid2">' + devfig("57_lookup_ktpa", "Tampa Intl with its runways", "sm")
      + devfig("58_radar_tampa", "Radar moved to KTPA", "sm") + "</div>")
    A("<p>The airport page downloads the runways (a few seconds) and shows a preview. Then:</p><ul>"
      f"<li>{K('SAVE')} adds the airport and its runways to the radar.</li>"
      f"<li>{K('HOME')} saves it <b>and</b> moves the radar centre there. This works offline too, just "
      "without runways.</li>"
      f"<li>{K('BACK')} returns to the search.</li></ul>")
    A(sec("lookup", "Small airfields: look up online"))
    A(ix("Look up online", "lookup"))
    A(ix("Airfields and strips", "lookup"))
    A(row(devfig("59_search_online_row", "A code not in the built-in list", "sm"),
          "<p>Small fields and strips are not in the built-in list. Type their code (for example 00AK) and "
          "tap <b>Look up online</b> at the bottom of the results. The radar finds them in the full "
          "OurAirports database.</p>"))
    A(sec("mylist", "Your airports"))
    A(ix("Your airports (list)", "mylist"))
    A(ix("Delete an airport", "mylist"))
    A(row(devfig("50_airports_empty", "Saved airports", "sm"),
          f"<p>{P('Settings &rsaquo; Airports &amp; night &rsaquo; Airports')} lists the airports you saved "
          f"(up to 24). They are drawn in violet. {K('HOME')} centres the radar on one; {K('DEL')} removes it "
          f"after you confirm. {K('FIND')} opens the search.</p>"))

    # 6 ---------------------------------------------------------------------
    A(chapter("settings", 6, "Settings on the radar",
              "Tap the <b>sliders</b> button. Settings has four pages; swipe left or right, or use the "
              "&lsaquo; &rsaquo; buttons. Tap a tile to step through its choices; changes apply at once and "
              "are saved."))
    A(ix("Settings (on the device)", "settings"))
    A(ix("Menu", "settings"))
    A(sec("set-display", "Page 1: Display"))
    A(ix("Theme", "set-display"))
    A(ix("Brightness", "set-display"))
    A(ix("Sweep animation", "set-display"))
    A(ix("Colour by altitude", "set-display"))
    A(ix("Ground aircraft", "set-display"))
    A(row(devfig("40_menu_display", "Display", "sm"), table(["Tile", "What it does"], [
        ["Range", "Radar range: 2 to 60 km at the 3rd ring."],
        ["Labels", "Full, callsign only, or off."],
        ["Trails", "Fading line behind each aircraft."],
        ["Runways", "Draw airport runways."],
        ["Ground A/C", "Show aircraft that are on the ground."],
        ["Alt colours", "Colour aircraft by altitude."],
        ["Sweep", "Rotating radar sweep animation."],
        ["Theme", "Classic, Amber, Ice or Night (red)."],
        ["Brightness", "20 to 100 %."]])))
    A(sec("set-units", "Page 2: Units & alerts"))
    A(ix("Units", "set-units"))
    A(ix("Rotate 180", "set-units"))
    A(ix("Invert colours", "set-units"))
    A(ix("Clock 12/24 h", "set-units"))
    A(row(devfig("41_menu_units_alerts", "Units & alerts", "sm"), table(["Tile", "What it does"], [
        ["Distance", "km, miles or nautical miles."],
        ["Altitude", "Feet or metres."],
        ["Speed", "Knots, km/h or mph."],
        ["Alert radius", "Off, 2, 3, 5, 10 or 20 km proximity alert."],
        ["Alert LED", "Flash the LED red during an alert."],
        ["Status LED", "Blink on each data update."],
        ["Clock", "12 or 24 hour."],
        ["Rotate 180", "Turn the picture upside down (for cable-up mounting)."],
        ["Invert colours", "Fix boards whose colours look like a photo negative."]])))
    A(sec("set-night", "Page 3: Airports & night"))
    A(ix("Night mode", "set-night"))
    A(ix("Maximum altitude filter", "set-night"))
    A(ix("Refresh interval", "set-night"))
    A(ix("Demo traffic", "set-night"))
    A(ix("Device info", "set-night"))
    A(row(devfig("42_menu_airports_night", "Airports & night", "sm"), table(["Tile", "What it does"], [
        ["Find airport", "Search for an airport (chapter 5)."],
        ["Airports", "Your saved airports."],
        ["Night mode", "Off, or a schedule: 21-07, 22-07, 23-06, 22-06 or 00-06."],
        ["Night dim", "Brightness during night mode: 5 to 40 %."],
        ["Night colours", "Switch to the red theme at night, or keep your theme."],
        ["Max altitude", "Hide aircraft above 5,000 to 30,000 ft, or no limit."],
        ["Refresh", "How often data is fetched: 3 to 30 s."],
        ["Demo traffic", "Simulated aircraft (10, 40 or 80) for showing the radar off-line."],
        ["Device info", "Web address, QR code, Wi-Fi, data and the web login."]])))
    A(sec("set-system", "Page 4: System"))
    A(ix("User guide QR code", "set-system"))
    A(ix("Restart", "set-system"))
    A(row(devfig("44_menu_system", "System", "sm"), table(["Tile", "What it does"], [
        ["User guide", "QR code that opens this guide on your phone, plus quick tips."],
        ["Touch", "Calibrate the touchscreen again."],
        ["Restart", "Restart the radar (asks first)."],
        ["Reset Wi-Fi", "Clear Wi-Fi, location and settings and open the setup screen (asks first)."]])))
    A(row(devfig("62_guide", "User guide screen", "sm"),
          "<p>The <b>User guide</b> screen shows a QR code to this document and the most useful touch "
          "controls, so help is always one scan away.</p>"))

    # 7 ---------------------------------------------------------------------
    A(chapter("themes", 7, "Themes and night mode"))
    A(ix("Themes", "themes"))
    A('<div class="grid2">'
      + devfig("20_radar_classic", "Classic", "sm") + devfig("22_theme_amber", "Amber", "sm")
      + devfig("23_theme_ice_altcolours_sweep", "Ice, with altitude colours and sweep", "sm")
      + devfig("24_theme_night", "Night (red)", "sm") + "</div>")
    A(sec("night", "Night mode"))
    A(ix("Night mode", "night"))
    A(ix("Dimming at night", "night"))
    A(row(devfig("30_night_mode", "Night mode active", "sm"),
          "<p>Night mode dims the screen and (optionally) switches to the red theme during the hours you "
          f"choose, then returns to normal in the morning. Set it under {P('Settings &rsaquo; Airports &amp; night')}, "
          "or with exact hours and your time zone on the web Radar page.</p>"
          '<div class="note">Night mode needs the correct time zone. Set it on the web Radar page '
          "(Screen &amp; clock &rsaquo; Time zone).</div>"))
    A(ix("Time zone", "night"))

    # 8 ---------------------------------------------------------------------
    A(chapter("web", 8, "Web pages",
              "From any phone or computer on your Wi-Fi you can change every setting, manage airports and "
              "watch live traffic."))
    A(sec("webopen", "Opening the web pages"))
    A(ix("Web pages: open", "webopen"))
    A(ix("plane-radar.local", "webopen"))
    A(ix("IP address", "webopen"))
    A("<ol><li>On the radar open " + P("Settings &rsaquo; Airports &amp; night &rsaquo; Device info") + ".</li>"
      "<li>Scan the QR code, or type the <b>WEB</b> address shown (for example http://192.168.1.57) into a "
      "browser. On most computers and iPhones <b>http://plane-radar.local</b> also works.</li>"
      "<li>Log in with user <b>admin</b> and the code shown on the same screen (row <b>WEB LOGIN</b>). "
      "You stay logged in while you use the pages.</li></ol>")
    A("<p>Every page has the same header and four tabs: <b>Home</b>, <b>Radar</b>, <b>Airports</b> and "
      "<b>Live</b>. The badge at the top right shows <b>Online</b> and your home airport.</p>")
    A(sec("webhome", "Home"))
    A(ix("Web: Home page", "webhome"))
    A(row(phonefig(web("home"), "Home"),
          "<p>Shortcuts to the other pages, and the device buttons:</p><ul>"
          "<li><b>Wi-Fi &amp; home airport</b>: change network (see 8.6).</li>"
          "<li><b>Device info</b>: technical details.</li>"
          "<li><b>Firmware update</b>: install new software (chapter 9).</li>"
          "<li><b>Restart</b>.</li></ul>"))
    A(sec("websettings", "Radar settings"))
    A(ix("Web: Radar settings", "websettings"))
    A(ix("Home airport: change on the web", "websettings"))
    A('<div class="grid2">' + phonefig(web("settings", (0, 2070)), "Home airport, Display, Units")
      + phonefig(web("settings", (2070, 4160)), "Alerts, Screen &amp; clock, Night mode") + "</div>")
    A("<p>All the touchscreen settings plus a few extra ones: exact night hours, time zone, hide aircraft "
      "above an altitude, and the data feed options. <b>Home airport</b> at the top changes as soon as you "
      "pick an airport. Other changes are applied with <b>Save changes</b> at the bottom.</p>")
    A(sec("webairports", "Airports"))
    A(ix("Web: Airports page", "webairports"))
    A(ix("Private strip", "webairports"))
    A(ix("Backup and restore", "webairports"))
    A(row(phonefig(web("airports_find"), "Finding an airport"),
          "<ul><li><b>Find an airport</b>: search, preview its runways, then <b>Add to radar</b> or "
          "<b>Add &amp; make home</b>.</li>"
          "<li><b>Your airports</b>: make one home, or delete it.</li>"
          "<li><b>Private strip (manual)</b>: add a strip that is not in any database, from its centre, "
          "heading and length or from its two runway ends.</li>"
          "<li><b>Backup and restore</b>: copy your airport list as text and paste it back later or into "
          "another radar.</li></ul>"))
    A(sec("weblive", "Live traffic"))
    A(ix("Web: Live traffic", "weblive"))
    A(row(phonefig(web("live"), "Live traffic"),
          "<p>A larger radar and a table of every aircraft the radar sees, updated every second. Emergency "
          "squawks are shown in red.</p>"))
    A(sec("webwifi", "Change Wi-Fi network"))
    A(ix("Change Wi-Fi network", "webwifi"))
    A("<p>Tap <b>Wi-Fi &amp; home airport</b> on the Home page, pick the new network, type its password and "
      "tap <b>Save</b>. If the radar is moving somewhere the old network is not available, it opens the "
      "yellow setup screen by itself; just follow chapter 2 again.</p>")

    # 9 ---------------------------------------------------------------------
    A(chapter("maint", 9, "Updates, restart and reset"))
    A(sec("update", "Firmware updates"))
    A(ix("Firmware update", "update"))
    A(ix("Update over Wi-Fi", "update"))
    A("<ol><li>Download the update file ending in <b>-update.bin</b> for your board.</li>"
      "<li>Open the web pages, then <b>Home &rsaquo; Firmware update</b>.</li>"
      "<li>Choose the file and tap <b>Update</b>. The radar shows <b>UPDATING</b>, then restarts by itself "
      "after about a minute.</li></ol>"
      "<p>Your Wi-Fi, settings, airports and login are kept. If the file is wrong or damaged it is rejected "
      "and the radar keeps its current software.</p>"
      '<div class="warn">Keep the radar powered during an update.</div>')
    A(sec("reset", "Restart and reset"))
    A(ix("BOOT button", "reset"))
    A(ix("Reset Wi-Fi", "reset"))
    A(ix("Factory reset", "reset"))
    A(row(devfig("61_confirm", "Reset asks for confirmation", "sm"),
          "<ul><li><b>Restart</b>: " + P("Settings &rsaquo; System &rsaquo; Restart") + ", the web Home page, "
          "or unplug and plug in again.</li>"
          "<li><b>Reset Wi-Fi</b>: " + P("Settings &rsaquo; System &rsaquo; Reset Wi-Fi") + ", or "
          "<b>hold the BOOT button for 3 seconds</b>. This clears the Wi-Fi, home location and settings and "
          "opens the setup screen. Touch calibration, your saved airports and the login code are kept.</li></ul>"
          "<p><b>BOOT</b> is the small button marked BOOT on the board (RST next to it just restarts). A short "
          "press of BOOT changes the range.</p>"))

    # 10 --------------------------------------------------------------------
    A(chapter("trouble", 10, "Troubleshooting"))
    A(ix("Troubleshooting", "trouble"))
    A(ix("Problems", "trouble"))
    A(table(["Problem", "What to do"], [
        ["My phone cannot find PlaneRadar-Setup",
         "The setup network only exists while the yellow screen is shown. Hold BOOT for 3 seconds to open it. "
         "Use the password printed under the left QR code."],
        ["The setup page does not open", "Open http://192.168.4.1 in a browser while joined to PlaneRadar-Setup. "
         "Turn off mobile data if your phone keeps leaving the setup network."],
        ["It will not join my Wi-Fi", "Check the password, and that the network is 2.4 GHz. Very weak signal: "
         "move the radar closer to the router."],
        ["Few or no aircraft", "Check the status reads LIVE. Increase the range. Coverage depends on adsb.fi "
         "receivers in your area; turn off Max altitude and turn on Ground A/C to see everything."],
        ["Wrong place on the map", "Set the home airport: Settings > Find airport > HOME, or on the web Radar page."],
        ["Taps land in the wrong place", "Settings > System > Touch, and tap the arrow tips precisely."],
        ["Colours look inverted", "Settings > Units & alerts > Invert colours."],
        ["Picture upside down", "Settings > Units & alerts > Rotate 180."],
        ["Night mode at the wrong time", "Set your time zone on the web Radar page (Screen & clock)."],
        ["Web page asks for a login", "User admin, password = the code under Settings > Device info (WEB LOGIN)."],
        ["Cannot open plane-radar.local", "Use the IP address shown under Device info instead (some Android phones "
         "do not support .local names)."],
        ["Screen blank or frozen", "Unplug for 5 seconds and plug in again. Try another USB power supply."],
    ]))

    # 11 --------------------------------------------------------------------
    A(chapter("quickref", 11, "Quick reference"))
    A(ix("Quick reference", "quickref"))
    A(ix("Controls", "quickref"))
    A(table(["Where", "Action", "Result"], [
        ["Radar", "Tap an aircraft", "Select it"],
        ["Radar", "Tap empty space", "Deselect"],
        ["Radar", "Press and hold", "Labels: full, callsign, off"],
        ["Panel", "Tap the aircraft card", "Details and closest approach"],
        ["Panel", "- / +", "Zoom out / in"],
        ["Panel", "List button", "Traffic list"],
        ["Panel", "Sliders button", "Settings (4 pages)"],
        ["Settings", "Swipe, or &lsaquo; &rsaquo;", "Next / previous page"],
        ["Any screen", "Back arrow (top left)", "Go back"],
        ["BOOT button", "Short press", "Next range"],
        ["BOOT button", "Hold 3 seconds", "Reset Wi-Fi and settings, open setup"],
    ]))
    A(table(["Address", "What"], [
        ["http://192.168.4.1", "Setup page (while joined to PlaneRadar-Setup)"],
        ["http://plane-radar.local", "Web pages on your network (or the IP under Device info)"],
        ["admin / your code", "Web login (code under Settings > Device info)"],
    ]))

    # 12 --------------------------------------------------------------------
    A(chapter("specs", 12, "Specifications and credits"))
    A(ix("Specifications", "specs"))
    A(ix("Credits", "specs"))
    A(table(["Item", "Detail"], [
        ["Display", "2.8 in colour touchscreen, 320 x 240"],
        ["Board", "ESP32-2432S028R (\"Cheap Yellow Display\")"],
        ["Power", "5 V USB"],
        ["Wi-Fi", "2.4 GHz 802.11 b/g/n, WPA2"],
        ["Ranges", "2, 5, 10, 15, 25, 40, 60 km"],
        ["Airports", "6,029 searchable offline; up to 24 of your own with runways"],
        ["Firmware", f"Plane Radar CYD edition v{esc(VERSION)}"],
    ]))
    A("<p>Based on <b>ESP32 Plane Radar</b> by MatixYo (MIT licence). Aircraft data: adsb.fi open data. "
      "Airport data: OurAirports (public domain). Fonts: Noto Sans (SIL Open Font Licence 1.1). "
      "Graphics: LovyanGFX. Wi-Fi setup: WiFiManager.</p>")
    return "".join(out)


# ---------------------------------------------------------------- assemble ---

def cover():
    return (f'<section class="cover"><div><div class="t">Plane Radar</div>'
            f'<div class="s">User guide &middot; CYD edition v{esc(VERSION)}</div></div>'
            f'<div class="hero"><div class="dev"><img src="{dev("21_radar_selected")}"></div></div>'
            f'<div class="foot"><div>Live aircraft around you on a 2.8-inch touchscreen.<br>'
            f'Latest version of this guide: scan the code.</div><img src="{qr_svg(GUIDE_URL)}"></div></section>')


def toc_html(pages):
    items = "".join(
        f'<li class="{"c" if lvl == 1 else "s"}"><a href="#{sid}">{esc(t)}</a><span class="dots"></span>'
        f'<span class="pg">{pages.get(sid, "00")}</span></li>' for lvl, sid, t in TOC)
    items += (f'<li class="c"><a href="#index">Index</a><span class="dots"></span>'
              f'<span class="pg">{pages.get("index", "00")}</span></li>')
    return f'<section class="toc"><h1>Contents</h1><ol>{items}</ol></section>'


def index_html(pages):
    seen = {}
    for term, sid in INDEX:
        seen.setdefault(term, sid)
    entries = sorted(seen.items(), key=lambda kv: kv[0].lower())
    out, letter = [], None
    for term, sid in entries:
        L = term[0].upper()
        if L != letter:
            letter = L
            out.append(f'<div class="letter">{esc(L)}</div>')
        out.append(f'<div class="e"><a href="#{sid}">{esc(term)}</a><span class="dots"></span>'
                   f'<span>{pages.get(sid, "00")}</span></div>')
    return f'<section class="index"><h1 id="index">Index</h1><div class="cols">{"".join(out)}</div></section>'


def render(html_text, pdf_path):
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "doc.html"
        src.write_text(html_text, encoding="utf-8")
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        "--generate-pdf-document-outline", f"--user-data-dir={Path(td) / 'p'}",
                        f"--print-to-pdf={pdf_path}", src.as_uri()],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180, check=True)


def anchor_pages(pdf_path):
    """Page (1-based) of every id, from the PDF's named destinations."""
    doc = fitz.open(pdf_path)
    names = doc.resolve_names()
    doc.close()
    return {k: v["page"] + 1 for k, v in names.items()}


def build_guide():
    TOC.clear()
    INDEX.clear()
    content = body()   # fills TOC / INDEX
    head = f"<!DOCTYPE html><html><head><meta charset='utf-8'><title>Plane Radar user guide</title><style>{CSS}</style></head><body>"
    tmp = DOCS / "guide.tmp.pdf"
    pages = {}
    for _ in range(3):   # page numbers settle after a pass or two
        html_text = head + cover() + toc_html(pages) + content + index_html(pages) + "</body></html>"
        render(html_text, tmp)
        found = anchor_pages(tmp)
        new = {sid: found[sid] for sid in [s for _, s, _ in TOC] + ["index"]}
        if new == pages:
            break
        pages = new
    doc = fitz.open(tmp)
    # Footer page numbers (not on the cover).
    for i, page in enumerate(doc):
        if i == 0:
            continue
        r = page.rect
        page.insert_text((r.width / 2 - 8, r.height - 26), str(i + 1), fontsize=9, fontname="helv",
                         color=(0.3, 0.3, 0.3))
        page.insert_text((50, r.height - 26), "Plane Radar user guide", fontsize=8, fontname="helv",
                         color=(0.5, 0.5, 0.5))
    # Bookmarks: contents, chapters, sections, index.
    bm = [[1, "Cover", 1], [1, "Contents", 2]]
    for lvl, sid, t in TOC:
        bm.append([lvl, t, pages[sid]])
    bm.append([1, "Index", pages["index"]])
    doc.set_toc(bm)
    doc.set_metadata({"title": "Plane Radar user guide", "author": "Plane Radar",
                      "subject": f"CYD edition v{VERSION}", "keywords": "Plane Radar, ADS-B, CYD, user guide"})
    out = DOCS / "guide.pdf"
    doc.save(out, garbage=4, deflate=True)
    doc.close()
    tmp.unlink()
    print(f"guide.pdf: {fitz.open(out).page_count} pages, {out.stat().st_size // 1024} KB")


# ------------------------------------------------------------ quick start ---

CARD_CSS = """
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; font: 7.9pt/1.33 "Segoe UI", system-ui, sans-serif; color: #000; }
.card { width: 4.25in; height: 5.5in; padding: 0.2in 0.22in 0.18in; display: flex; flex-direction: column; overflow: hidden; }
.top { display: flex; align-items: center; gap: 8pt; border-bottom: 2pt solid #000; padding-bottom: 5pt; }
.logo { width: 22pt; height: 22pt; border-radius: 50%; border: 2.5pt solid #000; position: relative; flex: none; }
.logo:after { content: ""; position: absolute; left: 50%; top: 50%; width: 3pt; height: 3pt; margin: -1.5pt; border-radius: 50%; background: #000; }
h1 { font-size: 15pt; margin: 0; line-height: 1; color: #000; }
.sub { font-size: 8pt; color: #444; margin-top: 2pt; }
h2 { font-size: 8.6pt; margin: 5pt 0 1.5pt; color: #000; text-transform: uppercase; letter-spacing: .4pt; }
ol { margin: 0; padding-left: 13pt; }
li { margin: 1pt 0; }
b { color: #000; }
.k { display: inline-block; padding: 0 3pt; border: .8pt solid #888; border-radius: 3pt; background: #f2f2f2; font-weight: 600; font-size: 7.6pt; line-height: 1.35; }
.shot { display: flex; gap: 7pt; align-items: center; margin-top: 4pt; }
.shot .dev { background: #2a2a2a; padding: 3pt; border-radius: 4pt; flex: none; }
.shot .dev img { width: 1.42in; display: block; }
.ctl { margin: 0; padding: 0; list-style: none; font-size: 7.4pt; }
.ctl li { margin: 0 0 1.5pt; }
.foot { margin-top: auto; display: flex; gap: 8pt; align-items: center; border-top: 1pt solid #bbb; padding-top: 5pt; }
.foot img { width: 0.85in; height: 0.85in; flex: none; }
.foot .t { font-size: 7.4pt; line-height: 1.3; }
.foot .t b { font-size: 9pt; }
.small { font-size: 7.2pt; color: #444; }
"""


def card_html():
    return f"""<div class="card">
<div class="top"><div class="logo"></div><div><h1>Plane Radar</h1><div class="sub">Quick start &middot; live aircraft around you</div></div></div>
<h2>1 &middot; Power</h2>
<p style="margin:0">Plug into any USB charger (5 V, 1 A). First start: <b>tap the tip of each arrow</b> to calibrate the screen.</p>
<h2>2 &middot; Wi-Fi and home airport</h2>
<ol>
<li>The screen turns yellow. Scan <b>1. JOIN WI-FI</b> with your phone, or join <b>PlaneRadar-Setup</b> with the <b>Password</b> shown under it.</li>
<li>The setup page opens (or scan <b>2. OPEN PAGE</b>). Tap <b>Wi-Fi &amp; home airport</b>.</li>
<li>Pick your network (2.4 GHz) and type its password. Type your nearest airport, e.g. <i>Tampa</i> or <i>KTPA</i>, tap it, then <b>Save</b>.</li>
</ol>
<h2>3 &middot; Use it</h2>
<div class="shot"><div class="dev"><img src="{dev('21_radar_selected')}"></div>
<ul class="ctl">
<li><b>Tap a plane</b>: select it</li>
<li><b>Tap the card</b>: details, overhead time</li>
<li><b>Hold</b> the radar: labels</li>
<li><span class="k">-</span> <span class="k">+</span> zoom</li>
<li><b>List</b> / <b>sliders</b>: traffic list / settings</li>
<li><b>BOOT</b> press: range &middot; hold 3 s: reset Wi-Fi</li>
</ul></div>
<p style="margin:5pt 0 0">Change settings from your phone: <b>Settings &rsaquo; Device info</b> shows the web address and login (user <b>admin</b>).</p>
<div class="foot"><img src="{qr_svg(GUIDE_URL, 3)}"><div class="t"><b>Full user guide (PDF)</b><br>Scan for every feature, settings and troubleshooting. Also on the radar: <b>Settings &rsaquo; System &rsaquo; User guide</b>.<br><span class="small">For fun only, not for navigation.</span></div></div>
</div>"""


def build_card():
    one = (f"<!DOCTYPE html><html><head><meta charset='utf-8'><title>Plane Radar quick start</title><style>"
           f"@page {{ size: 4.25in 5.5in; margin: 0; }}{CARD_CSS}</style></head><body>{card_html()}</body></html>")
    render(one, DOCS / "quick-start.pdf")
    d = fitz.open(DOCS / "quick-start.pdf")
    assert d.page_count == 1, f"quick start spills onto {d.page_count} pages"
    d.close()
    sheet = (f"<!DOCTYPE html><html><head><meta charset='utf-8'><title>Plane Radar quick start (4-up)</title><style>"
             f"@page {{ size: 8.5in 11in; margin: 0; }}{CARD_CSS}"
             ".sheet { display: grid; grid-template-columns: 4.25in 4.25in; grid-template-rows: 5.5in 5.5in; }"
             ".sheet .card { outline: .5pt dashed #777; outline-offset: -0.5pt; }"
             f"</style></head><body><div class='sheet'>{card_html() * 4}</div></body></html>")
    render(sheet, DOCS / "quick-start-letter.pdf")
    print("quick-start.pdf, quick-start-letter.pdf")


if __name__ == "__main__":
    print("QR ->", GUIDE_URL)
    build_guide()
    build_card()
