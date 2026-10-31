def equal(model, lhs, rhs, switch=None):
    if switch is None:
        model.addCons(lhs == rhs)
    else:
        model.addConsIndicator(lhs - rhs <= 0, binvar=switch)
        model.addConsIndicator(rhs - lhs <= 0, binvar=switch)


def inertia(model, value, bound, threshold, name, *, initial=None):
    if initial is not None:
        return (0. if abs(initial) < threshold else value), []
    if threshold == 0:
        return value, []
    if bound < threshold:
        return 0., []
    carry = model.addVar('carry_' + name, lb=-bound, ub=bound)
    modes = [model.addVar('inertia_' + name + '_' + str(i), vtype='B') for i in range(3)]
    model.addCons(sum(modes) == 1)
    for row in (value <= threshold - 1e-8, -value <= threshold - 1e-8):
        model.addConsIndicator(row, binvar=modes[0])
    equal(model, carry, 0, modes[0])
    model.addConsIndicator(-value <= -threshold - 1e-8, binvar=modes[1])
    equal(model, carry, value, modes[1])
    model.addConsIndicator(value <= -threshold - 1e-8, binvar=modes[2])
    equal(model, carry, value, modes[2])
    return carry, modes
