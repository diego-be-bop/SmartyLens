#!/bin/bash
# Prova dell'interfaccia su uno schermo virtuale 1280x800 (eseguire con xvfb-run).
# Avvia Smarty Lens, lo attiva con un clic, simula un testo evidenziato e verifica che compaia il riquadro.
set -u
export GDK_BACKEND=x11
BIN="$(cd "$(dirname "$0")/.." && pwd)/smartylens"
WORK="$(mktemp -d)"; cp "$BIN" "$WORK/"
# riusa gli indici gia' scaricati (se presenti), cosi' la prova non dipende dalla velocita' della rete
[ -d "$(dirname "$BIN")/dati/indici" ] && mkdir -p "$WORK/dati" && cp -r "$(dirname "$BIN")/dati/indici" "$WORK/dati/"
cd "$WORK"
./smartylens 2>/dev/null &
PID=$!
sleep 2
xdotool mousemove 1228 290 click 1          # l'icona nasce sul bordo destro, a un terzo dell'altezza
sleep 1
xdotool mousemove 300 200
printf 'np.linspace(0, 1, 5)' | xclip -selection primary -loops 0 &
# attende il riquadro (finestra larga 500 px) fino a 40 secondi; l'icona e' 48x48
for i in $(seq 1 40); do
    sleep 1
    WINS="$(xwininfo -root -tree | grep -i smartylens || true)"
    echo "$WINS" | grep -qE ' 500x[0-9]{3}\+' && break
done
import -window root "${SHOT:-/tmp/smartylens-test.png}" 2>/dev/null || true
echo "$WINS"
kill $PID 2>/dev/null; pkill xclip 2>/dev/null
echo "$WINS" | grep -q ' 48x48+' || { echo "ERRORE: icona non trovata"; exit 1; }
echo "$WINS" | grep -qE ' 500x[0-9]{3}\+' || { echo "ERRORE: riquadro non comparso"; exit 1; }
echo "OK: icona e riquadro presenti"
