#!/usr/bin/env python3
"""Eltern-Portal fürs Heimnetz: Radio-Sender (eigene Aufnahmen, Spotify-Playlists), Sperre, Lautstärke, Schlaf-Timer.

  tools/.venv/bin/python tools/portal/portal.py [--port 8770]

Dann im Browser am Mac: http://127.0.0.1:8770 (Spotify-Anmeldung geht nur dort), vom Handy: http://<mac-name>.local:8770

Das Gerät fragt alle 2 s GET /api/geraet ab (Textformat, siehe core/radio_player.h) und holt den Ton als
16-Bit-mono-PCM mit 16 kHz unter /radio/<id>.pcm. Spotify läuft über librespot (inoffizieller Spotify-Connect-
Empfänger, braucht Premium): librespot → ffmpeg (16 kHz mono) → /radio/spotify.pcm. Gesteuert wird über die
Spotify-Web-API (eigene App-ID nötig, siehe README). Aufnahmen wandelt ffmpeg beim Hochladen um.
Alles, was das Portal speichert, liegt in tools/portal/daten/ (nicht im Repository).
"""
import base64
import hashlib
import json
import os
import queue
import re
import secrets
import shutil
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "daten")
REC = os.path.join(DATA, "aufnahmen")
ALBEN = os.path.join(DATA, "alben")
CONFIG = os.path.join(DATA, "einstellungen.json")
TOKENS = os.path.join(DATA, "spotify.json")
RATE = 16000                      # wie Synth::RATE auf dem Gerät
DEVICE_NAME = "Grummel-Radio"     # so heißt der Lautsprecher in der Spotify-App
SCOPES = "user-read-playback-state user-modify-playback-state playlist-read-private playlist-read-collaborative"

os.makedirs(REC, exist_ok=True)
os.makedirs(ALBEN, exist_ok=True)
sys.path.insert(0, os.path.dirname(HERE))   # tools/: Bildfunktionen aus horses.py

# Kacheln auf dem Gerät (Index wie in firmware/main.cpp), für „Apps an/aus“
APPS = [(1, "Wie geht's dir?"), (2, "Grummel"), (3, "Atmen"), (4, "Kugel ins Loch"), (5, "Sammelalbum"), (6, "Rätsel"), (7, "Radio")]
MAX_CARDS = 48
lock = threading.RLock()

try:
    import certifi
    SSL = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    SSL = ssl.create_default_context()


# ---------------- Einstellungen ----------------

DEFAULT = {"sperre": {"modus": 0, "von": 19 * 60 + 30, "bis": 7 * 60}, "laut": 70, "schlaf": 30,
           "zufall": True, "quellen": [], "spotify": {"client_id": ""}, "apps_aus": 0, "album": "pferde"}


def load_config():
    try:
        with open(CONFIG, encoding="utf-8") as f:
            c = json.load(f)
    except (OSError, ValueError):
        c = {}
    for k, v in DEFAULT.items():
        c.setdefault(k, json.loads(json.dumps(v)))
    return c


cfg = load_config()


def save_config():
    tmp = CONFIG + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=1)
    os.replace(tmp, CONFIG)


def clean_name(s, n=46):
    s = re.sub(r"[\r\n|]+", " ", s or "").strip()
    return s[:n] or "Ohne Namen"


# Zustand des Geräts (aus der letzten Abfrage)
device = {"seen": 0, "params": {}, "playing": None}


def local_epoch():
    t = time.time()
    return int(t + time.localtime(t).tm_gmtoff)


# ---------------- Spotify: librespot + ffmpeg ----------------

