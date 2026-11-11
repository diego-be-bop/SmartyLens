#!/bin/bash
# Prova dell'interfaccia su uno schermo virtuale 1280x800 (eseguire con xvfb-run).
# Avvia Smarty Lens, lo attiva con un clic, simula un testo evidenziato e verifica che compaia il riquadro.
set -u
export GDK_BACKEND=x11
BIN="$(cd "$(dirname "$0")/.." && pwd)/smartylens"
WORK="$(mktemp -d)"; cp "$BIN" "$WORK/"; cd "$WORK"
./smartylens 2>/dev/null &
PID=$!
sleep 2
xdotool mousemove 1228 290 click 1          # l'icona nasce sul bordo destro, a un terzo dell'altezza
sleep 1
xdotool mousemove 300 200
printf 'np.linspace(0, 1, 5)' | xclip -selection primary -loops 0 &
sleep 8
import -window root "${SHOT:-/tmp/smartylens-test.png}" 2>/dev/null || true
# il riquadro e' una finestra larga 500 px; l'icona e' 48x48
WINS="$(xwininfo -root -tree | grep -i smartylens || true)"
echo "$WINS"
kill $PID 2>/dev/null; pkill xclip 2>/dev/null
echo "$WINS" | grep -q ' 48x48+' || { echo "ERRORE: icona non trovata"; exit 1; }
echo "$WINS" | grep -qE ' 500x[0-9]{3}\+' || { echo "ERRORE: riquadro non comparso"; exit 1; }
echo "OK: icona e riquadro presenti"
