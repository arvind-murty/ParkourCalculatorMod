"""Canonical identity shared by all runtime layers.

Search order breaks ties on these digests and checkpoints persist them, so any
replacement implementation must reproduce these exact bytes: sorted keys, the
default separators, ASCII escapes, float repr, and int distinct from float.
"""
import hashlib
import json


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False).encode()).hexdigest()
