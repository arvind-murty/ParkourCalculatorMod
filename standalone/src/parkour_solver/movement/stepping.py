from parkour_solver.physics.vertical import f32
from parkour_solver.movement.cases import convex_corner, corner_rows, exposed_side, outside_row, top_choices


def box_for(block, context):
    return context['scene']['collisionScene']['solids'][block['solidIndex']]


class SteppingGenerator:
    def generate(self, context, *, start=0, ends=None):
        yield from open_stepping_choices(context)

def _sequence_allowed(context, initial, event, jumping, rows, support=None):
    """Check the common named outcome before creating its side alternatives."""
    gate=context.get('interactionGate')
    if gate is None or not hasattr(gate,'sequence'):
        return True
    from parkour_solver.movement.normal import named_choice
    common=named_choice(context,'stepping',initial,event,jumping=jumping,
                        constraints=rows,support=support)
    return common is not None and gate.sequence(context,common) is None


def _fresh(made, *key):
    """Whether a choice keyed by `key` is new for its face: members of a merged face repeat each other's side rows."""
    import json
    key = json.dumps(key, sort_keys=True)
    if key in made:
        return False
    made.add(key)
    return True


def open_stepping_choices(context):
    """Steps, slab boosts and flight cases onto merged top faces: the side and corner code runs per member block, each
    choice carries its face's boxes and name, and a member repeating another's choice is dropped."""
    from parkour_solver.movement.contact_combinations import bounding_region, face_name, solid_owners
    from parkour_solver.reachability.service import candidate_faces, entry_backward, interaction_allowed, walk_off_allowed
    from parkour_solver.movement.program import scalar_initial
    from parkour_solver.movement.normal import choice_approaches, feet_allowed, first_descent, named_choice
    initial = scalar_initial(context['state'])
    scene = context['scene']
    y0 = initial['position']
    allowed = context.get('cases')
    def enabled(case):
        return allowed is None or case in allowed
    owners = solid_owners(scene)
    faces = [(face, face_name(scene, face, owners), [b for b in scene['blocks'] if b['solidIndex'] in face.members])
             for face in candidate_faces(context, 'stepping')]
    if initial['onGround']:
        for face, name, members in faces:
            if not feet_allowed(context, name):
                continue
            rise, boxes, made = face.plane - y0, [list(b) for b in face.boxes], set()
            for block in members:
                box = box_for(block, context)
                for case, jumping in [('normal_step', False), ('slab_boost', True)]:
                    if (not enabled(case) or not 0 < rise <= f32(.6) or jumping and rise <= f32(.42) or
                            not jumping and not context.get('runTickChoices', True)):
                        continue
                    if not interaction_allowed(context, box, 0, jumping=jumping, label=dict(owner=block['id'], role=case)):
                        continue
                    for axis in ('X', 'Z'):
                        for direction in (-1, 1):
                            if not exposed_side(scene, block, axis, -direction, y0) or entry_backward(
                                    context, axis, -direction, bounding_region(face.regions), label=dict(owner=block['id'], role=case)):
                                continue
                            for top, regions in top_choices(context, face, name, 1, jumping=jumping):
                                rows = [outside_row(box, axis, -direction, 0)] + top
                                if not _fresh(made, case, rows):
                                    continue
                                event = dict(case=case, input=0, plane=face.plane, boxes=boxes,
                                             anchors={'beforeJump' if jumping else 'beforeStep': 0, 'jump' if jumping else 'hit': 1})
                                choice = named_choice(context, 'stepping', initial, event, jumping=jumping, constraints=rows,
                                                      support=name if not jumping else None, region_choices=regions)
                                if choice:
                                    yield choice
                if (enabled('corner_jump_cancel') and f32(.42) < rise <= f32(.6) and
                        interaction_allowed(context, box, 0, jumping=True, label=dict(owner=block['id'], role='corner_jump_cancel'))):
                    for xdir in (-1, 1):
                        for zdir in (-1, 1):
                            # corner_rows enter across the X side facing -xdir; the Z side is only passed.
                            if not convex_corner(scene, block, xdir, zdir, y0) or entry_backward(
                                    context, 'X', -xdir, bounding_region(face.regions),
                                    label=dict(owner=block['id'], role='corner_jump_cancel')):
                                continue
                            rows = corner_rows(box, 0, 1, xdir, zdir, face.boxes)
                            if not _fresh(made, 'corner_jump_cancel', rows):
                                continue
                            event = dict(case='corner_jump_cancel', input=0, plane=y0, higherHeight=face.plane,
                                         boxes=boxes, anchors={'beforeJump': 0, 'jump': 1})
                            choice = named_choice(context, 'stepping', initial, event, jumping=True, constraints=rows)
                            if choice:
                                yield choice
    for initial, jumping, trace in choice_approaches(context):
        for lower, name, members in faces:
            if context.get('checkBudget'):
                context['checkBudget']()
            if not feet_allowed(context, name):
                continue
            landing = first_descent(trace, lower.plane)
            if landing is None:
                continue
            if not any(interaction_allowed(context, box_for(b, context), landing, jumping=jumping,
                                           label=dict(owner=b['id'], role='lower landing')) for b in members):
                continue
            lower_boxes = [list(b) for b in lower.boxes]
            if (initial['onGround'] and not jumping and lower.plane < initial['position'] and
                    not walk_off_allowed(context, lower_boxes, label=dict(owner=name, role='lower landing'))):
                continue
            y = trace['position'][landing]
            for lower_rows, lower_regions in top_choices(context, lower, name, landing, 'case:lower_landing', jumping=jumping):
                yield from _open_flight_cases(context, initial, jumping, faces, lower, lower_boxes, lower_rows, lower_regions,
                                              landing, y, enabled)


