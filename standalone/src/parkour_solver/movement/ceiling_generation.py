from parkour_solver.physics.vertical import f32
from parkour_solver.movement.cases import exposed_side, outside_row
from parkour_solver.movement.contact_combinations import (bounding_region, contact_event, finite_rows, reachable_option,
                                                          region_choices, scene_contact_options)
from parkour_solver.reachability.service import behind_decision, candidate_option, entry_backward


def grind_options(context, feet, allowed=None):
    """(event, rows, regionChoices) for each exposed side of a member under each low merged underside; members whose
    sides share a plane give that side once."""
    scene = context['scene']
    blocks = {b['solidIndex']: b for b in scene['blocks']}
    for option in scene_contact_options(scene, 1, -1):
        if context.get('checkBudget'):
            context['checkBudget']()
        if not candidate_option(context, option, 'ceilingSolids'):
            continue
        clearance = option.plane - feet
        owner = '+'.join(sorted(blocks[i]['id'] for i in option.members))
        if not 0 <= clearance < f32(.42) or allowed is not None and not any(allowed(list(box), owner) for box in option.boxes):
            continue
        option = reachable_option(context, option, 1, jumping=True, label=dict(owner=owner, role='grind'))
        if option is None or behind_decision(context, option.regions, 1, jumping=True, label=dict(owner=owner, role='grind')):
            continue
        under = [dict(r, name='case:finite') for r in finite_rows(1, bounding_region(option.regions), t1=1)]
        regions = [dict(offset=1, surface=option.surface, owner=owner,
                        options=[[dict(r, name='case:finite') for r in finite_rows(1, region, leg=0, offset=1)]
                                 for region in option.regions])] if len(option.regions) > 1 and context.get('regionItems') else []
        sides = set()
        for index in option.members:
            box = scene['collisionScene']['solids'][index]
            for axis in ('X', 'Z'):
                for sign in (-1, 1):
                    if exposed_side(scene, blocks[index], axis, sign, option.plane) and not entry_backward(
                            context, axis, sign, bounding_region(option.regions), label=dict(owner=owner, role='grind')):
                        row = outside_row(box, axis, sign, 0, 'grind:outside')
                        if (axis, sign, row['rhs']) in sides:
                            continue
                        sides.add((axis, sign, row['rhs']))
                        event = dict(case='grind', input=0, plane=feet, clearance=clearance,
                                     boxes=[list(b) for b in option.boxes], anchors={'beforeJump': 0, 'jump': 1})
                        yield event, [row] + under, regions


class CeilingGenerator:
    def generate(self, context, *, start=0, ends=None):
        yield from open_ceiling_choices(context)

def open_ceiling_choices(context):
    from parkour_solver.reachability.service import interaction_allowed
    from parkour_solver.movement.program import scalar_initial
    from parkour_solver.movement.normal import choice_approaches, event_choice, named_choice
    initial = scalar_initial(context['state'])
    scene = context['scene']
    if not scene.get('ceilingHits', True):
        return
    names = {b['solidIndex']: b['id'] for b in scene['blocks']}
    for option in scene_contact_options(scene, 1, -1):
        if context.get('checkBudget'):
            context['checkBudget']()
        if not candidate_option(context, option, 'ceilingSolids'):
            continue
        plane, owner = option.plane, '+'.join(sorted(names[i] for i in option.members))
        for state, jumping, trace in choice_approaches(context):
            hit = next((t for t, move in enumerate(trace['movement'])
                        if move > 0 and trace['position'][t] <= plane < trace['position'][t + 1]), None)
            if hit is None:
                continue
            reached = reachable_option(context, option, hit, jumping=jumping, label=dict(owner=owner, role='ceiling'))
            if reached is None or behind_decision(context, reached.regions, hit, jumping=jumping,
                                                  label=dict(owner=owner, role='ceiling')):
                continue
            event = contact_event(reached, 'Y', 1, hit, 'ceiling', blockId=owner)
            choice = event_choice(context, 'ceiling', state, hit + 1, jumping=jumping, finite=[event],
                                  vertical=[dict(state=hit + 1, plane=plane, direction=1)],
                                  region_choices=region_choices(context, event, owner))
            if choice:
                yield choice
    if not initial['onGround']:
        return
    allowed = lambda box, owner: interaction_allowed(context, box, 0, jumping=True, label=dict(owner=owner, role='grind'))
    for event, rows, regions in grind_options(context, initial['position'], allowed):
        choice = named_choice(context, 'ceiling', initial, event, jumping=True, constraints=rows, region_choices=regions)
        if choice:
            yield choice
