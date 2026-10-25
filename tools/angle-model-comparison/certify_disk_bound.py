import argparse
import hashlib
import json
from decimal import Context, Decimal, ROUND_CEILING, ROUND_FLOOR
from fractions import Fraction
from pathlib import Path


class IntervalArithmetic:
    def __init__(self, precision):
        self.down = Context(prec=precision, rounding=ROUND_FLOOR)
        self.up = Context(prec=precision, rounding=ROUND_CEILING)

    def exact(self, value):
        number = Decimal.from_float(float(value))
        return number, number

    def rational(self, value):
        numerator, denominator = Decimal(value.numerator), Decimal(value.denominator)
        return self.down.divide(numerator, denominator), self.up.divide(numerator, denominator)

    def add(self, first, second):
        return self.down.add(first[0], second[0]), self.up.add(first[1], second[1])

    def negate(self, value):
        return value[1].copy_negate(), value[0].copy_negate()

    def subtract(self, first, second):
        return self.add(first, self.negate(second))

    def multiply(self, first, second):
        return (min(self.down.multiply(a, b) for a in first for b in second),
                max(self.up.multiply(a, b) for a in first for b in second))

    def norm_upper(self, vector):
        squares = Decimal(0)
        for component in vector:
            absolute = max(value.copy_abs() for value in component)
            squares = self.up.add(squares, self.up.multiply(absolute, absolute))
        return self.up.next_plus(self.up.sqrt(squares))


def certified_lower_bound(case, multipliers, precision=60):
    arithmetic = IntervalArithmetic(precision)
    raw_weights = [Fraction.from_float(max(float(value), 0.0)) for value in multipliers]
    total = sum(raw_weights)
    if total <= 0:
        raise ValueError("A positive multiplier sum is required")
    weights = [arithmetic.rational(value / total) for value in raw_weights]
    n = case["ticks"]
    zero = arithmetic.exact(0)
    coefficients = [[zero, zero] for _ in range(n + 1)]
    rhs = zero
    index = 0
    for row in case["walls"]:
        if row["mode"] not in ("X", "Z"):
            raise ValueError("Only affine X/Z rows are supported")
        axis = 0 if row["mode"] == "X" else 1
        signs = (1, -1) if row["cmp"] == "EQ" else ((1,) if row["cmp"] == "LE" else (-1,))
        for sign in signs:
            weight = weights[index] if sign == 1 else arithmetic.negate(weights[index])
            index += 1
            coefficients[row["t1"]][axis] = arithmetic.add(coefficients[row["t1"]][axis], weight)
            if "t2" in row:
                other = weight if row["op"] == "PLUS" else arithmetic.negate(weight)
                coefficients[row["t2"]][axis] = arithmetic.add(coefficients[row["t2"]][axis], other)
            rhs = arithmetic.add(rhs, arithmetic.multiply(weight, arithmetic.exact(row["rhs"])))
    if index != len(weights):
        raise ValueError("Multiplier count must match the signed wall rows")
    position = [arithmetic.exact(value) for value in case["position"]]
    velocity = [arithmetic.exact(value) for value in case["velocity"]]
    constant = zero
    for tick in range(n + 1):
        for axis in range(2):
            constant = arithmetic.add(constant, arithmetic.multiply(coefficients[tick][axis], position[axis]))
        if tick < n:
            for axis in range(2):
                position[axis] = arithmetic.add(position[axis], velocity[axis])
                velocity[axis] = arithmetic.multiply(velocity[axis], arithmetic.exact(case["friction"][tick]))
    constant = arithmetic.subtract(constant, rhs)
    position_adjoint = coefficients[n]
    velocity_adjoint = [zero, zero]
    support_upper = Decimal(0)
    for tick in range(n - 1, -1, -1):
        friction = arithmetic.exact(case["friction"][tick])
        displacement_adjoint = [arithmetic.add(position_adjoint[axis],
                                arithmetic.multiply(friction, velocity_adjoint[axis])) for axis in range(2)]
        control_adjoint = [arithmetic.multiply(value, arithmetic.exact(case["magnitude"][tick]))
                           for value in displacement_adjoint]
        support_upper = arithmetic.up.add(support_upper, arithmetic.norm_upper(control_adjoint))
        velocity_adjoint = displacement_adjoint
        position_adjoint = [arithmetic.add(position_adjoint[axis], coefficients[tick][axis]) for axis in range(2)]
    bound = arithmetic.down.subtract(constant[0], support_upper)
    return {"case": case["name"], "precision": precision, "signedWallRows": index,
            "supportDualLowerBound": str(bound), "excludesZeroViolation": bound > 0,
            "multiplierNormalization": "Exact rational normalization of nonnegative binary64 inputs",
            "arithmetic": "Directed Decimal intervals; adjoint recurrence independent of dense affine matrix",
            "scope": "Ideal continuous no-threshold recurrence with serialized binary64 parameters and unit disks; not Minecraft game replay"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, required=True)
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    cases = json.loads(arguments.problems.read_text())
    diagnostics = json.loads(arguments.weights.read_text())
    if len(cases) != 1 or len(diagnostics) != 1:
        parser.error("Select one compiled case and one multiplier diagnostic")
    result = certified_lower_bound(cases[0], diagnostics[0]["nonnegativeWeights"])
    result["problemsSha256"] = hashlib.sha256(arguments.problems.read_bytes()).hexdigest()
    result["weightsSha256"] = hashlib.sha256(arguments.weights.read_bytes()).hexdigest()
    arguments.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
