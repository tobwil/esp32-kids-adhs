#!/usr/bin/env python3
"""Neues Fragenpaket fürs Orakel erzeugen und jede Frage mit Websuche prüfen.

  tools/.venv/bin/python tools/quiz.py generate quiz/2026-11-a.json --anzahl 50
  … --kategorie Tiere --kategorie "Franken und Coburg" --schwierigkeit fies

Ablauf (wie im Plan):
  1. Erzeugen: Claude schreibt Fragen als JSON (festes Schema, bekannte Fragen aus quiz/*.json werden ausgeschlossen).
  2. Prüfen: je fünf Fragen mit Websuche. Pro Frage ein Urteil: bestätigt, korrigiert, als Mythos umformuliert
     oder verworfen, immer mit Quelle. Ohne belastbare Quelle fliegt die Frage raus.
  3. Kuratieren: Die Ausgabedatei durchsehen, Unpassendes löschen, dann `quiz.py upload`.

Zugang: ANTHROPIC_API_KEY (oder `ant auth login`). Für ein AI Gateway zusätzlich ANTHROPIC_BASE_URL setzen,
das SDK leitet dann alle Aufrufe dorthin. Kosten: Tokens plus die Websuchen (max. 6 pro Fünfergruppe).
"""
import argparse
import glob
import json
import os
import sys

import anthropic

import quiz  # Prüfregeln und Zeichensatz des Geräts

MODEL = "claude-opus-5-5"
# Lehnen Sicherheitsfilter eine Anfrage ab, rechnet die API sie mit einem passenden Ersatzmodell nach
FALLBACK = {"betas": ["server-side-fallback-2026-07-01"], "fallbacks": "default"}
KATEGORIEN = ["Tiere", "Geschichte", "Technik", "Essen", "Sprache", "Franken und Coburg"]

STIL = f"""Regeln für jede Frage:
- Deutsch, locker, für eine kurze Denkpause am Schreibtisch. Unnützes, aber verblüffendes Wissen.
- Genau drei Antworten, eine davon richtig, die beiden anderen plausibel, aber eindeutig falsch.
- Frage höchstens {quiz.LIMITS['frage']} Zeichen, jede Antwort höchstens {quiz.LIMITS['antwort']} Zeichen,
  Erklärung ein bis zwei Sätze mit höchstens {quiz.LIMITS['erklaerung']} Zeichen: warum die Antwort stimmt.
- Nur Fakten, die sich mit einer seriösen Quelle belegen lassen. Keine Zahlen, die sich laufend ändern.
- Verbreitete Irrtümer (etwa „Goldfische vergessen nach drei Sekunden“) als Mythos-Frage stellen, nicht als Fakt.
- Kein senkrechter Strich |, keine Emojis, nur deutsche Sonderzeichen (ä ö ü ß, „ “ – …).
- Die Position der richtigen Antwort ist egal, das Gerät mischt sie."""

FRAGE_SCHEMA = {
    "type": "object",
    "properties": {
        "kategorie": {"type": "string"},
        "schwierigkeit": {"type": "string", "enum": ["leicht", "mittel", "fies"]},
        "frage": {"type": "string"},
        "antworten": {"type": "array", "items": {"type": "string"}},
        "richtig": {"type": "integer", "enum": [0, 1, 2]},
        "erklaerung": {"type": "string"},
    },
    "required": ["kategorie", "schwierigkeit", "frage", "antworten", "richtig", "erklaerung"],
    "additionalProperties": False,
}

URTEIL_TOOL = {
    "name": "urteil",
    "description": "Ergebnis der Prüfung einer einzelnen Frage melden. Für jede geprüfte Frage genau einmal aufrufen.",
    "strict": True,
    "input_schema": {
        "type": "object",
        "properties": {
            "nummer": {"type": "integer", "description": "Nummer der Frage aus der Liste"},
            "ergebnis": {"type": "string", "enum": ["bestaetigt", "korrigiert", "mythos", "verworfen"]},
            "begruendung": {"type": "string", "description": "Ein Satz, was die Quelle sagt"},
            "quelle": {"type": "string", "description": "URL der belastbarsten Quelle, leer bei verworfen"},
            "frage": FRAGE_SCHEMA,
        },
        "required": ["nummer", "ergebnis", "begruendung", "quelle", "frage"],
        "additionalProperties": False,
    },
}


