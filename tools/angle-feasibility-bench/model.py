import gzip
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.interpolate import CubicSpline


@dataclass
class Problem:
    name: str
    ticks: int
    position: np.ndarray
    velocity: np.ndarray
    magnitude: np.ndarray
    friction: np.ndarray
    base: np.ndarray
    authored: np.ndarray
    walls: list
    a: np.ndarray
    b: np.ndarray
    rhs: np.ndarray
    source: str = ""
    source_hash: str = ""

    def residuals(self, theta):
        return self.a @ np.cos(theta) + self.b @ np.sin(theta) - self.rhs

    def jacobian(self, theta):
        return -self.a * np.sin(theta) + self.b * np.cos(theta)

    def replay(self, theta):
        controls = self.magnitude[:, None] * np.column_stack((np.cos(theta), np.sin(theta)))
        positions = np.empty((self.ticks + 1, 2))
        velocities = np.empty_like(positions)
        positions[0] = self.position
        velocities[0] = self.velocity
        for t in range(self.ticks):
            moved = velocities[t] + controls[t]
            positions[t + 1] = positions[t] + moved
            velocities[t + 1] = self.friction[t] * moved
        return positions, velocities, controls

    def validate(self, theta, tolerance=1e-8):
        if len(theta) != self.ticks or not np.all(np.isfinite(theta)):
            return {"feasible": False, "reason": "invalid angles"}
        positions, velocities, controls = self.replay(theta)
        residuals = []
        for wall in self.walls:
            axis = 0 if wall["mode"] == "X" else 1
            value = positions[wall["t1"], axis]
            if wall.get("t2") is not None:
                value += (1 if wall["op"] == "PLUS" else -1) * positions[wall["t2"], axis]
            residuals.append((1 if wall["cmp"] == "LE" else -1) * (value - wall["rhs"]))
        residuals = np.asarray(residuals)
        worst = float(np.max(residuals, initial=-np.inf))
        return {
            "feasible": bool(worst <= tolerance),
            "max_violation": max(0.0, worst),
            "minimum_slack": -worst,
            "matrix_replay_disagreement": float(np.max(np.abs(residuals - self.residuals(theta)), initial=0)),
            "magnitude_error": float(np.max(np.abs(np.linalg.norm(controls, axis=1) - self.magnitude), initial=0)),
            "rejected_rows": [dict(name=w["name"], residual=float(r), **{k: w[k] for k in ("mode", "t1", "t2", "op", "cmp", "rhs") if k in w}) for w, r in zip(self.walls, residuals) if r > tolerance],
        }

    def waypoint_angles(self):
        anchors = {0: self.position.copy()}
        bands = {}
        for wall in self.walls:
            if wall.get("t2") is not None:
                continue
            key = (wall["t1"], 0 if wall["mode"] == "X" else 1)
            lo, hi = bands.get(key, (-np.inf, np.inf))
            if wall["cmp"] == "GE":
                lo = max(lo, wall["rhs"])
            else:
                hi = min(hi, wall["rhs"])
            bands[key] = lo, hi
        times = sorted({t for t, axis in bands})
        for t in times:
            bounds = [bands.get((t, axis), (-np.inf, np.inf)) for axis in range(2)]
            if all(np.isfinite(lo) and np.isfinite(hi) for lo, hi in bounds):
                anchors[t] = np.array([(lo + hi) / 2 for lo, hi in bounds])
        if len(anchors) < 2:
            return np.zeros(self.ticks)
        ts = np.array(sorted(anchors))
        ps = np.array([anchors[t] - self.position for t in ts])
        spline = CubicSpline(ts, ps, bc_type=((1, self.velocity), "natural"))
        targets = spline(np.minimum(np.arange(self.ticks + 1), ts[-1]))
        displacement = np.diff(targets, axis=0)
        previous = self.velocity.copy()
        angles = np.zeros(self.ticks)
        for t in range(self.ticks):
            required = displacement[t] - previous
            if np.linalg.norm(required) < 1e-10:
                required = displacement[t]
            angles[t] = np.arctan2(required[1], required[0])
            previous = self.friction[t] * displacement[t]
        return angles


def compile_problem(data, ticks=None, name=None):
    n = data["ticks"] if ticks is None else ticks
    if n <= 0 or n > data["ticks"]:
        raise ValueError("Horizon outside exported problem")
    start = np.array(data["position"], dtype=float)
    velocity = np.array(data["velocity"], dtype=float)
    magnitude = np.array(data["magnitude"][:n], dtype=float)
    friction = np.array(data["friction"][:n], dtype=float)
    free = np.zeros((n + 1, 2))
    coupling = np.zeros((n + 1, n))
    derivative = np.zeros(n)
    v = velocity.copy()
    for t in range(n):
        derivative[t] += 1
        coupling[t + 1] = coupling[t] + derivative
        derivative *= friction[t]
        free[t + 1] = free[t] + v
        v *= friction[t]
    walls = []
    a, b, rhs = [], [], []
    for wall in data["walls"]:
        if max(wall["t1"], wall.get("t2") or 0) > n:
            continue
        if wall["mode"] not in ("X", "Z") or wall["cmp"] not in ("GE", "LE"):
            raise ValueError("Unsupported row: " + str(wall))
        axis = 0 if wall["mode"] == "X" else 1
        coef = coupling[wall["t1"]].copy()
        constant = start[axis] + free[wall["t1"], axis]
        if wall.get("t2") is not None:
            sign = 1 if wall["op"] == "PLUS" else -1
            coef += sign * coupling[wall["t2"]]
            constant += sign * (start[axis] + free[wall["t2"], axis])
        sign = 1 if wall["cmp"] == "LE" else -1
        coef *= sign * magnitude
        a.append(coef if axis == 0 else np.zeros(n))
        b.append(coef if axis == 1 else np.zeros(n))
        rhs.append(sign * (wall["rhs"] - constant))
        walls.append(wall.copy())
    base = np.array(data.get("base", [0] * n)[:n])
    authored = base + np.deg2rad(data.get("yaw", [0] * n)[:n])
    return Problem(name or data["name"], n, start, velocity, magnitude, friction, base, authored,
                   walls, np.array(a).reshape((-1, n)), np.array(b).reshape((-1, n)), np.array(rhs),
                   data.get("source", ""), data.get("sha256", ""))


def load_export(path, root=None):
    path = Path(path)
    content = path.read_bytes()
    if path.suffix == ".gz":
        content = gzip.decompress(content)
    values = json.loads(content)
    if root is not None:
        for value in values:
            actual = hashlib.sha256((Path(root) / value["source"]).read_bytes()).hexdigest()
            if actual != value["sha256"]:
                raise ValueError("Capture changed since export: " + value["source"])
    return values
