import os
import shutil
import sys

REPO_ROOT  = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BROKER_DIR = os.path.join(REPO_ROOT, "broker")

sys.path.insert(0, BROKER_DIR)

# broker/main.py reads broker/config.toml at import time. Tests must work in a
# fresh checkout where config.toml hasn't been created yet, so seed it from the
# tracked sample when missing.
_cfg = os.path.join(BROKER_DIR, "config.toml")
if not os.path.exists(_cfg):
    shutil.copy(os.path.join(BROKER_DIR, "config.sample.toml"), _cfg)
