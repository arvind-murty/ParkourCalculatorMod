"""Declared milestones and acyclic visits of merged top faces, including local hops."""

import copy

from parkour_solver.search.identity import digest
from parkour_solver.search.comparison import comparison_label, priority_key, POLICY_VERSION as COMPARISON_POLICY_VERSION


HARD_JUMP_POLICY_VERSION = 5
# A jump back onto its launch face fits when the room ahead exceeds this share of its natural jump (user, 2026-11-21;
# may change).
SAME_FACE_FIT_FACTOR = .7


def settings(value, scene):
    result = dict(version=1, anchors=[], turnaround=dict(degrees=110., spatialMargin=.3),
                  solveEveryDecisionLevels=1, feasibilityPolicy='numerical_oracle', observability=True)
    if not isinstance(value, dict) or set(value)-set(result):
        raise ValueError('Unsupported anchorSearch fields')
    result.update(copy.deepcopy(value))
    names = {b['id'] for b in scene['blocks']}
    anchors = result['anchors']
    if (not isinstance(anchors, list) or len(set(anchors)) != len(anchors) or
            any(a not in names for a in anchors)):
        raise ValueError('Anchors must be ordered unique scene blocks')
    if scene['startBlock'] in anchors or scene['goalBlock'] in anchors:
        raise ValueError('Initial and terminal supports are implicit endpoints')
    # Visits name merged faces, so an anchor is only reached if its top is a whole face of its own (user, 2026-11-22).
    from parkour_solver.movement.contact_combinations import solid_owners, top_faces
    solid, owners = {b['id']: b['solidIndex'] for b in scene['blocks']}, solid_owners(scene)
    for a in anchors+[scene['startBlock'], scene['goalBlock']]:
        shared = {o for f in top_faces(scene) if solid[a] in f.members for i in f.members for o in owners[i]}-{owner(scene, a)}
        if shared:
            raise ValueError(f"{a} shares its top face with {', '.join(sorted(shared))}; declare them as one block")
    if result['version'] != 1 or result['feasibilityPolicy'] != 'numerical_oracle':
        raise ValueError("Unsupported anchor policy: version must be 1 and feasibilityPolicy 'numerical_oracle'")
    if type(result['solveEveryDecisionLevels']) is not int or result['solveEveryDecisionLevels'] < 1:
        raise ValueError('Decision solve interval must be positive')
    if type(result['observability']) is not bool:
        raise ValueError('observability must be boolean')
    turn = result['turnaround']
    if not isinstance(turn, dict) or set(turn)-{'degrees','spatialMargin'}:
        raise ValueError('Unsupported turnaround settings')
    result['turnaround'] = dict(degrees=110., spatialMargin=.3) | turn
    import math
    if any(isinstance(v, bool) or not isinstance(v, (int,float)) or not math.isfinite(v)
           for v in result['turnaround'].values()):
        raise ValueError('Turn settings must be finite numbers')
    if not 0 < result['turnaround']['degrees'] < 180 or result['turnaround']['spatialMargin'] < 0:
        raise ValueError('Invalid turn angle or margin')
    return result


def semantic_settings(policy):
    return dict({k:v for k,v in policy.items() if k!='observability'},
                hardJumpPolicyVersion=HARD_JUMP_POLICY_VERSION,searchPolicyVersion=20,comparisonPolicyVersion=COMPARISON_POLICY_VERSION)


def owner(scene, block):
    b = next((b for b in scene['blocks'] if b['id'] == block), None)
    return b.get('sourceBlock', b['id']) if b else block


def box_owners(scene):
    return {tuple(scene['collisionScene']['solids'][b['solidIndex']]): owner(scene,b['id']) for b in scene['blocks']}


def face_namer(scene):
    """A function naming the merged top face a list of boxes stands on; None when no box is a block's solid.

    Boxes inside one face that are not all of it (one block, while generation still lands per block) take the
    face's name. Boxes making up a whole face, or a block cut into differently named pieces, take their owners'
    join.
    """
    from parkour_solver.movement.contact_combinations import face_name, solid_owners, top_faces
    owners = solid_owners(scene)
    index = {tuple(scene['collisionScene']['solids'][i]): i for i in owners}
    faces = {}
    for option in top_faces(scene):
        for i in option.members:
            faces.setdefault(i, []).append((frozenset(option.members), face_name(scene, option, owners)))

    def name(boxes):
        found = frozenset(index[tuple(b)] for b in boxes if tuple(b) in index)
        if not found:
            return None
        holding = [(members, n) for members, n in faces.get(min(found), ()) if found <= members]
        names = {n for _, n in holding}
        if len(names) == 1 and all(members != found for members, _ in holding):
            return names.pop()
        return '+'.join(sorted(set().union(*(owners[i] for i in found))))
    return name


