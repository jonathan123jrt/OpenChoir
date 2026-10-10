"""Entry point the OpenChoir plugin runs:  python openchoir_job.py job.json

Prints machine-readable progress lines (@@STATUS, @@PROGRESS frac eta msg,
@@KEY, @@LAYER id path, @@DONE result.json, @@ERROR msg) for the plugin.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

import render_core  # noqa: E402

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: openchoir_job.py job.json")
        sys.exit(1)
    sys.exit(render_core.run_job_file(sys.argv[1]))