def text_of(msg):
    return "".join(b.text for b in msg.content if b.type == "text")


def check_stop(msg, what):
    if msg.stop_reason == "refusal":
        cat = msg.stop_details.category if msg.stop_details else None
        sys.exit(f"{what}: Anfrage abgelehnt (Kategorie {cat})")
    if msg.stop_reason == "max_tokens":
        sys.exit(f"{what}: Antwort abgeschnitten (max_tokens), weniger Fragen pro Lauf anfordern")


def known_questions():
    out = []
    for p in glob.glob(os.path.join(quiz.ROOT, "quiz", "*.json")):
        try:
            out += [q["frage"] for q in quiz.load(p)[1]]
        except (OSError, KeyError, ValueError):
            pass
    return out


def generate(client, anzahl, kategorien, schwierigkeit):
    bekannt = known_questions()
    prompt = (
        f"Erzeuge {anzahl} Quizfragen für ein Schreibtisch-Orakel, verteilt auf diese Kategorien: {', '.join(kategorien)}.\n"
        f"Schwierigkeit: {schwierigkeit if schwierigkeit != 'gemischt' else 'gemischt, etwa 40 % leicht, 40 % mittel, 20 % fies'}.\n\n"
        f"{STIL}\n\nDiese Fragen gibt es schon, bitte keine davon und nichts sinngleiches:\n"
        + "\n".join(f"- {q}" for q in bekannt)
    )
    with client.beta.messages.stream(
        model=MODEL,
        max_tokens=64000,
        output_config={
            "effort": "high",
            "format": {
                "type": "json_schema",
                "schema": {
                    "type": "object",
                    "properties": {"fragen": {"type": "array", "items": FRAGE_SCHEMA}},
                    "required": ["fragen"],
                    "additionalProperties": False,
                },
            },
        },
        messages=[{"role": "user", "content": prompt}],
        **FALLBACK,
    ) as stream:
        msg = stream.get_final_message()
    check_stop(msg, "Erzeugen")
    return json.loads(text_of(msg))["fragen"]


def verify_batch(client, batch):
    liste = "\n\n".join(f"Nummer {i}:\n{json.dumps(q, ensure_ascii=False)}" for i, q in enumerate(batch))
    prompt = (
        "Prüfe jede dieser Quizfragen mit der Websuche. Suche für jede Frage mindestens eine seriöse Quelle "
        "(Lexikon, Behörde, Museum, Fachartikel, große Zeitung), die die richtige Antwort und die Erklärung stützt.\n"
        "- Stimmt alles: ergebnis bestaetigt.\n"
        "- Ist ein Detail falsch oder schief, aber die Frage rettbar: korrigiere Frage, Antworten oder Erklärung, ergebnis korrigiert.\n"
        "- Ist der Fakt umstritten oder ein verbreiteter Irrtum: als Mythos-Frage umformulieren, ergebnis mythos.\n"
        "- Ohne belastbare Quelle oder wenn eine falsche Antwort ebenfalls stimmen könnte: ergebnis verworfen, quelle leer.\n"
        f"Korrigierte Fragen halten dieselben Regeln ein:\n{STIL}\n\n"
        "Melde jedes Ergebnis mit dem Werkzeug urteil, genau einmal pro Nummer, auch für verworfene.\n\n" + liste
    )
    messages = [{"role": "user", "content": prompt}]
    tools = [{"type": "web_search_20260209", "name": "web_search", "max_uses": 6}, URTEIL_TOOL]
    for _ in range(6):   # pause_turn: der Server setzt nach langen Suchen fort, höchstens fünfmal
        with client.beta.messages.stream(
            model=MODEL, max_tokens=32000, output_config={"effort": "high"}, tools=tools, messages=messages, **FALLBACK
        ) as stream:
            msg = stream.get_final_message()
        check_stop(msg, "Prüfen")
        if msg.stop_reason != "pause_turn":
            break
        messages.append({"role": "assistant", "content": msg.content})
    urteile = {}
    for b in msg.content:
        if b.type == "tool_use" and b.name == "urteil":
            u = b.input if isinstance(b.input, dict) else json.loads(b.input)
            if isinstance(u.get("nummer"), int) and 0 <= u["nummer"] < len(batch):
                urteile[u["nummer"]] = u
    return urteile


