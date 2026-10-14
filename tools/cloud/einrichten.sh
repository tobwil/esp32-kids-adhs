#!/bin/sh
# Eltern-Portal in der Cloud einrichten (einmalig, du führst das selbst aus):
#   1. bei Cloudflare anmelden (Browser öffnet sich, kostenloses Konto reicht)
#   2. Speicher in der EU anlegen (R2-Bucket „grummel-portal“). R2 muss im Cloudflare-Dashboard einmal freigeschaltet sein.
#   3. Portal veröffentlichen
#   4. Gerät-Schlüssel erzeugen und als Geheimnis ablegen, Eltern-Passwort festlegen (wird abgefragt, nicht angezeigt)
# Mehrfach ausführen ist in Ordnung: vorhandener Speicher bleibt, Geheimnisse werden ersetzt.
set -e
cd "$(dirname "$0")"
[ -d node_modules ] || npm install

npx wrangler whoami >/dev/null 2>&1 || npx wrangler login

if npx wrangler r2 bucket list --jurisdiction eu 2>&1 | grep -q "grummel-portal"; then
  echo "Speicher „grummel-portal“ gibt es schon."
else
  out=$(npx wrangler r2 bucket create grummel-portal --jurisdiction eu 2>&1) || {
    echo "$out" | grep -q "enable R2" && {
      echo
      echo "R2 ist in deinem Cloudflare-Konto noch nicht freigeschaltet."
      echo "Im Dashboard links „R2 Object Storage“ öffnen und den kostenlosen Plan aktivieren, dann dieses Skript noch einmal starten."
      exit 1
    }
    echo "$out"
    exit 1
  }
  echo "Speicher „grummel-portal“ in der EU angelegt."
fi

npx wrangler deploy

# Gerät-Schlüssel: zufällig, geht direkt an Cloudflare (steht später im Portal unter „Gerät verbinden“)
openssl rand -hex 24 | tr -d '\n' | npx wrangler secret put GERAET_SCHLUESSEL
echo
echo "Jetzt das Eltern-Passwort für das Portal festlegen (lang und nur für euch):"
npx wrangler secret put ELTERN_PASSWORT

echo
echo "Fertig. Die Adresse oben (https://grummel-portal.….workers.dev) im Browser öffnen und anmelden."
echo "Gerät verbinden: im Portal unter „Gerät verbinden“ Adresse und Schlüssel kopieren und auf der Einrichtungsseite des Geräts eintragen."