def support_face(scene, support, name=None):
    """The face a block id (terminalSupport, the start block) names through its solid; any other value unchanged."""
    block = next((b for b in scene['blocks'] if b['id'] == support), None)
    if block is None:
        return support
    return (name or face_namer(scene))([scene['collisionScene']['solids'][block['solidIndex']]])


def terminal_face(scene, program):
    """The face a program's terminalSupport names; None without one (slab boosts and blip-ups set none)."""
    return support_face(scene, program['terminalSupport']) if program.get('terminalSupport') else None


def visits(scene, program):
    """Read actual selected contacts, never profile height or transient air IDs."""
    name = face_namer(scene)
    events = [(e['offset'], 0, e) for e in program['profile'].get('contactEvents', [])
              if e['axis'] == 'Y' and e.get('direction') == -1]
    profile=program['profile']
    for e in profile.get('caseEvents', []):
        t=e['input']+1
        # A cancel/grind touches geometry without landing on its owner. Keep
        # its requested impulse and physical case, but do not invent a visit.
        if e.get('lowerBoxes') is not None:
            events.append((e['input'],1,dict(boxes=e['lowerBoxes'])))
        boxes=e.get('boxes')
        if boxes and profile['grounded'][t] and abs(profile['position'][t]-max(b[4] for b in boxes))<=1e-7:
            events.append((e['input'],2,e))
    result = [(0, support_face(scene,scene['startBlock'],name))]
    for tick, _, e in sorted(events, key=lambda x:(x[0],x[1],digest(x[2]))):
        found = name(e.get('boxes', ()))
        if found is not None:
            result.append((tick+1, found))
    # Some named profiles report only terminalSupport. Include its physical
    # face without collapsing the input/jump history used by the solver.
    if program.get('terminalSupport') is not None:
        result.append((program['profile']['duration'], support_face(scene,program['terminalSupport'],name)))
    return result


def launch(scene, program, begin=None, end=None):
    tick, name = [v for v in visits(scene, program) if begin is None or v[0] <= begin][-1]
    ladder = program['profile']['ladderTicks'][tick:None if end is None else end+1]
    return None if any(ladder) else (tick, name)


FEET_CASES = frozenset(('normal_step', 'slab_boost', 'blip', 'blip_up', 'corner_jump_cancel', 'corner_blip_up'))


def feet_touches(scene, program):
    name = face_namer(scene)
    result = []
    for e in program['profile'].get('caseEvents', []):
        if e.get('case') not in FEET_CASES:
            continue
        for boxes in (e.get('boxes') or (), e.get('lowerBoxes') or ()):
            found = name(boxes)
            if found is not None:
                result.append((e['input']+1, found))
    return sorted(result)


def visit_state(scene, program):
    current = None
    departed = set()
    valid = True
    touches = feet_touches(scene,program)
    arrivals = visits(scene,program)
    # A touch that is itself a visit at that tick (a blip's lower landing) is not a return to that face.
    visited = set(arrivals)
    def touch_until(tick):
        nonlocal valid
        while touches and (tick is None or touches[0][0] < tick):
            touch = touches.pop(0)
            if touch[1] in departed and touch not in visited:
                valid = False
    for tick, name in arrivals:
        touch_until(tick)
        if name == current:
            continue
        if name in departed:
            valid = False
        if current is not None:
            departed.add(current)
        current = name
    touch_until(None)
    return dict(version=1, current=current, departed=sorted(departed), valid=valid)


def audit(scene, program, anchors, *, reached=None):
    """Required anchors reached in order (and their arrival ticks), even for a rejected program."""
    state = visit_state(scene,program)
    required = [owner(scene,a) for a in anchors]
    index = 0
    times = []
    reason = None
    for tick, name in visits(scene,program):
        if name in required[index:]:
            if name != required[index]:
                reason = 'required_anchor_order'
                break
            times.append(tick)
            index += 1
    if not state['valid']:
        reason = 'departed_owner_return'
    elif reason is None and reached is not None and index < reached:
        reason = 'missing_prior_anchor'
    return dict(accepted=reason is None, reason=reason, visits=state, reached=index, ticks=times)


