import math


def movement_bounds(case, domains, horizon):
    route, profiles, modes = case['route'], case['profiles'], case['modes']
    states = [[0., 0.] for _ in range(horizon + 1)]
    prior = {0: (0., math.hypot(*route['initial']['velocity'][::2]))}
    phases = {}

    def record(tick, radius, speed):
        states[tick][0] = max(states[tick][0], radius)
        states[tick][1] = max(states[tick][1], speed)

    def advance(radius, speed, name):
        movement = speed + modes[name]['magnitude']
        return radius + movement, movement * modes[name]['friction']

    for leg, choices in enumerate(domains):
        following = {}
        for departure in choices:
            reached = []
            for arrival, (radius, speed) in prior.items():
                count = departure - arrival
                if not route['runBounds'][leg][0] <= count <= route['runBounds'][leg][1]:
                    continue
                for tick in range(arrival, departure):
                    record(tick, radius, speed)
                    radius, speed = advance(radius, speed, 'run')
                reached.append((radius, speed))
            if not reached:
                continue
            radius, speed = max(v[0] for v in reached), max(v[1] for v in reached)
            trace = []
            for k in range(profiles[leg]['airtime']):
                trace.append((radius, speed))
                record(departure + k, radius, speed)
                radius, speed = advance(radius, speed, 'jump' if k == 0 else 'air')
            trace.append((radius, speed))
            record(departure + profiles[leg]['airtime'], radius, speed)
            phases[leg, departure] = trace
            following[departure + profiles[leg]['airtime']] = radius, speed
        prior = following
    return states, phases
