import math

from parkour_solver.numerics import prepare, validate
from parkour_solver.scene.route import initial_guess


def waypoint_seed(problem, waypoints):
    yaw = problem['yaw'][:]
    for tick in range(min(problem['ticks'], 160)):
        targets = [(at, point) for at, point in waypoints if at > tick]
        if not targets:
            break
        at, point = min(targets, key=lambda item: item[0])
        state = validate(problem, yaw)
        coupling, drag = 0., 1.
        for t in range(tick, at):
            coupling += drag
            drag *= problem['friction'][t]
        delta = [point[a] - state['positions'][tick][a] - coupling * state['velocity'][tick][a] for a in (0, 1)]
        yaw[tick] = math.degrees(math.atan2(delta[1], delta[0]) - problem['base'][tick])
    return yaw


def ladder_waypoints(problem, selections):
    waypoints = []
    for offset, selected in selections:
        if selected.get('entryOnly'):
            continue
        s, column, wall = selected['schedule'], selected['column'], selected['wall']
        point = [column['x'] + .5, column['z'] + .5]
        point[0 if wall['axis'] == 'X' else 1] = wall['plane']
        if s['entryState'] == 0 and offset == 0:
            tangent = 1 if wall['axis'] == 'X' else 0
            point[tangent] = problem['position'][tangent]
        if any(at == offset and e['schedule']['exitState'] <= s['entryState']
               for at, e in selections):
            entry_face = selected['entryFace']
            if entry_face['axis'] != wall['axis']:
                point[0 if entry_face['axis'] == 'X' else 1] = entry_face['plane'] - entry_face['direction'] * .03
        waypoints.append((offset + s['entryState'], point[:]))
        following = [e for at, e in selections if at == offset
                     and e['schedule']['entryState'] >= s['exitState']]
        transfer_target = None
        if following:
            transfer_target = point[:]
            face = selected['exitFace']
            transfer_target[0 if face['axis'] == 'X' else 1] = face['plane'] + face['direction'] * .13
        for t, hit in enumerate(s['hit']):
            if hit:
                pressing = point[:]
                if transfer_target is not None:
                    duration = max(1, s['exitState'] - s['entryState'] - 1)
                    progress = max(0., min(1., (t + 1 - s['entryState']) / duration))
                    tangent = 1 if wall['axis'] == 'X' else 0
                    inside_target = transfer_target[tangent]
                    if selected['exitFace']['axis'] != wall['axis']:
                        inside_target -= selected['exitFace']['direction'] * .14
                    if duration >= 8 and abs(inside_target - pressing[tangent]) / duration < .09:
                        progress *= progress
                    pressing[tangent] += progress * (inside_target - pressing[tangent])
                pressing[0 if wall['axis'] == 'X' else 1] += wall['direction'] * .01
                waypoints.append((offset + t + 1, pressing))
        face = selected['exitFace']
        if s['exitKind'] == 'top':
            point[0 if wall['axis'] == 'X' else 1] = wall['plane'] + wall['direction'] * .03
            if transfer_target is not None:
                point = transfer_target
        elif transfer_target is not None:
            point = transfer_target
        elif s['exitKind'] == 'side':
            point[0 if face['axis'] == 'X' else 1] = face['plane'] + face['direction'] * .03
        waypoints.append((offset + s['exitState'], point[:]))
    return waypoints


def numerical_problem(route, plan, coefficients, parent=None):
    """The numerical request for a scheduled plan of a footprint-derived route.

    find_route sends it and the creation-time relaxation screen solves it
    (reachability/service.py), so a child's screen and its repair loop's request
    for the same rows are one Clarabel solve (numerics/disk.py caches by content).
    """
    from parkour_solver import native_core
    native = native_core.from_environment()
    if native is not None:
        return native.numerical_problem(route, plan, coefficients, parent)
    problem = dict(coefficients, version=1, ticks=plan['ticks'],
                   position=[route['initial']['position'][0], route['initial']['position'][2]],
                   velocity=[route['initial']['velocity'][0], route['initial']['velocity'][2]],
                   yaw=initial_guess(route, plan, parent), threshold=route.get('physicsThreshold', .005), walls=plan['walls'],
                   contacts=plan.get('contacts', []),
                   objective=dict(route.get('guideObjective', route['objective']), tick=plan['ticks']))
    if 'ladderTicks' in plan:
        problem['ladderTicks'] = plan['ladderTicks']
    return prepare(problem)