def main(argv):
    ap = argparse.ArgumentParser(prog="quiz.py generate", description="Fragenpaket erzeugen und prüfen")
    ap.add_argument("ausgabe")
    ap.add_argument("--anzahl", type=int, default=50)
    ap.add_argument("--kategorie", action="append", help=f"mehrfach möglich, Standard: alle ({', '.join(KATEGORIEN)})")
    ap.add_argument("--schwierigkeit", default="gemischt", choices=["gemischt", "leicht", "mittel", "fies"])
    a = ap.parse_args(argv)
    if os.path.exists(a.ausgabe):
        sys.exit(f"{a.ausgabe} gibt es schon")

    client = anthropic.Anthropic()   # ANTHROPIC_API_KEY / ANTHROPIC_BASE_URL aus der Umgebung
    try:
        print(f"Erzeuge {a.anzahl} Fragen …", flush=True)
        roh = generate(client, a.anzahl, a.kategorie or KATEGORIEN, a.schwierigkeit)
        print(f"{len(roh)} Entwürfe, prüfe mit Websuche …", flush=True)
        fertig, zaehler = [], {"bestaetigt": 0, "korrigiert": 0, "mythos": 0, "verworfen": 0, "ohne urteil": 0}
        for start in range(0, len(roh), 5):
            batch = roh[start:start + 5]
            urteile = verify_batch(client, batch)
            for i in range(len(batch)):
                u = urteile.get(i)
                if not u:
                    zaehler["ohne urteil"] += 1
                    continue
                zaehler[u["ergebnis"]] += 1
                if u["ergebnis"] == "verworfen" or not u["quelle"].startswith("http"):
                    continue
                q = dict(u["frage"])
                q["quelle"] = u["quelle"]
                q["pruefung"] = f"{u['ergebnis']}: {u['begruendung']}"
                fertig.append(q)
            print(f"  {min(start + 5, len(roh))}/{len(roh)} geprüft, {len(fertig)} behalten", flush=True)
    except anthropic.AuthenticationError:
        sys.exit("Kein gültiger Zugang: ANTHROPIC_API_KEY setzen (oder ANTHROPIC_BASE_URL fürs Gateway prüfen)")
    except anthropic.RateLimitError:
        sys.exit("Ratenlimit erreicht, später noch einmal versuchen")
    except anthropic.APIStatusError as e:
        sys.exit(f"API-Fehler {e.status_code}: {e.message}")
    except anthropic.APIConnectionError:
        sys.exit("Keine Verbindung zur API")

    # Regeln des Geräts: was trotzdem nicht passt, fliegt raus
    ok = [q for q in fertig if not quiz.problems([q])]
    pack = os.path.splitext(os.path.basename(a.ausgabe))[0]
    with open(a.ausgabe, "w", encoding="utf-8") as f:
        json.dump({"pack": pack, "hinweis": f"Erzeugt mit {MODEL}, geprüft per Websuche. Vor dem Upload durchsehen.", "fragen": ok},
                  f, ensure_ascii=False, indent=2)
    print("Ergebnis: " + ", ".join(f"{k} {v}" for k, v in zaehler.items()) + f", {len(fertig) - len(ok)} verletzen Geräteregeln")
    print(f"✓ {len(ok)} Fragen in {a.ausgabe}. Durchsehen, dann: tools/.venv/bin/python tools/quiz.py upload {a.ausgabe}")