def next_anchor(scene, program, anchors, task):
    """Face name (its owner, see settings) of the next required anchor (anchors are visited in order), or of the task
    block after the last."""
    reached = audit(scene, program, anchors)['reached']
    return owner(scene, anchors[reached] if reached < len(anchors) else task)


ADVANTAGE_CASES = frozenset(('grind', 'slab_boost', 'corner_jump_cancel'))


def advantageous_targets(scene, base, child, next_owner):
    """Keys of the advantageous jumps made by the choice that turns base into child.

    A jump (or a run-off/step-up, itself a run tick) is advantageous when it lands on the
    next anchor; a jump also when it hits a ceiling or is a grind, slab boost or corner jump
    cancel. Run ticks are generated only as the minimum that makes one of these possible.
    """
    p, start = child['profile'], base['profile']['duration']
    by_box = box_owners(scene)
    def owners(boxes):
        return '+'.join(sorted({by_box[tuple(b)] for b in boxes if tuple(b) in by_box}))
    # Undersides are already merged, so a ceiling or grind is named by its boxes' owners; a slab boost or corner
    # jump cancel by the top face it steps onto, as advantageous_candidates keys it.
    face = face_namer(scene)
    keys = set()
    if p['grounded'][-1] and owner(scene, child.get('terminalSupport')) == next_owner:
        keys.add(('anchor', next_owner))
    if not p['jump'][start]:
        return keys
    keys |= {('ceiling', owners(e['boxes'])) for e in p.get('contactEvents', [])
             if e.get('role') == 'ceiling' and e['offset'] >= start}
    keys |= {(e['case'], owners(e['boxes']) if e['case'] == 'grind' else face(e['boxes'])) for e in p.get('caseEvents', [])
             if e.get('case') in ADVANTAGE_CASES and e['input'] >= start}
    return keys


