#!/bin/bash
# Prova dell'interfaccia su macOS: avvia l'app chiedendole di aprire il riquadro per np.linspace
# e verifica che il riquadro sia stato mostrato. (La lettura del testo evidenziato richiede il
# permesso Accessibilita', che una prova automatica non puo' concedere.)
set -u
cd "$(dirname "$0")/.."
OUT="$(mktemp -d)/esito.txt"
SMARTYLENS_SELFTEST="$OUT" ./SmartyLens.app/Contents/MacOS/SmartyLens --cerca "np.linspace(0, 1, 5)" &
PID=$!
for i in $(seq 1 40); do sleep 1; [ -s "$OUT" ] && break; done
sleep 1
screencapture -x "${SHOT:-/tmp/smartylens-mac.png}" 2>/dev/null || true
kill $PID 2>/dev/null
cat "$OUT" 2>/dev/null
grep -q "titolo=numpy.linspace" "$OUT" 2>/dev/null || { echo "ERRORE: riquadro non comparso"; exit 1; }
echo "OK: riquadro mostrato"