def _open_flight_cases(context, initial, jumping, faces, lower, lower_boxes, lower_rows, lower_regions, landing, y, enabled):
    from parkour_solver.movement.contact_combinations import bounding_region
    from parkour_solver.reachability.service import entry_backward, interaction_allowed
    from parkour_solver.movement.normal import feet_allowed, named_choice
    import json
    scene = context['scene']
    for higher, name, members in faces:
        if higher.plane <= lower.plane or not feet_allowed(context, name):
            continue
        boxes, made, sequenced = [list(b) for b in higher.boxes], set(), {}
        def sequence_allowed(event, rows, support=None):
            # The common outcome is the same for every member of the face.
            key = json.dumps([event['case'], rows], sort_keys=True)
            if key not in sequenced:
                sequenced[key] = _sequence_allowed(context, initial, event, jumping, rows, support)
            return sequenced[key]
        for block in members:
            box = box_for(block, context)
            if not interaction_allowed(context, box, landing, jumping=jumping, label=dict(owner=block['id'], role='flight case')):
                continue
            if enabled('blip') and 0 <= higher.plane - y <= f32(.6):
                for top, regions in top_choices(context, higher, name, landing + 1, jumping=jumping):
                    shared_event=dict(case='blip',input=landing,plane=higher.plane,lowerHeight=lower.plane,
                        higherHeight=higher.plane,boxes=boxes,lowerBoxes=lower_boxes,
                        anchors={'landing':landing,'hit':landing+1})
                    # On L the player keeps clear of each exposed side of the higher block. A hit on it before the
                    # blip is a wall hit followed by a blip, deferred with intentional wall hits (user, 2026-11-22).
                    options = ([[outside_row(box, axis, sign, landing, 'blip:wall_avoid')]
                                for axis in ('X', 'Z') for sign in (-1, 1) if exposed_side(scene, block, axis, sign, y)]
                               if sequence_allowed(shared_event, lower_rows + top, name) else [])
                    for rows in options:
                        if not _fresh(made, 'blip', rows, top):
                            continue
                        event = dict(case='blip', input=landing, plane=higher.plane, lowerHeight=lower.plane,
                            higherHeight=higher.plane, boxes=boxes, lowerBoxes=lower_boxes,
                            anchors={'landing': landing, 'hit': landing + 1})
                        choice = named_choice(context, 'stepping', initial, event, jumping=jumping,
                            constraints=lower_rows + rows + top, support=name,
                            region_choices=lower_regions + regions)
                        if choice:
                            yield choice
            for case in ('blip_up', 'corner_blip_up'):
                if not enabled(case) or not (y > higher.plane if case == 'blip_up' else 0 <= higher.plane - y <= f32(.6)):
                    continue
                shared_event=dict(case=case,input=landing,plane=y,lowerHeight=lower.plane,higherHeight=higher.plane,
                    boxes=boxes,lowerBoxes=lower_boxes,anchors={'landing':landing,'hit':landing+1})
                tops = top_choices(context, higher, name, landing + 1, jumping=jumping) if case == 'blip_up' else [([], [])]
                if not any(sequence_allowed(shared_event, lower_rows + top) for top, _ in tops):
                    continue
                entered = lambda axis, sign: not entry_backward(context, axis, sign, bounding_region(higher.regions),
                                                                label=dict(owner=block['id'], role=case))
                options = ([([outside_row(box, axis, sign, landing, 'blip_up:outside')] + top, regions)
                            for axis in ('X', 'Z') for sign in (-1, 1)
                            if exposed_side(scene, block, axis, sign, y) and entered(axis, sign)
                            for top, regions in tops] if case == 'blip_up' else
                           [(corner_rows(box, landing, landing + 1, xdir, zdir, higher.boxes), [])
                            for xdir in (-1, 1) for zdir in (-1, 1)
                            if convex_corner(scene, block, xdir, zdir, y) and entered('X', -xdir)])
                for constraint, regions in options:
                    if not _fresh(made, case, constraint):
                        continue
                    event = dict(case=case, input=landing, plane=y, lowerHeight=lower.plane, higherHeight=higher.plane,
                                 boxes=boxes, lowerBoxes=lower_boxes, anchors={'landing': landing, 'hit': landing + 1})
                    choice = named_choice(context, 'stepping', initial, event, jumping=jumping,
                                          constraints=lower_rows + constraint, region_choices=lower_regions + regions)
                    if choice:
                        yield choice
