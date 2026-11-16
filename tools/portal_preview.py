#!/usr/bin/env python3
"""Preview the device web portal on your PC with a mock API (no hardware).

Extracts the real HTML/CSS/JS from src/services/web_pages.h and serves it with
fake JSON endpoints, so the pages can be checked in any browser:

  python tools/portal_preview.py            -> http://localhost:8080/
"""
import json
import math
import random
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / "src" / "services" / "web_pages.h").read_text(encoding="utf-8")
P = {m.group(1): m.group(3) for m in re.finditer(r'static const char (k\w+)\[\] = R"(\w+)\((.*?)\)\2";', src, re.S)}

state = {
    "settings": {"lat": 52.3676, "lon": 4.9041, "range_idx": 2, "dist_units": 0, "alt_units": 0,
                 "speed_units": 0, "label_mode": 2, "theme": 0, "brightness": 80, "alert_km": 0,
                 "fetch_s": 5, "max_alt_kft": 0, "show_runways": True, "show_trails": True,
                 "show_ground": False, "alt_colors": False, "sweep": False,
                 "alert_led": True, "status_led": True, "clock_24h": True,
                 "flip": False, "invert": False, "tz": "UTC0", "night_mode": False,
                 "night_start_h": 22, "night_end_h": 7, "night_brightness": 10, "night_theme": True},
    "airports": [],
    "lookup": {"state": "idle", "progress": 0, "message": "", "ident": ""},
    "sim": 12,
}
# Built-in search index, parsed from the generated C++ table.
INDEX = []
for m in re.finditer(r'\{"([\w-]+)", "(\w*)", (\d+), (-?\d+), (-?\d+), "(.*?)" "\\0" "(.*?)" "\\0" "(.*?)"\}',
                     (ROOT / "src" / "data" / "airport_index_data.cpp").read_text(encoding="utf-8")):
    INDEX.append({"ident": m[1], "iata": m[2], "lat": int(m[4]) / 1e5, "lon": int(m[5]) / 1e5,
                  "name": m[6], "city": m[7], "region": m[8]})


def search(q):
    """Simplified version of services/airport_search.cpp (same matching idea)."""
    toks = [t for t in re.split(r"[^A-Za-z0-9-]+", q.upper()) if t]
    c = state["settings"]
    out = []
    for a in INDEX:
        score = 0
        for t in toks:
            hay = (a["name"] + " " + a["city"]).upper()
            if a["ident"] == t: s = 1000
            elif a["iata"] == t: s = 950
            elif re.search(r"\b" + re.escape(t), hay): s = 300
            elif a["region"].endswith(t): s = 60
            else: s = 0
            if not s: break
            score += s
        else:
            km = math.hypot((a["lon"] - c["lon"]) * 111.2 * math.cos(math.radians(c["lat"])), (a["lat"] - c["lat"]) * 111.2)
            out.append((-score, km, a))
    out.sort(key=lambda r: (r[0], r[1]))
    return {"total": len(out), "results": [dict(r[2], km=round(r[1], 1)) for r in out[:12]]}


RANGES = ["2.0km", "5km", "10km", "15km", "25km", "40km", "60km"]

# Fake WiFiManager home page (structure mirrors the library's markup).
WM_HOME = """<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/><title>Plane Radar</title>
<style>.c,body{text-align:center;font-family:verdana}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}input,button,select,.msg{border-radius:.3rem;width: 100%}button{cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%}.wrap {text-align:left;display:inline-block;min-width:260px;max-width:500px}</style>
HEAD</head><body class=''><div class='wrap'><h1>Plane Radar</h1><h3>plane-radar</h3>
<form action='/wifi' method='get'><button>Configure WiFi</button></form><br/>
MENU<form action='/info' method='get'><button>Info</button></form><br/>
<form action='/update' method='get'><button>Update</button></form><br/><hr><br/>
<form action='/restart' method='get'><button>Restart</button></form><br/>
<div class='msg S'><strong>Connected</strong> to HomeNetwork<br/><em><small>with IP 192.168.1.57</small></em></div></div></body></html>"""


