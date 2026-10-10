#!/bin/bash
# OpenChoir AI - one-time setup for macOS. Double-click (or right-click > Open).
# Installs a private Python, Seed-VC, the VocalSet voice bank and the AI models.
# Everything stays inside this folder. The OpenChoir plugin looks for it in
# ~/Library/Application Support/OpenChoir/ai (install.command puts it there).
set -e
cd "$(dirname "$0")"

SEEDVC_COMMIT="51383efd921027683c89e5348211d93ff12ac2a8"

echo "=== OpenChoir AI setup ==="
echo "Needs ~6 GB of disk space and a good internet connection. This takes 10-30 minutes."
echo

# 1. uv: a self-contained Python installer (no Homebrew or system Python needed)
export PATH="$HOME/.local/bin:$PATH"
if ! command -v uv >/dev/null 2>&1; then
    echo "-> Installing uv (Python manager)..."
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
fi

# 2. Seed-VC source (pinned version)
if [ ! -f seed-vc/seed_vc_wrapper.py ]; then
    echo "-> Downloading Seed-VC..."
    rm -rf seed-vc seedvc.zip "seed-vc-$SEEDVC_COMMIT"
    curl -L -o seedvc.zip "https://github.com/Plachtaa/seed-vc/archive/$SEEDVC_COMMIT.zip"
    unzip -q seedvc.zip
    mv "seed-vc-$SEEDVC_COMMIT" seed-vc
    rm seedvc.zip
fi

# 3. Python 3.10 environment + packages
if [ ! -x .venv/bin/python ]; then
    echo "-> Creating Python 3.10 environment..."
    uv venv --python 3.10 .venv
fi
echo "-> Installing packages (PyTorch etc.)..."
uv pip install --python .venv/bin/python -r requirements.txt

# 4. Voice bank: 20 consenting singers from VocalSet (CC BY 4.0)
echo "-> Fetching singer voices from VocalSet (~250 MB)..."
.venv/bin/python fetch_voices.py

# 5. Download the AI models now so the first render starts straight away
echo "-> Downloading AI models (~2.5 GB, one time)..."
PYTORCH_ENABLE_MPS_FALLBACK=1 .venv/bin/python -c "
import render_core
render_core.start_engine('auto', 'auto')
print('Models ready.')
"

chmod +x "OpenChoir AI.command" 2>/dev/null || true
echo
echo "=== Setup complete ==="
echo "Open the OpenChoir plugin in your DAW (on an instrument track) and drop in a vocal."
echo "You can also double-click 'OpenChoir AI.command' to render without a DAW."