def advantageous_candidates(scene, table, launch, top, next_owner, types, threshold=.005):
    """Advantageous moves from a grounded decision on face `launch` (feet at `top`), from launch tables
    (`table(face)`, see reachability.service.face_table) and vertical replays only.

    Each entry is dict(key, A, via). A jump-now move (via None) has the key advantageous_targets
    gives as a JSON list and its airtime A: the jump input plus its air inputs through the landing (12 for a
    flat jump, 3 under a ceiling 2 blocks above the feet, 2 for a grind, slab boost or corner jump cancel).
    A ceiling or grind A is the earliest descent through any landing top of the launch table, a lower bound
    (the built choice stops at the hit, and the table ignores where the player can be after it); the
    frontier uses it only to bound run ticks and takes built landings for the rest. A run-off onto another
    face (via) is a ["landing", face] entry: A counts its airborne inputs only, since the walk-off input
    moves as a run tick (0 when it lands within that input), and `limit` is the airtime of an unhit jump
    from here onto that face (the frontier lowers it to a jump-now landing there), which its run ticks
    plus A compete with. A walking step-up onto another face is a ["step", face] entry with A = 0: its
    step input is a run tick too. Slab boosts and corner jump cancels are keyed by the face they step onto.
    """
    import json
    import math
    from parkour_solver.movement.cases import case_profile
    from parkour_solver.movement.contact_combinations import scene_contact_options
    from parkour_solver.movement.normal import first_descent, replay
    from parkour_solver.physics.vertical import REST_CARRY, f32
    blocks = {b['id']: b for b in scene['blocks']}
    by_box, name_boxes = box_owners(scene), face_namer(scene)
    def face(block):
        return support_face(scene, block, name_boxes)
    undersides = scene_contact_options(scene, 1, -1) if 'ceiling' in types and scene.get('ceilingHits', True) else []

    def rest(y):
        return dict(position=y, velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)

    def landing(trace, tops):
        return min((t+1 for u in tops for t in [first_descent(trace, u)] if t is not None), default=None)

    def jumps(name, t):
        """(kind, name) -> A for the jumps from face `name` with feet at t."""
        kinds = table(name)
        tops = {blocks[b]['top'] for b in kinds['landing']}
        trace = replay(rest(t), 200, jumping=True, threshold=threshold)
        found = {}
        def add(key, airtime):
            if airtime is not None and airtime < found.get(key, math.inf):
                found[key] = airtime
        if 'normal' in types:
            add(('anchor', next_owner), landing(trace, {blocks[b]['top'] for b in kinds['landing']
                                                        if owner(scene, b) == next_owner}))
        for option in undersides:
            if not any(i in kinds['ceilingSolids'] for i in option.members):
                continue
            names, plane = '+'.join(sorted({by_box[tuple(b)] for b in option.boxes if tuple(b) in by_box})), option.plane
            hit = next((h for h, move in enumerate(trace['movement'])
                        if move > 0 and trace['position'][h] <= plane < trace['position'][h+1]), None)
            if hit is not None:
                add(('ceiling', names), landing(replay(rest(t), 200, jumping=True, threshold=threshold,
                                                       contacts=[dict(state=hit+1, plane=plane, direction=1)]), tops))
            if 0 <= plane-t < f32(.42):
                event = dict(case='grind', input=0, plane=t, clearance=plane-t)
                add(('grind', names), min((p['duration'] for u in tops
                                           for p in [case_profile(t, u, event, threshold=threshold)] if p), default=None))
        if 'stepping' in types:
            for b in kinds['stepping']:
                if f32(.42) < blocks[b]['top']-t <= f32(.6):
                    add(('slab_boost', face(b)), 2)
                    add(('corner_jump_cancel', face(b)), 2)
        return found

    entries = {}
    def add(key, airtime, via, **bound):
        key = json.dumps(key)
        if key not in entries or airtime < entries[key]['A']:
            entries[key] = dict(key=key, A=airtime, via=via, **bound)
    for (kind, name), airtime in jumps(launch, top).items():
        add([kind, name], airtime, None)
    kinds, fall = table(launch), replay(rest(top), 200, threshold=threshold)
    rise = replay(rest(top), 200, jumping=True, threshold=threshold)
    for b in sorted(kinds['landing'] | kinds['stepping']):
        name, u = face(b), blocks[b]['top']
        if name == launch:
            continue
        if 'normal' in types and b in kinds['landing'] and (u < top or u == top and name == next_owner):
            t = first_descent(fall, u)
            limit = landing(rise, {blocks[c]['top'] for c in kinds['landing'] if face(c) == name})
            if t is None or limit is None:
                continue
            add(['landing', name], t, name, limit=limit)
        elif 'stepping' in types and b in kinds['stepping'] and 0 < u-top <= f32(.6):
            add(['step', name], 0, name)
    return sorted(entries.values(), key=lambda e: e['key'])


def same_face_fit(point, velocity, leg, regions, magnitudes, frictions):
    """Whether a jump landing back on its launch face naturally fits there (O11).

    The room is measured from the player's solved position `point` ([x, z]) to the farthest corner of the face the
    player stands on, given as its merged player-centre `regions` ([x0, z0, x1, z1]), along the heading of the leg's
    direction class (`leg_class`). The natural jump accelerates fully along the leg from the solved velocity with the
    landing schedule's movement magnitudes and frictions.
    """
    from parkour_solver.reachability.intervals import travel_radii
    from parkour_solver.spatial.anchor_guides import leg_class
    dx, dz = leg
    u, _ = leg_class(leg)
    room = max((x-point[0])*u[0]+(z-point[1])*u[1] for region in regions
               for x in (region[0], region[2]) for z in (region[1], region[3]))
    jump = travel_radii(velocity[0]*dx+velocity[1]*dz, magnitudes, frictions)[-1]
    return dict(room=room, jump=jump, fits=room > SAME_FACE_FIT_FACTOR*jump)