# Wi-Fi setup page as WiFiManager renders it, with the device's airport picker
# (script taken from src/services/wifi_setup.cpp so the preview matches).
_ws = (ROOT / "src" / "services" / "wifi_setup.cpp").read_text(encoding="utf-8")
PICKER_JS = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', re.search(r"kPickerJs\[\] =(.*?);\n", _ws, re.S)[1])).replace('\\"', '"')


def wifi_page():
    nets = "".join(f"<div><a href='#p' onclick='c(this)' data-ssid='{n}'>{n}</a><div role='img' class='q q-{q}'></div></div>"
                   for n, q in (("HomeNetwork", 4), ("Neighbour-5G", 2), ("CoffeeShop", 1)))
    picker = ("<div class='pr-sec'><h3>Home airport</h3><p class='pr-hint'>The radar centres on this airport. "
              "Type a name, city or code.</p><input id='aq' placeholder='e.g. Tampa, KTPA or TPA' autocomplete='off' "
              "autocapitalize='off'><div id='ares'></div><div id='asel' class='pick'>No airport chosen yet</div></div>" + PICKER_JS)
    units = ("<div class='pr-sec'><h3>Distance units</h3><select id='du' style='margin:0'><option value='0'>Kilometres</option>"
             "<option value='1'>Miles</option><option value='2'>Nautical miles</option></select></div>")
    form = ("<form method='POST' action='wifisave'><label for='s'>SSID</label><input id='s' name='s' maxlength='32' placeholder=''><br/>"
            "<label for='p'>Password</label><input id='p' name='p' maxlength='64' type='password'><input type='checkbox' id='showpass'> "
            "<label for='showpass'>Show Password</label><br/><br/>" + picker +
            "<label for='home_ap'></label><br/><input id='home_ap' name='home_ap' maxlength='8' value='' type='hidden'>\n"
            + units + "<br/><br/><button type='submit'>Save</button></form>")
    return ("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>" + P["kHead"]
            + "<script>function c(l){document.getElementById('s').value=l.getAttribute('data-ssid');document.getElementById('p').focus()}</script>"
            + "</head><body><div class='wrap'>" + nets + form
            + "<br/><form action='/wifi?refresh=1' method='POST'><button name='refresh' value='1'>Refresh</button></form></div></body></html>")


def aircraft():
    t = time.time()
    rng = random.Random(7)
    out = []
    for i in range(state["sim"]):
        d = rng.uniform(1, 18)
        b = (rng.uniform(0, 360) + t * rng.uniform(-0.5, 0.5)) % 360
        alt = rng.randint(1500, 38000)
        gs = rng.randint(120, 470)
        out.append({"hex": f"4{i:05x}", "call": rng.choice(["KLM", "BAW", "EZY", "RYR"]) + str(1000 + i * 37),
                    "type": rng.choice(["B738", "A320", "E190", "A359"]), "reg": "", "alt": f"{alt}ft",
                    "spd": f"{gs}kt", "dists": f"{d:.1f}km", "gs": gs, "trk": rng.randint(0, 359),
                    "dist": round(d, 2), "brg": round(b), "sqk": "7700" if i == 3 else "1200",
                    "emerg": i == 3})
    out.sort(key=lambda a: a["dist"])
    return {"range": RANGES[state["settings"]["range_idx"]], "outer_km": [2, 5, 10, 15, 25, 40, 60][state["settings"]["range_idx"]] * 4 / 3,
            "sim": True, "aircraft": out}


