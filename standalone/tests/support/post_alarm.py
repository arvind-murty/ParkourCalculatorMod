"""Shared test builders extracted from tests.unit.numerics.test_post_alarm: its alarmed fixture requests."""
import gzip
import json
import pathlib

from parkour_solver.numerics import prepare

# One alarmed request per study pipeline outcome (research/post-alarm/README.md), and a Real11 request whose most short
# tick is not a jump tick.
FIXTURE = pathlib.Path(__file__).resolve().parents[1]/'fixtures'/'post_alarm'/'alarmed-requests.json.gz'


def alarmed(kind):
    with gzip.open(FIXTURE, 'rt') as stream:
        row = json.load(stream)['requests'][kind]
    return prepare(dict(row['problem'], objectiveMode='configured'))