def encounter_groups(scene, names):
    """Partition the pairs names[k] -> names[k+1] into groups searched as one frame.

    From a group's first end block, follow touching solids (the obstacle-cluster
    test) that some landing block of the chain can touch in one flight. Every such
    solid joins and extends the chain; the block-table kinds only label members as
    landings (blocks a landing block can land on) or hit/ceiling obstacles. A
    landing block's table measures from its whole face. The group runs through
    the last later anchor among the landing blocks.
    """
    from parkour_solver.reachability.service import face_table
    from parkour_solver.spatial.surfaces import scene_geometry
    geometry = scene_geometry(scene['collisionScene'])
    solid = {b['id']: b['solidIndex'] for b in scene['blocks']}
    by_solid = {}
    for b in scene['blocks']:
        by_solid.setdefault(b['solidIndex'], []).append(b['id'])
    traces, tables, name_boxes = {}, {}, face_namer(scene)

    def table(name):
        key = support_face(scene, name, name_boxes)
        if key not in tables:
            tables[key] = face_table(scene, key, traces)
        return tables[key]

    def neighbours(name):
        return {n for i in geometry.touching(solid[name], by_solid) for n in by_solid[i]}

    groups, first = [], 0
    while first < len(names)-1:
        members, landings = {names[first+1]}, {names[first+1]}
        while True:
            frontier = {n for m in members for n in neighbours(m)}-members
            frontier = {c for c in frontier if any(c in table(l)['blocks'] for l in landings)}
            if not frontier:
                break
            members |= frontier
            grown = True
            while grown:
                added = {c for c in members-landings if any(c in table(l)['landing'] for l in landings)}
                landings |= added
                grown = bool(added)
        owners = {owner(scene, l) for l in landings}
        last = max(m for m in range(first+1, len(names)) if owner(scene, names[m]) in owners)-1
        groups.append(dict(first=first, last=last, anchors=names[first+1:last+2], members=sorted(members),
                           landings=sorted(landings)))
        first = last+1
    return groups


def local_counts(program, start=0):
    c=comparison_label(program,start=start)
    return {k:c[k] for k in ('T','J','R','A','start')}


def priority(program, start=0, turnaround=False, identity=''):
    return priority_key(comparison_label(program,start=start,mode=turnaround,identity=identity))


def within_caps(request, program):
    """Honor declared limits without importing legacy scene defaults as caps."""
    c=local_counts(program)
    if any(name in request and c[key]>request[name] for name,key in
           (('maxJumps','J'),('maxTotalRunTicks','R'))):
        return False
    if 'maxRunTicksPerJump' in request:
        run=0;p=program['profile']
        for t in range(p['duration']):
            run=run+1 if p['grounded'][t] and not p['jump'][t] else 0
            if run>request['maxRunTicksPerJump']:return False
    return True


def transfer_structure(scene, program, start, source, target, *, complete=False, until_arrival=False):
    """Separate source-local momentum preparation from the transfer flight.

    Each actual return to the source closes a setup hop. The pending/complete
    flight after the last source contact has at most/exactly one requested
    jump. A source contact during the target's arrival input belongs to that
    transfer's physical case, not an earlier setup boundary. With
    `until_arrival`, history after the first target arrival belongs to later
    pairs of the same encounter group and is ignored.
    """
    p=program['profile'];n=p['duration']
    launches=[t for t in range(start,n) if p['grounded'][t] and p['jump'][t]]
    source,target=owner(scene,source),owner(scene,target)
    contacts=[(t,name) for t,name in visits(scene,program) if start<t<=n]
    arrival=min((t for t,name in contacts if name==target),default=None)
    if until_arrival and arrival is not None:
        contacts=[(t,name) for t,name in contacts if t<=arrival]
        launches=[t for t in launches if t<arrival]
    boundary=max([start]+[t for t,name in contacts if name==source and (arrival is None or t<arrival)])
    preparation=[t for t in launches if t<boundary]
    transfer=[t for t in launches if t>=boundary and (arrival is None or t<arrival)]
    from parkour_solver.movement.cases import CASES
    stepping=[e['input'] for e in p.get('caseEvents', [])
              if (e.get('type')=='stepping' or e.get('case') in CASES-{'grind'})
              and boundary<=e['input'] and (arrival is None or e['input']<arrival)]
    reason=None
    if any(name not in (source,target) for _,name in contacts):
        reason='intermediate_support_between_anchors'
    elif arrival is not None and any(t>arrival and name==source for t,name in contacts):
        reason='return_after_target_arrival'
    elif arrival is not None and any(t>=arrival for t in launches):
        reason='jump_after_target_arrival'
    elif len(transfer)>1:
        reason='multiple_transfer_impulses'
    elif arrival is not None and stepping:
        reason='stepping_during_hard_transfer'
    elif complete and (arrival is None or len(transfer)!=1):
        reason='missing_transfer_jump'
    return dict(version=HARD_JUMP_POLICY_VERSION,valid=reason is None,reason=reason,
                source=source,target=target,spanStart=start,preparationThrough=boundary,
                preparationJumps=preparation,transferJumps=transfer,arrival=arrival,
                transferSteppingInputs=stepping,assumption='hard_transfer_without_stepping')

