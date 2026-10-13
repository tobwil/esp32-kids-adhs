#!/usr/bin/env python3
"""Pferde-Album: Fotos holen, prüfen, einbauen, übertragen.

  horses.py fetch     Fotos von Wikimedia Commons holen (Hauptbild des deutschen Wikipedia-Artikels,
                      nur freie Lizenzen), auf 328×196 zuschneiden, Bildnachweis merken → tools/horses/out/
  horses.py review    Übersichtsbild aller Karten zum Durchsehen → tools/horses/out/uebersicht.png
  horses.py build     Kartentexte als eingebautes Paket → tiles/album_builtin.h (+ out/karten.txt),
                      Bildnachweis → tools/horses/BILDNACHWEIS.md, Rohbilder für den Simulator aus den JPEGs
  horses.py upload    Fotos und Texte per USB aufs Gerät (LittleFS /pferde/…)
  horses.py import <ordner>   eigene oder generierte Bilder übernehmen (<Nr>.png, <Nr>-name.jpg …),
                      ohne Abschneiden: der Rand wird mit einer unscharfen Fassung desselben Bildes gefüllt

Ein unpassendes Foto ersetzen: in pferde.json beim Pferd "commons": "Datei:Name.jpg" eintragen, dann fetch erneut.
Benötigt: pillow, pyserial, certifi (tools/.venv).
"""
import base64
import glob
import io
import json
import os
import re
import ssl
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(HERE, "horses", "pferde.json")
OUT = os.path.join(HERE, "horses", "out")
CREDITS = os.path.join(HERE, "horses", "fotos.json")
CARD_W, CARD_H, THUMB_W, THUMB_H = 328, 196, 164, 98
UA = "DeskKacheln-Pferdealbum/1.0 (privates Lernprojekt)"
FREE = ("cc0", "cc by", "cc-by", "public domain", "gemeinfrei", "pd")


def _tls():
    """Zertifikatsprüfung mit certifi (python.org-Installationen kennen die System-Zertifikate nicht)."""
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


TLS = _tls()


def load():
    with open(DATA, encoding="utf-8") as f:
        return json.load(f)["pferde"]


def credits():
    if os.path.exists(CREDITS):
        with open(CREDITS, encoding="utf-8") as f:
            return json.load(f)
    return {}


def api(host, **params):
    params["format"] = "json"
    url = f"https://{host}/w/api.php?" + urllib.parse.urlencode(params)
    raw = download(url)
    if raw is None:
        raise RuntimeError("API antwortet nicht (Ratenlimit)")
    return json.loads(raw)


def strip_html(s):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", s or "")).strip()


def find_image(p):
    """Dateiname des Fotos: Override aus pferde.json oder Hauptbild des Wikipedia-Artikels."""
    if p.get("commons"):
        return p["commons"].split(":", 1)[-1]
    d = api("de.wikipedia.org", action="query", prop="pageimages", piprop="name", titles=p["wiki"], redirects=1)
    for page in d["query"]["pages"].values():
        if page.get("pageimage"):
            return page["pageimage"]
    # Artikel nicht gefunden: suchen
    d = api("de.wikipedia.org", action="query", list="search", srsearch=p["wiki"] + " Pferd", srlimit=1)
    hits = d["query"]["search"]
    if hits:
        d = api("de.wikipedia.org", action="query", prop="pageimages", piprop="name", titles=hits[0]["title"])
        for page in d["query"]["pages"].values():
            if page.get("pageimage"):
                return page["pageimage"]
    return None


def image_info(name):
    d = api("commons.wikimedia.org", action="query", titles="File:" + name, prop="imageinfo",
            iiprop="url|extmetadata", iiurlwidth=960)   # Standardbreite: wird nicht neu erzeugt, schont das Ratenlimit
    for page in d["query"]["pages"].values():
        ii = (page.get("imageinfo") or [None])[0]
        if not ii:
            return None
        meta = ii.get("extmetadata", {})
        artist = strip_html(meta.get("Artist", {}).get("value", ""))
        if not artist or "machine-readable" in artist.lower():
            artist = strip_html(meta.get("Credit", {}).get("value", "")) or "siehe Wikimedia Commons"
            if "machine-readable" in artist.lower():
                artist = "siehe Wikimedia Commons"
        return {
            "datei": name,
            "url": ii.get("thumburl") or ii["url"],
            "seite": ii.get("descriptionurl", ""),
            "lizenz": strip_html(meta.get("LicenseShortName", {}).get("value", "")),
            "lizenz_url": meta.get("LicenseUrl", {}).get("value", ""),
            "urheber": artist,
        }
    return None


