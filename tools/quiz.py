#!/usr/bin/env python3
"""Fragenpakete fürs Orakel.

  quiz.py check   <paket.json>            Aufbau, Längen und Zeichen prüfen
  quiz.py build   <paket.json>            als eingebautes Paket nach core/quiz_builtin.h schreiben
  quiz.py upload  <paket.json> [port]     per USB aufs Gerät übertragen (ersetzt das eingebaute Paket)
  quiz.py reset   [port]                  übertragenes Paket löschen, Gerät nutzt wieder das eingebaute
  quiz.py generate <ausgabe.json> [--anzahl 50] [--kategorie Tiere] [--schwierigkeit mittel]
                                          neues Paket erzeugen und mit Websuche prüfen (siehe quiz_generate.py)

Zeilenformat auf dem Gerät (eine Frage pro Zeile, | als Trenner):
  kategorie|schwierigkeit(0 leicht, 1 mittel, 2 fies)|frage|antwort0|antwort1|antwort2|richtig(0-2)|erklärung
"""
import glob
import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIFF = {"leicht": 0, "mittel": 1, "fies": 2}
# Zeichen, die die Schriften auf dem Gerät kennen (tools/mkfont.swift, Liste „ascii“)
CHARSET = set(chr(c) for c in range(32, 127)) | set("ÄÖÜäöüß×·–„“”…°€éèáàâñçÉ")
LIMITS = {"frage": 110, "antwort": 30, "erklaerung": 170, "kategorie": 24}


def load(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    return d.get("pack") or os.path.splitext(os.path.basename(path))[0], d["fragen"]


def problems(fragen):
    out, seen = [], set()
    for i, q in enumerate(fragen):
        where = f"Frage {i + 1} ({q.get('frage', '?')[:40]})"
        for key in ("kategorie", "schwierigkeit", "frage", "antworten", "richtig", "erklaerung"):
            if key not in q:
                out.append(f"{where}: Feld „{key}“ fehlt")
        if any(k not in q for k in ("frage", "antworten", "richtig", "erklaerung", "kategorie")):
            continue
        if q.get("schwierigkeit") not in DIFF:
            out.append(f"{where}: Schwierigkeit muss leicht, mittel oder fies sein")
        if len(q["antworten"]) != 3:
            out.append(f"{where}: genau drei Antworten nötig")
        if q["richtig"] not in (0, 1, 2):
            out.append(f"{where}: „richtig“ muss 0, 1 oder 2 sein")
        if len(set(a.strip().lower() for a in q["antworten"])) != len(q["antworten"]):
            out.append(f"{where}: Antworten doppelt")
        texts = [("frage", q["frage"]), ("erklaerung", q["erklaerung"]), ("kategorie", q["kategorie"])] + [("antwort", a) for a in q["antworten"]]
        for kind, t in texts:
            if len(t) > LIMITS[kind]:
                out.append(f"{where}: {kind} zu lang ({len(t)} > {LIMITS[kind]})")
            bad = sorted(set(c for c in t if c not in CHARSET))
            if bad:
                out.append(f"{where}: Zeichen fehlen in der Schrift: {''.join(bad)}")
            if "|" in t or "\n" in t:
                out.append(f"{where}: | und Zeilenumbruch sind nicht erlaubt")
        key = q["frage"].strip().lower()
        if key in seen:
            out.append(f"{where}: Frage doppelt")
        seen.add(key)
    return out


def lines(fragen):
    for q in fragen:
        yield "|".join([q["kategorie"], str(DIFF[q["schwierigkeit"]]), q["frage"], *q["antworten"], str(q["richtig"]), q["erklaerung"]])


def check_or_exit(path):
    pack, fragen = load(path)
    p = problems(fragen)
    for line in p:
        print("✗", line)
    if p:
        sys.exit(f"{len(p)} Problem(e) in {path}")
    return pack, fragen


def cmd_check(path):
    pack, fragen = check_or_exit(path)
    cats = {}
    for q in fragen:
        cats[q["kategorie"]] = cats.get(q["kategorie"], 0) + 1
    print(f"✓ {pack}: {len(fragen)} Fragen, " + ", ".join(f"{k} {v}" for k, v in cats.items()))


def cmd_build(path):
    pack, fragen = check_or_exit(path)
    def esc(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')
    body = [f'    "#pack {esc(pack)}\\n"'] + [f'    "{esc(l)}\\n"' for l in lines(fragen)]
    out = os.path.join(ROOT, "core", "quiz_builtin.h")
    with open(out, "w", encoding="utf-8") as f:
        f.write(f"// Erzeugt von tools/quiz.py build {os.path.relpath(path, ROOT)}, nicht von Hand ändern.\n")
        f.write("#pragma once\n\nnamespace dk {\n\nstatic const char QUIZ_BUILTIN[] =\n")
        f.write("\n".join(body) + ";\n\n}  // namespace dk\n")
    print(f"✓ {out}: {len(fragen)} Fragen")


def open_port(port):
    import serial
    port = port or (glob.glob("/dev/cu.usbmodem*") or ["/dev/cu.usbmodem13101"])[0]
    s = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def send(s, line, expect="quiz ok", timeout=3.0):
    s.write((line + "\n").encode("utf-8"))
    end = time.time() + timeout
    while time.time() < end:
        r = s.readline().decode("utf-8", errors="replace").strip()
        if r.startswith(expect):
            return r
        if r.startswith("quiz fehler"):
            sys.exit(f"Gerät meldet: {r}")
    sys.exit(f"keine Antwort „{expect}“ auf: {line[:60]}")


def cmd_upload(path, port=None):
    pack, fragen = check_or_exit(path)
    s = open_port(port)
    send(s, json.dumps({"quiz": "begin", "pack": pack, "count": len(fragen)}, separators=(",", ":")))
    for i, l in enumerate(lines(fragen)):
        send(s, "Q " + l)
        print(f"\r{i + 1}/{len(fragen)}", end="", flush=True)
    r = send(s, json.dumps({"quiz": "end"}, separators=(",", ":")), expect="quiz fertig", timeout=5)
    print(f"\n✓ {r}")


def cmd_reset(port=None):
    s = open_port(port)
    print("✓", send(s, json.dumps({"quiz": "reset"}, separators=(",", ":")), expect="quiz fertig"))


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == "check" and len(a) > 1:
        cmd_check(a[1])
    elif a[0] == "build" and len(a) > 1:
        cmd_build(a[1])
    elif a[0] == "upload" and len(a) > 1:
        cmd_upload(a[1], a[2] if len(a) > 2 else None)
    elif a[0] == "reset":
        cmd_reset(a[1] if len(a) > 1 else None)
    elif a[0] == "generate":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import quiz_generate
        quiz_generate.main(a[1:])
    else:
        sys.exit(__doc__)
