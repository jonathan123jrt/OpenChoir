#!/bin/bash
# Double-click to install OpenChoir for the current user:
#   - OpenChoir (AI choir instrument) and OpenChoir Ensemble (real-time effect), VST3 + AU
#   - the AI engine scripts, into ~/Library/Application Support/OpenChoir/ai
# then optionally runs the one-time AI setup (~6 GB download).
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
AI_DIR="$HOME/Library/Application Support/OpenChoir/ai"
mkdir -p "$VST3_DIR" "$AU_DIR" "$AI_DIR"

for p in "OpenChoir" "OpenChoir Ensemble"; do
    rm -rf "$VST3_DIR/$p.vst3" "$AU_DIR/$p.component"
    cp -R "$p.vst3"      "$VST3_DIR/"
    cp -R "$p.component" "$AU_DIR/"
    # Open-source build, not notarised by Apple: clear the "downloaded" flag that would block it
    xattr -dr com.apple.quarantine "$VST3_DIR/$p.vst3" "$AU_DIR/$p.component" 2>/dev/null || true
done
xattr -dr com.apple.quarantine "OpenChoir.app" "OpenChoir Ensemble.app" 2>/dev/null || true

# AI engine scripts (keeps an existing Python environment, models and voices)
cp -f "OpenChoir AI/"*.py "OpenChoir AI/requirements.txt" "OpenChoir AI/README.md" "$AI_DIR/"
cp -f "OpenChoir AI/"*.command "$AI_DIR/"
chmod +x "$AI_DIR/"*.command
xattr -dr com.apple.quarantine "$AI_DIR" 2>/dev/null || true

# Make Logic / GarageBand notice the new Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Installed:"
echo "  OpenChoir           (instrument)  VST3 + AU"
echo "  OpenChoir Ensemble  (effect)      VST3 + AU"
echo "  AI engine scripts -> $AI_DIR"
echo ""
if [ -x "$AI_DIR/.venv/bin/python" ] && [ -f "$AI_DIR/voices/vocalset/voices.json" ]; then
    echo "The AI engine is already set up. Updating its voice bank if needed..."
    (cd "$AI_DIR" && .venv/bin/python fetch_voices.py) || true
    echo "Restart your DAW and load OpenChoir on an instrument track."
else
    read -r -p "Set up the AI engine now? It downloads ~6 GB and takes 10-30 minutes. [Y/n] " A
    if [ "$A" != "n" ] && [ "$A" != "N" ]; then
        bash "$AI_DIR/setup.command"
    else
        echo "You can do it later from the plugin (Choir screen > Install the AI engine)."
    fi
fi
read -r -p "Press Return to close."
