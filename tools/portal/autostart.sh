#!/bin/sh
# Eltern-Portal beim Anmelden am Mac automatisch starten (LaunchAgent).
#   tools/portal/autostart.sh        einrichten und starten
#   tools/portal/autostart.sh aus    wieder entfernen
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PLIST="$HOME/Library/LaunchAgents/de.grummel.portal.plist"

if [ "$1" = "aus" ]; then
  launchctl unload "$PLIST" 2>/dev/null || true
  rm -f "$PLIST"
  echo "Autostart entfernt."
  exit 0
fi

mkdir -p "$HOME/Library/LaunchAgents" "$ROOT/tools/portal/daten"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>de.grummel.portal</string>
  <key>ProgramArguments</key>
  <array><string>$ROOT/tools/.venv/bin/python</string><string>$ROOT/tools/portal/portal.py</string></array>
  <key>EnvironmentVariables</key><dict><key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>$ROOT/tools/portal/daten/portal.log</string>
  <key>StandardOutPath</key><string>$ROOT/tools/portal/daten/portal.log</string>
</dict>
</plist>
EOF
launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"
echo "Autostart eingerichtet: http://127.0.0.1:8770"
