from parkour_solver.physics.vertical import f32
from parkour_solver.movement.cases import convex_corner, corner_rows, exposed_side, landing_wall_options, outside_row, top_choices


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


def open_stepping_choices(context):
    from parkour_solver.reachability.service import candidate_blocks, interaction_allowed, walk_off_allowed
    from parkour_solver.movement.program import scalar_initial
    from parkour_solver.movement.normal import choice_approaches, feet_allowed, first_descent, named_choice
    initial = scalar_initial(context['state'])
    scene = context['scene']
    y0 = initial['position']
    allowed = context.get('cases')
    def enabled(case):
        return allowed is None or case in allowed
    blocks = candidate_blocks(context, 'stepping')
    if initial['onGround']:
        for block in blocks:
            if not feet_allowed(context, block):
                continue
            box = box_for(block, context)
            rise = block['top'] - y0
            for case, jumping in [('normal_step', False), ('slab_boost', True)]:
                if (not enabled(case) or not 0 < rise <= f32(.6) or jumping and rise <= f32(.42) or
                        not jumping and not context.get('runTickChoices', True)):
                    continue
                if not interaction_allowed(context, box, 0, jumping=jumping, label=dict(owner=block['id'], role=case)):
                    continue
                for axis in ('X', 'Z'):
                    for direction in (-1, 1):
                        if not exposed_side(scene, block, axis, -direction, y0):
                            continue
                        for top, regions in top_choices(context, block, 1, jumping=jumping):
                            rows = [outside_row(box, axis, -direction, 0)] + top
                            event = dict(case=case, input=0, plane=block['top'], boxes=[box],
                                         anchors={'beforeJump' if jumping else 'beforeStep': 0, 'jump' if jumping else 'hit': 1})
                            choice = named_choice(context, 'stepping', initial, event, jumping=jumping, constraints=rows,
                                                  support=block['id'] if not jumping else None, region_choices=regions)
                            if choice:
                                yield choice
            if (enabled('corner_jump_cancel') and f32(.42) < rise <= f32(.6) and
                    interaction_allowed(context, box, 0, jumping=True, label=dict(owner=block['id'], role='corner_jump_cancel'))):
                for xdir in (-1, 1):
                    for zdir in (-1, 1):
                        if not convex_corner(scene, block, xdir, zdir, y0):
                            continue
                        event = dict(case='corner_jump_cancel', input=0, plane=y0, higherHeight=block['top'],
                                     boxes=[box], anchors={'beforeJump': 0, 'jump': 1})
                        choice = named_choice(context, 'stepping', initial, event, jumping=True,
                                              constraints=corner_rows(box, 0, 1, xdir, zdir))
                        if choice:
                            yield choice
    for initial, jumping, trace in choice_approaches(context):
        for lower in blocks:
            if context.get('checkBudget'):
                context['checkBudget']()
            if not feet_allowed(context, lower):
                continue
            landing = first_descent(trace, lower['top'])
            if landing is None:
                continue
            if not interaction_allowed(context, box_for(lower, context), landing, jumping=jumping,
                                       label=dict(owner=lower['id'], role='lower landing')):
                continue
            if (initial['onGround'] and not jumping and lower['top'] < initial['position'] and
                    not walk_off_allowed(context, [box_for(lower, context)], label=dict(owner=lower['id'], role='lower landing'))):
                continue
            y = trace['position'][landing]
            for lower_rows, lower_regions in top_choices(context, lower, landing, 'case:lower_landing', jumping=jumping):
                yield from _open_flight_cases(context, initial, jumping, blocks, lower, lower_rows, lower_regions, landing, y,
                                              enabled)


def _open_flight_cases(context, initial, jumping, blocks, lower, lower_rows, lower_regions, landing, y, enabled):
    from parkour_solver.reachability.service import interaction_allowed
    from parkour_solver.movement.normal import feet_allowed, named_choice
    scene = context['scene']
    for higher in blocks:
        if higher['top'] <= lower['top'] or not feet_allowed(context, higher):
            continue
        box = box_for(higher, context)
        if not interaction_allowed(context, box, landing, jumping=jumping, label=dict(owner=higher['id'], role='flight case')):
            continue
        if enabled('blip') and 0 <= higher['top'] - y <= f32(.6):
            for top, regions in top_choices(context, higher, landing + 1, jumping=jumping):
                shared_event=dict(case='blip',input=landing,plane=higher['top'],lowerHeight=lower['top'],
                    higherHeight=higher['top'],boxes=[box],lowerBox=box_for(lower,context),
                    anchors={'landing':landing,'hit':landing+1})
                options = ()
                if _sequence_allowed(context,initial,shared_event,jumping,lower_rows+top,higher['id']):
                    options = landing_wall_options(scene, higher, landing, y) if landing else (
                        ([outside_row(box, axis, sign, 0, 'blip:wall_avoid')], [])
                        for axis in ('X', 'Z') for sign in (-1, 1) if exposed_side(scene, higher, axis, sign, y))
                for rows, finite in options:
                    event = dict(case='blip', input=landing, plane=higher['top'], lowerHeight=lower['top'],
                        higherHeight=higher['top'], boxes=[box], lowerBox=box_for(lower, context),
                        anchors={'landing': landing, 'hit': landing + 1})
                    choice = named_choice(context, 'stepping', initial, event, jumping=jumping,
                        constraints=lower_rows + rows + top, finite=finite, support=higher['id'],
                        region_choices=lower_regions + regions)
                    if choice:
                        yield choice
        for case in ('blip_up', 'corner_blip_up'):
            if not enabled(case) or not (y > higher['top'] if case == 'blip_up' else 0 <= higher['top'] - y <= f32(.6)):
                continue
            shared_event=dict(case=case,input=landing,plane=y,lowerHeight=lower['top'],higherHeight=higher['top'],
                boxes=[box],lowerBox=box_for(lower,context),anchors={'landing':landing,'hit':landing+1})
            tops = top_choices(context, higher, landing + 1, jumping=jumping) if case == 'blip_up' else [([], [])]
            if not any(_sequence_allowed(context,initial,shared_event,jumping,lower_rows+top) for top, _ in tops):
                continue
            options = ([([outside_row(box, axis, sign, landing, 'blip_up:outside')] + top, regions)
                        for axis in ('X', 'Z') for sign in (-1, 1) if exposed_side(scene, higher, axis, sign, y)
                        for top, regions in tops] if case == 'blip_up' else
                       [(corner_rows(box, landing, landing + 1, xdir, zdir), [])
                        for xdir in (-1, 1) for zdir in (-1, 1) if convex_corner(scene, higher, xdir, zdir, y)])
            for constraint, regions in options:
                event = dict(case=case, input=landing, plane=y, lowerHeight=lower['top'], higherHeight=higher['top'],
                             boxes=[box], lowerBox=box_for(lower, context), anchors={'landing': landing, 'hit': landing + 1})
                choice = named_choice(context, 'stepping', initial, event, jumping=jumping,
                                      constraints=lower_rows + constraint, region_choices=lower_regions + regions)
                if choice:
                    yield choice