def lookup_result(ident):
    return {"ident": ident, "name": "London Heathrow Airport", "lat": 51.4706, "lon": -0.461941, "label": True,
            "runways": [{"name": "09L/27R", "le_lat": 51.47749, "le_lon": -0.489439, "he_lat": 51.477681, "he_lon": -0.433227},
                        {"name": "09R/27L", "le_lat": 51.46478, "le_lon": -0.486808, "he_lat": 51.464957, "he_lon": -0.434048}]}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def page(self, name):
        self.send(200, P["kShellStart"] + P["kHead"] + P["kShellBody"] + P[name] + P["kShellEnd"], "text/html")

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            return self.send(200, WM_HOME.replace("HEAD", P["kHead"]).replace("MENU", P["kMenu"]), "text/html")
        if u.path == "/pr.css":
            return self.send(200, P["kCss"], "text/css")
        if u.path == "/settings":
            return self.page("kSettings")
        if u.path == "/airports":
            return self.page("kAirports")
        if u.path == "/live":
            return self.page("kLive")
        if u.path == "/wifi":
            return self.send(200, wifi_page(), "text/html")
        if u.path == "/api/search":
            return self.send(200, json.dumps(search(parse_qs(u.query).get("q", [""])[0])))
        if u.path == "/api/state":
            st = {"online": True, "ip": "192.168.1.57", "ssid": "HomeNetwork", "rssi": -58, "heap": 142000,
                  "uptime_s": 3600, "ok": 718, "err": 2, "fetch_ms": 840, "last_http": 200, "count": state["sim"],
                  "sim": state["sim"]}
            c = state["settings"]
            home = min(INDEX, key=lambda a: (a["lat"] - c["lat"]) ** 2 + ((a["lon"] - c["lon"]) * math.cos(math.radians(c["lat"]))) ** 2)
            return self.send(200, json.dumps({"settings": c, "ranges": RANGES, "status": st, "home": dict(home, km=0.0),
                                              "version": "1.1.0-cyd"}))
        if u.path == "/api/aircraft":
            return self.send(200, json.dumps(aircraft()))
        if u.path == "/api/airports":
            return self.send(200, json.dumps({"airports": state["airports"], "max": 24, "online": True,
                                              "centre": {"lat": state["settings"]["lat"], "lon": state["settings"]["lon"]}}))
        if u.path == "/api/lookup":
            lk = state["lookup"]
            if lk["state"] == "running":
                lk["progress"] = min(100, lk["progress"] + 20)
                lk["message"] = "Searching airport database..." if lk["progress"] < 60 else "Fetching runways..."
                if lk["progress"] >= 100:
                    lk.update(state="done", message="Large airport, 2 runways", result=lookup_result(lk["ident"]))
            return self.send(200, json.dumps(lk))
        self.send(404, '{"error":"not found"}')

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}") if n else {}
        if u.path == "/api/settings":
            state["settings"].update({k: v for k, v in body.items() if k in state["settings"]})
            state["sim"] = body.get("sim", state["sim"])
            return self.send(200, '{"ok":true}')
        if u.path == "/api/airports":
            if not body.get("ident"):
                return self.send(400, '{"error":"ident required (letters/digits)"}')
            body["ident"] = body["ident"].upper()
            state["airports"] = [a for a in state["airports"] if a["ident"] != body["ident"]] + [body]
            return self.send(200, '{"ok":true}')
        if u.path == "/api/airports/delete":
            state["airports"] = [a for a in state["airports"] if a["ident"] != q.get("ident", [""])[0]]
            return self.send(200, '{"ok":true}')
        if u.path == "/api/location":
            a = next((a for a in INDEX if a["ident"] == q.get("ident", [""])[0]), None)
            if not a:
                return self.send(404, '{"error":"unknown airport"}')
            state["settings"].update(lat=a["lat"], lon=a["lon"])
            return self.send(200, '{"ok":true}')
        if u.path == "/api/airports/center":
            return self.send(200, '{"ok":true}')
        if u.path == "/api/lookup":
            state["lookup"] = {"state": "running", "progress": 5, "message": "Starting...", "ident": q.get("ident", [""])[0].upper()}
            return self.send(200, '{"ok":true}')
        if u.path == "/api/lookup/save":
            r = state["lookup"].get("result")
            if r:
                state["airports"] = [a for a in state["airports"] if a["ident"] != r["ident"]] + [r]
                state["lookup"] = {"state": "idle", "progress": 0, "message": "", "ident": ""}
            return self.send(200, '{"ok":true}')
        self.send(404, '{"error":"not found"}')


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Portal preview on http://localhost:{port}/  (pages: {', '.join(sorted(P))})")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
