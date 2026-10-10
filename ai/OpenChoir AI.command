#!/bin/bash
# OpenChoir AI without a DAW - same choices as the plugin's Choir screen.
cd "$(dirname "$0")"
export PYTORCH_ENABLE_MPS_FALLBACK=1

if [ ! -x .venv/bin/python ] || [ ! -f voices/vocalset/voices.json ]; then
    echo "First run: setting things up..."
    bash ./setup.command || { echo "Setup failed - see the messages above."; read -r -p "Press Return to close."; exit 1; }
else
    .venv/bin/python fetch_voices.py >/dev/null || .venv/bin/python fetch_voices.py   # upgrades an old voice bank
fi

echo
echo "=================  OpenChoir AI  ================="
echo "Export your lead vocal as a WAV (dry, no reverb, one singer)."
echo "Drag the file into this window, then press Return:"
read -r RAW
VOCAL=$(.venv/bin/python -c 'import sys,shlex; s=sys.argv[1].strip(); print(shlex.split(s)[0] if s else "")' "$RAW")
if [ ! -f "$VOCAL" ]; then echo "Couldn't find that file: $VOCAL"; read -r -p "Press Return to close."; exit 1; fi

echo
echo "Choir:  1) Gospel Choir   2) Treble Choir   3) Shout Choir"
read -r -p "Number [1]: " M
case "$M" in 2) MODEL=treble ;; 3) MODEL=shout ;; *) MODEL=gospel ;; esac

LAYERS=()
read -r -p "Add Octave Up?   [Y/n] " A; [ "$A" != "n" ] && [ "$A" != "N" ] && LAYERS+=(oct_up)
read -r -p "Add Octave Down? [Y/n] " A; [ "$A" != "n" ] && [ "$A" != "N" ] && LAYERS+=(oct_down)
echo "Harmonies (in key):  1) 3rd above  2) 3rd below  3) 5th above  4) 5th below"
read -r -p "Numbers separated by spaces, or Return for none: " H
for h in $H; do
    case "$h" in 1) LAYERS+=(third_up) ;; 2) LAYERS+=(third_down) ;; 3) LAYERS+=(fifth_up) ;; 4) LAYERS+=(fifth_down) ;; esac
done
read -r -p "Key, e.g. \"A minor\" (Return = detect automatically): " KEY
[ -z "$KEY" ] && KEY=auto
read -r -p "Stereo or mono output? [S/m] " SM
read -r -p "Quality: 1) draft  2) standard  3) high  [2]: " Q
case "$Q" in 1) QUALITY=draft ;; 3) QUALITY=high ;; *) QUALITY=standard ;; esac

ARGS=(--model "$MODEL" --key "$KEY" --quality "$QUALITY" --layers "${LAYERS[@]}")
[ "$SM" = "m" ] || [ "$SM" = "M" ] && ARGS+=(--mono)

echo
.venv/bin/python choir_ai.py "$VOCAL" "${ARGS[@]}" 2>&1 | grep -v "^@@PROGRESS" | sed 's/^@@[A-Z]* //' && \
    open "$(dirname "$VOCAL")/$(basename "${VOCAL%.*}") - OpenChoir $MODEL"
echo
read -r -p "Press Return to close."