class SpotifyAudio:
    """Startet librespot (Pipe-Ausgabe) und ffmpeg, verteilt den Ton an die verbundenen Geräte."""

    def __init__(self):
        self.subs = []
        self.proc = None
        self.status = "aus"
        self.log = os.path.join(DATA, "librespot.log")

    def start(self):
        exe = shutil.which("librespot")
        ff = shutil.which("ffmpeg")
        if not exe:
            self.status = "librespot fehlt (brew install librespot)"
            return
        if not ff:
            self.status = "ffmpeg fehlt (brew install ffmpeg)"
            return
        threading.Thread(target=self.run, args=(exe, ff), daemon=True).start()

    def run(self, exe, ff):
        while True:
            self.status = "läuft"
            with open(self.log, "ab") as log:
                lib = subprocess.Popen([exe, "--name", DEVICE_NAME, "--backend", "pipe", "--bitrate", "160",
                                        "--device-type", "speaker", "--initial-volume", "100",
                                        "--enable-volume-normalisation", "--cache", os.path.join(DATA, "librespot")],
                                       stdout=subprocess.PIPE, stderr=log)
                conv = subprocess.Popen([ff, "-hide_banner", "-loglevel", "error", "-f", "s16le", "-ar", "44100", "-ac", "2",
                                         "-i", "pipe:0", "-f", "s16le", "-ar", str(RATE), "-ac", "1", "-flush_packets", "1", "pipe:1"],
                                        stdin=lib.stdout, stdout=subprocess.PIPE, stderr=log, bufsize=0)
                lib.stdout.close()
                self.proc = lib
                while True:
                    chunk = conv.stdout.read(1600)   # 50 ms
                    if not chunk:
                        break
                    with lock:
                        subs = list(self.subs)
                    for q in subs:
                        try:
                            q.put_nowait(chunk)
                        except queue.Full:
                            pass   # Gerät hinkt hinterher: lieber eine Lücke als Verzögerung
                lib.kill()
                conv.kill()
            self.status = "neu gestartet"
            time.sleep(3)

    def subscribe(self):
        q = queue.Queue(maxsize=80)
        with lock:
            self.subs.append(q)
        return q

    def unsubscribe(self, q):
        with lock:
            if q in self.subs:
                self.subs.remove(q)


audio = SpotifyAudio()


# ---------------- Spotify-Web-API ----------------

pkce = {}