def download(url, tries=6):
    """Bild laden; bei 429 so lange warten, wie der Server verlangt"""
    import urllib.error
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30, context=TLS) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code != 429:
                raise
            wait = int(e.headers.get("Retry-After", "0") or 0) or 20 * (i + 1)
            print(f"  … Server bittet um Pause, warte {wait} s", flush=True)
            time.sleep(wait)
    return None


def to_rgb565(img):
    """Pillow-Bild → RGB565, Little Endian (wie auf ESP32 und im Simulator)."""
    out = bytearray()
    pixels = img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()
    for r, g, b in pixels:
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def crop_card(im):
    """auf 328:196 zuschneiden, etwas oberhalb der Mitte (Köpfe bleiben im Bild)"""
    from PIL import Image
    im = im.convert("RGB")
    tw, th = CARD_W, CARD_H
    scale = max(tw / im.width, th / im.height)
    im = im.resize((max(tw, round(im.width * scale)), max(th, round(im.height * scale))), Image.LANCZOS)
    x = (im.width - tw) // 2
    y = int((im.height - th) * 0.4)
    return im.crop((x, y, x + tw, y + th))


def fit_card(im):
    """ganzes Bild auf 328×196, Rand mit unscharfer, abgedunkelter Fassung desselben Bildes füllen"""
    from PIL import Image, ImageFilter, ImageEnhance
    im = im.convert("RGB")
    bg = im.resize((CARD_W, CARD_H), Image.LANCZOS) if abs(im.width / im.height - CARD_W / CARD_H) < 0.02 else None
    if bg is not None:
        return bg
    cover = crop_card(im).filter(ImageFilter.GaussianBlur(14))
    cover = ImageEnhance.Brightness(cover).enhance(0.55)
    scale = min(CARD_W / im.width, CARD_H / im.height)
    fg = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
    cover.paste(fg, ((CARD_W - fg.width) // 2, (CARD_H - fg.height) // 2))
    return cover


def save_card(pid, card):
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    card.save(os.path.join(OUT, f"{pid}.jpg"), "JPEG", quality=84, progressive=False, optimize=True)   # Baseline für JPEGDEC
    with open(os.path.join(OUT, f"{pid}.rgb"), "wb") as f:
        f.write(to_rgb565(card))
    with open(os.path.join(OUT, f"{pid}_t.rgb"), "wb") as f:
        f.write(to_rgb565(card.resize((THUMB_W, THUMB_H), Image.LANCZOS)))


def cmd_import(folder):
    """eigene Bilder: Dateiname beginnt mit der Kartennummer (6.png, 06-haflinger.jpg)"""
    from PIL import Image
    ids = {p["id"]: p["name"] for p in load()}
    cr = credits()
    n = 0
    for path in sorted(glob.glob(os.path.join(os.path.expanduser(folder), "*"))):
        m = re.match(r"0*(\d+)", os.path.basename(path))
        if not m or int(m.group(1)) not in ids or not path.lower().endswith((".jpg", ".jpeg", ".png", ".webp")):
            continue
        pid = int(m.group(1))
        save_card(str(pid), fit_card(Image.open(path)))
        cr[str(pid)] = {"datei": os.path.basename(path), "url": "", "seite": "", "lizenz": "eigenes Bild", "lizenz_url": "", "urheber": "eigenes Bild"}
        print(f"✓ {pid}. {ids[pid]} ← {os.path.basename(path)}")
        n += 1
    with open(CREDITS, "w", encoding="utf-8") as f:
        json.dump(cr, f, ensure_ascii=False, indent=2)
    print(f"{n} Bilder übernommen. Weiter mit: horses.py review, dann horses.py upload")


def cmd_fetch():
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    cr = credits()
    for p in load():
        pid = str(p["id"])
        if pid in cr and os.path.exists(os.path.join(OUT, f"{pid}.jpg")) and not p.get("commons_neu"):
            continue   # schon da
        try:
            name = find_image(p)
            info = image_info(name) if name else None
        except Exception as e:   # Netzfehler: dieses Pferd überspringen
            print(f"✗ {p['name']}: {e}")
            continue
        if not info:
            print(f"✗ {p['name']}: kein Foto im Artikel „{p['wiki']}“ (\"commons\" in pferde.json setzen)")
            continue
        if not any(f in info["lizenz"].lower() for f in FREE):
            print(f"✗ {p['name']}: Lizenz „{info['lizenz']}“ ist nicht frei, übersprungen")
            continue
        raw = download(info["url"])
        if raw is None:
            print(f"✗ {p['name']}: Download fehlgeschlagen, später noch einmal fetch")
            continue
        card = crop_card(Image.open(io.BytesIO(raw)))
        card.save(os.path.join(OUT, f"{pid}.jpg"), "JPEG", quality=84, progressive=False, optimize=True)   # Baseline für JPEGDEC
        with open(os.path.join(OUT, f"{pid}.rgb"), "wb") as f:
            f.write(to_rgb565(card))
        with open(os.path.join(OUT, f"{pid}_t.rgb"), "wb") as f:
            f.write(to_rgb565(card.resize((THUMB_W, THUMB_H), Image.LANCZOS)))
        cr[pid] = info
        size = os.path.getsize(os.path.join(OUT, f"{pid}.jpg"))
        print(f"✓ {p['name']}: {info['datei']} ({info['lizenz']}, {info['urheber'][:40]}) → {size // 1024} KB")
        time.sleep(3)   # freundlich zu den Wikimedia-Servern
        with open(CREDITS, "w", encoding="utf-8") as f:   # nach jedem Bild sichern
            json.dump(cr, f, ensure_ascii=False, indent=2)


def clean_author(who):
    """Urheberangabe von Commons kartentauglich kürzen"""
    who = (who or "").strip()
    m = re.search(r"No machine-readable author provided\.\s*(\S+) assumed", who)
    if m:
        return m.group(1)
    if who.count("_") >= 3 or who.lower().startswith("photograph"):   # Dateiname oder Beschreibung statt Name
        return "siehe Wikimedia Commons"
    who = re.split(r" - | \(", who)[0].strip()
    return who or "siehe Wikimedia Commons"


def foto_line(c):
    if not c:
        return ""
    if c.get("lizenz") == "eigenes Bild":
        return ""
    who = clean_author(c["urheber"])
    if len(who) > 38:
        who = who[:36] + "…"
    return f"Foto: {who} · {c['lizenz']} · Wikimedia Commons".replace("|", "/")


def card_lines():
    cr = credits()
    out = ["#album pferde"]
    for p in load():
        fields = [str(p["id"]), p["name"], p["typ"], p["herkunft"], p["groesse"], p["farben"], p["wesen"], p["besonders"], foto_line(cr.get(str(p["id"])))]
        out.append("|".join(f.replace("|", "/").replace("\n", " ") for f in fields))
    return out


def write_credits_md():
    """Bildnachweis für alle Fotos (Pflicht bei CC BY/CC BY-SA)"""
    cr = credits()
    rows = ["# Bildnachweis Pferde-Album", "",
            "Alle Fotos stammen von Wikimedia Commons. Sie wurden auf 328 × 196 Pixel zugeschnitten und verkleinert "
            "(`tools/horses/out/<id>.jpg`). Die bearbeiteten Fassungen stehen unter derselben Lizenz wie das jeweilige Original.", "",
            "| Nr. | Pferd | Urheber | Lizenz | Original |", "|---|---|---|---|---|"]
    for p in load():
        c = cr.get(str(p["id"]))
        if not c:
            rows.append(f"| {p['id']} | {p['name']} | – | – | kein Foto |")
            continue
        lic = f"[{c['lizenz']}]({c['lizenz_url']})" if c.get("lizenz_url") else c["lizenz"]
        who = (c.get("urheber") or "").replace("|", "/")
        rows.append(f"| {p['id']} | {p['name']} | {who} | {lic} | [{c['datei']}]({c['seite']}) |")
    with open(os.path.join(HERE, "horses", "BILDNACHWEIS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")


def make_raw():
    """Rohdaten für den Simulator aus den JPEGs (nach einem frischen Klonen, ohne Download)"""
    from PIL import Image
    for jpg in glob.glob(os.path.join(OUT, "*.jpg")):
        base = jpg[:-4]
        if os.path.exists(base + ".rgb") and os.path.exists(base + "_t.rgb"):
            continue
        im = Image.open(jpg).convert("RGB")
        with open(base + ".rgb", "wb") as f:
            f.write(to_rgb565(im))
        with open(base + "_t.rgb", "wb") as f:
            f.write(to_rgb565(im.resize((THUMB_W, THUMB_H), Image.LANCZOS)))


def cmd_build():
    write_credits_md()
    try:
        make_raw()
    except ImportError:
        print("(pillow fehlt: keine Rohbilder für den Simulator)")
    lines = card_lines()
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "karten.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    esc = lambda s: s.replace("\\", "\\\\").replace('"', '\\"')
    with open(os.path.join(ROOT, "tiles", "album_builtin.h"), "w", encoding="utf-8") as f:
        f.write("// Erzeugt von tools/horses.py build aus tools/horses/pferde.json, nicht von Hand ändern.\n")
        f.write("#pragma once\n\nnamespace dk {\n\nstatic const char ALBUM_BUILTIN[] =\n")
        f.write("\n".join(f'    "{esc(l)}\\n"' for l in lines) + ";\n\n}  // namespace dk\n")
    print(f"✓ tiles/album_builtin.h: {len(lines) - 1} Karten")


def cmd_review():
    from PIL import Image, ImageDraw, ImageFont
    cr = credits()
    ps = load()
    cols, cw, ch = 4, CARD_W, CARD_H + 54
    sheet = Image.new("RGB", (cols * (cw + 10) + 10, ((len(ps) + cols - 1) // cols) * (ch + 10) + 10), (40, 40, 48))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 13)
    except OSError:
        font = ImageFont.load_default()
    for i, p in enumerate(ps):
        x, y = 10 + (i % cols) * (cw + 10), 10 + (i // cols) * (ch + 10)
        path = os.path.join(OUT, f"{p['id']}.jpg")
        if os.path.exists(path):
            sheet.paste(Image.open(path), (x, y))
        else:
            d.rectangle((x, y, x + cw, y + CARD_H), fill=(80, 30, 30))
        c = cr.get(str(p["id"]), {})
        d.text((x, y + CARD_H + 4), f"{p['id']}. {p['name']}", fill=(255, 255, 255), font=font)
        d.text((x, y + CARD_H + 20), clean_author(c.get("urheber", "")) if c else "kein Foto", fill=(180, 180, 200), font=font)
        d.text((x, y + CARD_H + 36), c.get("lizenz", ""), fill=(180, 180, 200), font=font)
    path = os.path.join(OUT, "uebersicht.png")
    sheet.save(path)
    print(f"✓ {path}")


def cmd_upload(port=None):
    sys.path.insert(0, HERE)
    import quiz   # serielle Hilfen
    s = quiz.open_port(port)
    files = [(f"/pferde/{os.path.basename(f)}", f) for f in sorted(glob.glob(os.path.join(OUT, "*.jpg")))]
    cmd_build()
    files.append(("/pferde/karten.txt", os.path.join(OUT, "karten.txt")))
    for dest, src in files:
        data = open(src, "rb").read()
        quiz.send(s, json.dumps({"file": "begin", "path": dest, "size": len(data)}, separators=(",", ":")), expect="file ok")
        for i in range(0, len(data), 512):
            quiz.send(s, "F " + base64.b64encode(data[i:i + 512]).decode(), expect="file ok")
        r = quiz.send(s, json.dumps({"file": "end"}, separators=(",", ":")), expect="file fertig")
        print(f"✓ {dest} ({len(data) // 1024} KB) {r}")
    print("✓", quiz.send(s, json.dumps({"album": "reload"}, separators=(",", ":")), expect="album"))


if __name__ == "__main__":
    a = sys.argv[1:]
    cmds = {"fetch": cmd_fetch, "review": cmd_review, "build": cmd_build}
    if a and a[0] in cmds:
        cmds[a[0]]()
    elif a and a[0] == "import" and len(a) > 1:
        cmd_import(a[1])
    elif a and a[0] == "upload":
        cmd_upload(a[1] if len(a) > 1 else None)
    else:
        sys.exit(__doc__)
