"""Primitive request validators shared by all runtime layers."""
import math

TOLERANCE = 1e-7
# The solver's rows keep this far inside what they plan (regions shrink, avoided sides grow, a planned contact
# crosses its face by it), so floating-point error never makes the replay disagree with the plan.
SOLVER_MARGIN = 1e-4
# Slack of every replay comparison against the plan, behind SOLVER_MARGIN.
REPLAY_TOLERANCE = 1e-4


def number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(name + ' must be a finite number')
    return value


def integer(value, name, lower, upper):
    if isinstance(value, bool) or not isinstance(value, int) or not lower <= value <= upper:
        raise ValueError(name + ' is out of range')
    return value