def tokens():
    try:
        with open(TOKENS, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_tokens(t):
    with open(TOKENS, "w", encoding="utf-8") as f:
        json.dump(t, f)
    os.chmod(TOKENS, 0o600)


def token_request(data):
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request("https://accounts.spotify.com/api/token", data=body,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, context=SSL, timeout=10) as r:
        return json.load(r)


def access_token():
    t = tokens()
    if not t.get("refresh_token"):
        return None
    if t.get("expires", 0) > time.time() + 60:
        return t["access_token"]
    new = token_request({"grant_type": "refresh_token", "refresh_token": t["refresh_token"],
                         "client_id": cfg["spotify"]["client_id"]})
    t["access_token"] = new["access_token"]
    t["expires"] = time.time() + new.get("expires_in", 3600)
    if new.get("refresh_token"):
        t["refresh_token"] = new["refresh_token"]
    save_tokens(t)
    return t["access_token"]


def api(method, path, body=None):
    tok = access_token()
    if not tok:
        raise RuntimeError("Spotify ist nicht verbunden")
    data = json.dumps(body).encode() if body is not None else (b"" if method in ("PUT", "POST") else None)
    req = urllib.request.Request("https://api.spotify.com/v1" + path, data=data, method=method,
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, context=SSL, timeout=10) as r:
            raw = r.read()
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        msg = e.read().decode(errors="replace")
        try:
            msg = json.loads(msg)["error"]["message"]
        except (ValueError, KeyError, TypeError):
            pass
        raise RuntimeError(f"Spotify {e.code}: {msg}")


def radio_device_id():
    for d in api("GET", "/me/player/devices").get("devices", []):
        if d.get("name") == DEVICE_NAME:
            return d["id"]
    raise RuntimeError("Grummel-Radio ist in Spotify nicht zu sehen. Einmal in der Spotify-App als Lautsprecher wählen.")


spotify_hint = ""


def spotify_action(action, source=None):
    global spotify_hint
    try:
        dev = radio_device_id()
        q = "?device_id=" + urllib.parse.quote(dev)
        if action == "play":
            api("PUT", "/me/player/play" + q, {"context_uri": source["uri"]})
            if cfg.get("zufall"):
                api("PUT", "/me/player/shuffle" + q + "&state=true")
        elif action == "pause":
            api("PUT", "/me/player/pause" + q)
        elif action == "weiter":
            api("POST", "/me/player/next" + q)
        spotify_hint = ""
    except Exception as e:   # Hinweis landet auf dem Gerät und im Portal
        spotify_hint = str(e)[:60]
        print("spotify:", e, file=sys.stderr)


# ---------------- HTTP ----------------

def find_source(sid):
    for s in cfg["quellen"]:
        if s["id"] == sid:
            return s
    return None


def device_reply():
    with lock:
        lines = [f"zeit {local_epoch()}",
                 f"sperre {cfg['sperre']['modus']} {cfg['sperre']['von']} {cfg['sperre']['bis']}",
                 f"laut {cfg['laut']}", f"schlaf {cfg['schlaf']}"]
        for s in cfg["quellen"]:
            lines.append(f"quelle {s['id']} {s['art']} {s['name']}")
        lines.append(f"apps {int(cfg.get('apps_aus', 0))}")
        aid = cfg.get("album", "pferde")
        lines.append(f"album {aid} {album_version(aid):08x}" if aid != "pferde" else "album pferde 0")
        hint = spotify_hint
        if not hint and any(s["art"] == "spotify" for s in cfg["quellen"]) and not tokens().get("refresh_token"):
            hint = "Spotify ist nicht verbunden"
        if hint:
            lines.append("hinweis " + hint[:60])
    return "\n".join(lines) + "\n"


def wav_header(n):
    return (b"RIFF" + (36 + n).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little") + (1).to_bytes(2, "little")
            + (1).to_bytes(2, "little") + RATE.to_bytes(4, "little") + (RATE * 2).to_bytes(4, "little") + (2).to_bytes(2, "little")
            + (16).to_bytes(2, "little") + b"data" + n.to_bytes(4, "little"))


def convert(raw, ext):
    """Beliebige Audiodatei → 16 kHz mono PCM (lautheitsangeglichen)."""
    ff = shutil.which("ffmpeg")
    if not ff:
        raise RuntimeError("ffmpeg fehlt (brew install ffmpeg)")
    src = os.path.join(DATA, "upload" + ext)
    with open(src, "wb") as f:
        f.write(raw)
    try:
        out = subprocess.run([ff, "-hide_banner", "-loglevel", "error", "-i", src, "-af", "loudnorm=I=-18:TP=-2",
                              "-f", "s16le", "-ar", str(RATE), "-ac", "1", "pipe:1"], capture_output=True, timeout=600)
    finally:
        os.remove(src)
    if out.returncode or not out.stdout:
        raise RuntimeError("Datei ließ sich nicht umwandeln: " + out.stderr.decode(errors="replace")[:200])
    return out.stdout


def new_id(prefix):
    used = {s["id"] for s in cfg["quellen"]}
    i = 1
    while f"{prefix}{i}" in used or os.path.exists(os.path.join(REC, f"{prefix}{i}.pcm")):
        i += 1
    return f"{prefix}{i}"


# ---------------- Sammelalben ----------------
# daten/alben/<id>/thema.json (Titel, Karten) und <karte>.jpg (328×196, Baseline-JPEG fürs Gerät)

def slug(s):
    s = s.lower().replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("ß", "ss")
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")[:20] or "album"


def album_dir(aid):
    return os.path.join(ALBEN, re.sub(r"[^a-z0-9-]", "", aid))


def album_load(aid):
    try:
        with open(os.path.join(album_dir(aid), "thema.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def album_save(a):
    d = album_dir(a["id"])
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "thema.json"), "w", encoding="utf-8") as f:
        json.dump(a, f, ensure_ascii=False, indent=1)


def album_list():
    out = []
    for name in sorted(os.listdir(ALBEN)):
        a = album_load(name)
        if a:
            for k in a["karten"]:
                k["bild"] = os.path.exists(os.path.join(album_dir(a["id"]), f"{k['id']}.jpg"))
            out.append(a)
    return out


def field(s, n):
    return re.sub(r"[\r\n|]+", " ", str(s or "")).strip()[:n]


def album_text(a):
    """Kartentext fürs Gerät (Format 2, siehe tiles/album.h)."""
    lines = [f"#album {a['id']}", f"#titel {a['titel']}", f"#symbol {a['symbol']}", f"#zaehlwort {a['zaehlwort']}",
             f"#frage {a['frage']}", "#format 2",
             "#bilder " + ",".join(str(k["id"]) for k in a["karten"] if os.path.exists(os.path.join(album_dir(a["id"]), f"{k['id']}.jpg")))]
    for k in a["karten"]:
        lines.append("|".join([str(k["id"]), k["name"], k.get("untertitel", ""), k.get("zeile1", ""), k.get("zeile2", ""),
                               a.get("ueberschrift3", ""), k.get("text3", ""), a.get("ueberschrift4", ""), k.get("text4", ""),
                               k.get("wusstest", ""), k.get("foto", "")]))
    return "\n".join(lines) + "\n"


def album_version(aid):
    a = album_load(aid)
    if not a:
        return 0
    v = zlib.crc32(album_text(a).encode())
    for k in a["karten"]:
        p = os.path.join(album_dir(aid), f"{k['id']}.jpg")
        if os.path.exists(p):
            st = os.stat(p)
            v = zlib.crc32(f"{k['id']}:{st.st_size}:{int(st.st_mtime)}".encode(), v)
    return v or 1


def card_image(raw):
    """Foto → 328×196 ohne Abschneiden (Rand unscharf gefüllt), Baseline-JPEG."""
    try:
        from PIL import Image, ImageOps
        from horses import fit_card
    except ImportError:
        raise RuntimeError("Pillow fehlt: tools/.venv/bin/pip install pillow")
    import io
    im = ImageOps.exif_transpose(Image.open(io.BytesIO(raw)))
    card = fit_card(im)
    buf = io.BytesIO()
    card.save(buf, "JPEG", quality=84, progressive=False, optimize=True)
    return buf.getvalue(), card


def card_rgb(aid, kid, thumb):
    """Rohdaten RGB565 für den Simulator (der kein JPEG dekodiert)."""
    from PIL import Image
    from horses import to_rgb565, THUMB_W, THUMB_H
    im = Image.open(os.path.join(album_dir(aid), f"{kid}.jpg")).convert("RGB")
    if thumb:
        im = im.resize((THUMB_W, THUMB_H), Image.LANCZOS)
    return to_rgb565(im)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "GrummelPortal/1"

    def log_message(self, fmt, *args):
        if not self.path.startswith("/api/geraet"):
            sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    # -- Antworten --
    def send(self, code, body, ctype="text/plain; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def json(self, obj, code=200):
        self.send(code, json.dumps(obj, ensure_ascii=False), "application/json; charset=utf-8")

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 300 * 1024 * 1024:
            raise RuntimeError("Datei zu groß")
        return self.rfile.read(n) if n else b""

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        try:
            if u.path == "/":
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    return self.send(200, f.read(), "text/html; charset=utf-8")
            if u.path == "/api/geraet":
                with lock:
                    device["seen"] = time.time()
                    device["params"] = q
                return self.send(200, device_reply())
            if u.path == "/api/radio":
                return self.radio_action(q)
            if u.path == "/api/apps":   # am Gerät umgestellt
                with lock:
                    cfg["apps_aus"] = int(q.get("aus", 0)) & 0xFE
                    save_config()
                return self.send(200, "ok\n")
            if u.path == "/api/alben":
                return self.json({"aktiv": cfg.get("album", "pferde"), "alben": album_list(), "max": MAX_CARDS})
            if u.path == "/album/karten.txt":   # aktives Thema fürs Gerät
                a = album_load(cfg.get("album", ""))
                if not a:
                    return self.send(404, "kein Thema\n")
                return self.send(200, album_text(a))
            m = re.fullmatch(r"/album/bild/(\d+)(_t)?\.(jpg|rgb)", u.path)
            if m:
                aid = cfg.get("album", "")
                if m.group(3) == "jpg":
                    with open(os.path.join(album_dir(aid), m.group(1) + ".jpg"), "rb") as f:
                        return self.send(200, f.read(), "image/jpeg")
                return self.send(200, card_rgb(aid, m.group(1), bool(m.group(2))), "application/octet-stream")
            m = re.fullmatch(r"/alben/([a-z0-9-]+)/(\d+)\.jpg", u.path)
            if m:
                with open(os.path.join(album_dir(m.group(1)), m.group(2) + ".jpg"), "rb") as f:
                    return self.send(200, f.read(), "image/jpeg", {"Cache-Control": "no-cache"})
            if u.path == "/api/sperre":   # am Gerät umgestellt
                with lock:
                    cfg["sperre"]["modus"] = max(0, min(2, int(q.get("modus", 0))))
                    save_config()
                return self.send(200, "ok\n")
            if u.path == "/api/status":
                return self.json(self.status())
            if u.path == "/api/spotify/meine":
                items = api("GET", "/me/playlists?limit=50").get("items", [])
                return self.json([{"name": p["name"], "uri": p["uri"]} for p in items if p])
            if u.path == "/spotify/login":
                return self.spotify_login()
            if u.path == "/spotify/callback":
                return self.spotify_callback(q)
            m = re.fullmatch(r"/radio/([A-Za-z0-9_:-]+)\.pcm", u.path)
            if m:
                return self.stream(m.group(1))
            m = re.fullmatch(r"/vorschau/([A-Za-z0-9_-]+)\.wav", u.path)
            if m:
                path = os.path.join(REC, m.group(1) + ".pcm")
                with open(path, "rb") as f:
                    pcm = f.read()
                return self.send(200, wav_header(len(pcm)) + pcm, "audio/wav")
            self.send(404, "nicht gefunden\n")
        except Exception as e:
            self.send(500, f"Fehler: {e}\n")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        try:
            if u.path == "/api/aufnahme":
                raw = self.body()
                pcm = convert(raw, "." + re.sub(r"[^a-z0-9]", "", q.get("typ", "webm"))[:5])
                with lock:
                    sid = new_id("a")
                    with open(os.path.join(REC, sid + ".pcm"), "wb") as f:
                        f.write(pcm)
                    cfg["quellen"].append({"id": sid, "art": "aufnahme", "name": clean_name(q.get("name")),
                                           "sekunden": round(len(pcm) / 2 / RATE)})
                    save_config()
                return self.json({"ok": True, "id": sid})
            if u.path == "/api/album/bild":
                raw = self.body()
                jpg, _ = card_image(raw)
                with lock:
                    a = album_load(q.get("album", ""))
                    kid = int(q.get("karte", 0))
                    if not a or not any(k["id"] == kid for k in a["karten"]):
                        raise RuntimeError("Karte nicht gefunden")
                    with open(os.path.join(album_dir(a["id"]), f"{kid}.jpg"), "wb") as f:
                        f.write(jpg)
                return self.json({"ok": True})
            data = json.loads(self.body() or b"{}")
            with lock:
                if u.path == "/api/einstellungen":
                    if "sperre" in data:
                        s = data["sperre"]
                        cfg["sperre"] = {"modus": max(0, min(2, int(s["modus"]))), "von": int(s["von"]) % 1440, "bis": int(s["bis"]) % 1440}
                    for k, lo, hi in (("laut", 10, 100), ("schlaf", 0, 180)):
                        if k in data:
                            cfg[k] = max(lo, min(hi, int(data[k])))
                    if "zufall" in data:
                        cfg["zufall"] = bool(data["zufall"])
                    if "apps_aus" in data:
                        cfg["apps_aus"] = int(data["apps_aus"]) & 0xFE
                elif u.path == "/api/album/speichern":
                    aid = data.get("id") or slug(data.get("symbol") or data.get("titel", ""))
                    a = album_load(aid) if data.get("id") else None
                    if not a:
                        if aid == "pferde" or album_load(aid):
                            aid = aid[:16] + "-" + secrets.token_hex(2)
                        a = {"id": aid, "karten": []}
                    a["titel"] = field(data.get("titel"), 28) or "Sammelalbum"
                    a["symbol"] = field(data.get("symbol"), 12) or "Album"
                    a["zaehlwort"] = field(data.get("zaehlwort"), 14) or "Karten"
                    a["frage"] = field(data.get("frage"), 40) or "Wer ist das?"
                    a["ueberschrift3"] = field(data.get("ueberschrift3"), 20)
                    a["ueberschrift4"] = field(data.get("ueberschrift4"), 20)
                    album_save(a)
                    save_config()
                    return self.json({"ok": True, "id": aid})
                elif u.path == "/api/album/aktiv":
                    aid = data.get("id", "pferde")
                    if aid != "pferde" and not album_load(aid):
                        raise RuntimeError("Thema nicht gefunden")
                    cfg["album"] = aid
                elif u.path == "/api/album/loeschen":
                    d = album_dir(data["id"])
                    if os.path.isdir(d) and data["id"] != "pferde":
                        shutil.rmtree(d)
                    if cfg.get("album") == data["id"]:
                        cfg["album"] = "pferde"
                elif u.path == "/api/album/karte":
                    a = album_load(data.get("album", ""))
                    if not a:
                        raise RuntimeError("Thema nicht gefunden")
                    kid = int(data.get("id") or 0)
                    k = next((k for k in a["karten"] if k["id"] == kid), None)
                    if not k:
                        if len(a["karten"]) >= MAX_CARDS:
                            raise RuntimeError(f"Höchstens {MAX_CARDS} Karten pro Thema")
                        kid = max([k["id"] for k in a["karten"]] + [0]) + 1
                        k = {"id": kid}
                        a["karten"].append(k)
                    k["name"] = field(data.get("name"), 30) or "Ohne Namen"
                    for key, n in (("untertitel", 30), ("zeile1", 60), ("zeile2", 40), ("text3", 90), ("text4", 90), ("wusstest", 160), ("foto", 90)):
                        k[key] = field(data.get(key), n)
                    album_save(a)
                    return self.json({"ok": True, "id": kid})
                elif u.path == "/api/album/karte/loeschen":
                    a = album_load(data.get("album", ""))
                    if a:
                        a["karten"] = [k for k in a["karten"] if k["id"] != int(data["id"])]
                        album_save(a)
                        p = os.path.join(album_dir(a["id"]), f"{int(data['id'])}.jpg")
                        if os.path.exists(p):
                            os.remove(p)
                elif u.path == "/api/quelle/loeschen":
                    cfg["quellen"] = [s for s in cfg["quellen"] if s["id"] != data["id"]]
                    p = os.path.join(REC, re.sub(r"[^A-Za-z0-9_-]", "", data["id"]) + ".pcm")
                    if os.path.exists(p):
                        os.remove(p)
                elif u.path == "/api/quelle/umbenennen":
                    s = find_source(data["id"])
                    if s:
                        s["name"] = clean_name(data["name"])
                elif u.path == "/api/quelle/reihenfolge":
                    order = {sid: i for i, sid in enumerate(data["ids"])}
                    cfg["quellen"].sort(key=lambda s: order.get(s["id"], 999))
                elif u.path == "/api/spotify/playlist":
                    self.add_playlist(data)
                elif u.path == "/api/spotify/client":
                    cfg["spotify"]["client_id"] = re.sub(r"[^A-Za-z0-9]", "", data.get("client_id", ""))[:64]
                elif u.path == "/api/spotify/trennen":
                    if os.path.exists(TOKENS):
                        os.remove(TOKENS)
                else:
                    return self.send(404, "nicht gefunden\n")
                save_config()
            self.json({"ok": True})
        except Exception as e:
            self.json({"ok": False, "fehler": str(e)}, 400)

    # -- Einzelteile --
    def add_playlist(self, data):
        link = data.get("link", "").strip()
        m = re.search(r"playlist[/:]([A-Za-z0-9]{10,40})", link)
        if not m:
            raise RuntimeError("Das sieht nicht nach einem Playlist-Link aus")
        pid = m.group(1)
        name = data.get("name")
        if not name:
            try:
                name = api("GET", f"/playlists/{pid}?fields=name")["name"]
            except Exception:
                name = "Spotify-Playlist"
        sid = "sp-" + pid[:20]
        if not find_source(sid):
            cfg["quellen"].append({"id": sid, "art": "spotify", "name": clean_name(name), "uri": "spotify:playlist:" + pid})

    def radio_action(self, q):
        act = q.get("aktion")
        src = find_source(q.get("quelle", ""))
        with lock:
            device["playing"] = src["id"] if act == "play" and src else None
            spotify_on = act == "play" and src and src["art"] == "spotify"
        if spotify_on:
            threading.Thread(target=spotify_action, args=("play", src), daemon=True).start()
        elif act in ("pause", "weiter") or (act == "play" and src and src["art"] != "spotify"):
            if tokens().get("refresh_token") and any(s["art"] == "spotify" for s in cfg["quellen"]):
                threading.Thread(target=spotify_action, args=("weiter" if act == "weiter" else "pause",), daemon=True).start()
        self.send(200, "ok\n")

    def stream(self, sid):
        # Kein Content-Length: der Strom endet, wenn die Verbindung schließt
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            if sid == "spotify":
                q = audio.subscribe()
                try:
                    while True:
                        try:
                            self.wfile.write(q.get(timeout=1))
                        except queue.Empty:
                            continue   # Pause in Spotify: Verbindung offen halten
                finally:
                    audio.unsubscribe(q)
            else:
                path = os.path.join(REC, re.sub(r"[^A-Za-z0-9_-]", "", sid) + ".pcm")
                with open(path, "rb") as f:
                    while True:
                        chunk = f.read(4096)
                        if not chunk:
                            break
                        self.wfile.write(chunk)   # das Gerät liest im eigenen Tempo (TCP bremst)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass

    def status(self):
        with lock:
            seen = device["seen"]
            t = tokens()
            return {"geraet": {"online": time.time() - seen < 8, "zuletzt": int(seen), "werte": device["params"]},
                    "sperre": cfg["sperre"], "laut": cfg["laut"], "schlaf": cfg["schlaf"], "zufall": cfg.get("zufall", True),
                    "quellen": cfg["quellen"],
                    "apps": [{"index": i, "name": n, "an": not (int(cfg.get("apps_aus", 0)) >> i & 1)} for i, n in APPS],
                    "spotify": {"client_id": cfg["spotify"]["client_id"], "verbunden": bool(t.get("refresh_token")),
                                "librespot": audio.status, "hinweis": spotify_hint},
                    "uhr": time.strftime("%H:%M")}

    def spotify_login(self):
        cid = cfg["spotify"]["client_id"]
        host = self.headers.get("Host", "")
        if not cid:
            return self.send(400, "Erst die Client-ID der Spotify-App eintragen.\n")
        if not host.startswith("127.0.0.1"):
            return self.send(400, "Die Spotify-Anmeldung geht nur am Mac selbst: http://127.0.0.1:%d\n" % self.server.server_port)
        verifier = secrets.token_urlsafe(64)
        state = secrets.token_urlsafe(16)
        pkce[state] = verifier
        challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
            "client_id": cid, "response_type": "code", "redirect_uri": self.redirect_uri(), "scope": SCOPES,
            "code_challenge_method": "S256", "code_challenge": challenge, "state": state})
        self.send(302, "", extra={"Location": url})

    def redirect_uri(self):
        return "http://127.0.0.1:%d/spotify/callback" % self.server.server_port

    def spotify_callback(self, q):
        verifier = pkce.pop(q.get("state", ""), None)
        if not verifier or "code" not in q:
            return self.send(400, "Anmeldung abgebrochen: " + q.get("error", "unbekannt") + "\n")
        t = token_request({"grant_type": "authorization_code", "code": q["code"], "redirect_uri": self.redirect_uri(),
                           "client_id": cfg["spotify"]["client_id"], "code_verifier": verifier})
        save_tokens({"access_token": t["access_token"], "refresh_token": t["refresh_token"],
                     "expires": time.time() + t.get("expires_in", 3600)})
        self.send(302, "", extra={"Location": "/#spotify"})


def main():
    port = 8770
    if "--port" in sys.argv:
        port = int(sys.argv[sys.argv.index("--port") + 1])
    audio.start()
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    # im Heimnetz per Bonjour ankündigen, damit das Gerät das Portal ohne Adresse findet
    bonjour = None
    if shutil.which("dns-sd"):
        bonjour = subprocess.Popen(["dns-sd", "-R", "Grummel-Portal", "_grummel._tcp", "local", str(port)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    srv.daemon_threads = True
    host = subprocess.run(["scutil", "--get", "LocalHostName"], capture_output=True, text=True).stdout.strip() or "mac"
    print(f"Eltern-Portal: http://127.0.0.1:{port}  (im Heimnetz: http://{host}.local:{port})")
    print(f"Spotify: {audio.status}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if bonjour:
            bonjour.kill()


if __name__ == "__main__":
    main()
