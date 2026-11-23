import collections
import copy

from parkour_solver.scene.geometry import expanded_box
from parkour_solver.reachability.intervals import interaction_reachability, interaction_reachability_many, reachable_envelopes


PROOF_POLICY = 5


def interaction_context_key(context):
    """Fingerprint all bounds/rows/probe uncertainty, including unknown Y."""
    import math
    from parkour_solver.canonical import digest
    def finite(value):
        if isinstance(value, float) and not math.isfinite(value):
            return {'nonfiniteBound': str(value)}
        if isinstance(value, dict):
            return {k: finite(v) for k, v in value.items()}
        if isinstance(value, (list, tuple)):
            return [finite(v) for v in value]
        return value
    return digest(dict(policy=PROOF_POLICY, context=finite(context)))


def analysis_key(prepared_key, open_terminal, scope, initial_intervals, parent_ticks):
    """An analysis named by the preparation it models (InteractionGate.analyze_model)."""
    return interaction_context_key(dict(prepared=prepared_key, openTerminal=open_terminal, scope=scope,
                                        initialIntervals=initial_intervals, parentTicks=parent_ticks))


def query_interaction(request, context, *, session=None, budget=None, context_key=None):
    """Conservative role/window query shared by routing and fixed generation.

    Unknown family contexts cannot borrow a fixed-program proof. The caller
    may supply a precomputed key only for this exact immutable context.
    """
    import copy
    from parkour_solver.canonical import digest
    role = request['role']
    if role not in ('required_contact', 'unwanted_contact', 'approach_prerequisite'):
        raise ValueError('Unknown interaction role')
    windows = request['windows']
    if any(type(w['tick']) is not int or w['tick'] < 0 or w['phase'] not in ('Y', 'X', 'Z') for w in windows):
        raise ValueError('Interaction windows require nonnegative ticks and Y/X/Z phases')
    context_key = context_key or interaction_context_key(context)
    key = digest(dict(policy=PROOF_POLICY, context=context_key, request=request))
    if session is not None and key in session:
        return copy.deepcopy(session[key])
    envelope = context.get('envelopes')
    phases = []
    status, reason = 'unknown', 'unknown_motion_family'
    if envelope is not None and windows:
        for window in windows:
            if budget is not None:
                budget()
            tick, phase = window['tick'], window['phase']
            if tick >= len(envelope['phases']):
                phases.append(dict(window, ruledOut=False, reason='window_outside_context'))
            else:
                checked = block_interaction(envelope, request['box'], tick, (phase,),
                    interior=request.get('interior', role == 'unwanted_contact'))
                phases.append(dict(checked, **window))
        status = 'absent' if all(p['ruledOut'] for p in phases) else 'not_ruled_out'
        reason = 'all_scoped_phases_excluded' if status == 'absent' else 'interaction_retained'
    proof = dict(policy=PROOF_POLICY, id=key, context=context_key, role=role, ruledOut=status == 'absent',
        status=status, reason=reason, phases=phases, owner=request.get('owner'),
        interior=request.get('interior', role == 'unwanted_contact'), invalidation='exact_context_only')
    if session is not None:
        session[key] = copy.deepcopy(proof)
    return proof


def block_interaction(envelopes, box, tick, phases=('Y', 'X', 'Z'), *, interior=False):
    return interaction_reachability(envelopes, dict(alternatives=[dict(regions=[
        dict(tick=tick, phase=phase, bounds=expanded_box(box), interior=interior)]) for phase in phases]))


def interaction_allowed(context, box, tick, *, label, jumping=False, phases=('Y', 'X', 'Z')):
    return bounds_allowed(context, expanded_box(box), tick, label=label, jumping=jumping, phases=phases)


def bounds_allowed(context, bounds, tick, *, label, jumping=False, phases=('Y', 'X', 'Z')):
    """`label` (owner, role) names the generator choice in the exclusion log."""
    if 'interactionGate' not in context:
        raise ValueError('Generation needs an interactionGate; set it to None to run without reachability')
    gate = context['interactionGate']
    return gate is None or not gate.query(context, bounds, tick, label=label, jumping=jumping, phases=phases)['ruledOut']


def bounds_allowed_many(context, bounds, tick, *, label, jumping=False, phases=('Y', 'X', 'Z')):
    if not bounds:
        return []
    if 'interactionGate' not in context:
        raise ValueError('Generation needs an interactionGate; set it to None to run without reachability')
    gate = context['interactionGate']
    return ([True]*len(bounds) if gate is None else [not proof['ruledOut'] for proof in
            gate.query_many(context, bounds, tick, label=label, jumping=jumping, phases=phases)])


def behind_decision(context, region, tick, *, jumping, label):
    """At an ordinary decision (`context['forward']`), whether reaching the reachable part of this player-centre region
    ([x0, z0, x1, z1]) at input `tick` needs more than the margin of travel in a move its guide leg disallows."""
    forward = context.get('forward')
    if forward is None:
        return False
    gate = context['interactionGate']
    at = context['program']['profile']['duration']+tick
    envelope = gate.approach(context, tick, jumping=jumping) if gate is not None else None
    reach = envelope['states'][at] if envelope is not None and at < len(envelope['states']) else None
    x0, z0, x1, z1 = region
    if reach is not None:
        x0, x1, z0, z1 = max(x0, reach['X'][0]), min(x1, reach['X'][1]), max(z0, reach['Z'][0]), min(z1, reach['Z'][1])
        if x0 > x1 or z0 > z1:
            return False
    (px, pz), lows, highs = forward['point'], dict(X=x0, Z=z0), dict(X=x1, Z=z1)
    point = dict(X=px, Z=pz)
    need, side = max((point[axis]-highs[axis] if sign < 0 else lows[axis]-point[axis], axis+('+' if sign > 0 else '-'))
                     for axis in ('X', 'Z') for sign in (-1, 1) if (axis, sign) not in forward['allowed'])
    if need <= forward['margin']:
        return False
    if gate is not None:
        gate.exclusions.append(dict(label, frame=context.get('frameId'), horizontal=list(region), input=at,
            proof=dict(reason='behind_decision_point', side=side, need=need, margin=forward['margin'])))
    return True


def entry_backward(context, axis, sign, region, *, label):
    """At an ordinary decision, whether entering a box from its `axis` `sign` side ([x0, z0, x1, z1] its player-centre
    region), so moving along -`sign`, is a move the decision's guide leg disallows."""
    forward = context.get('forward')
    if forward is None or (axis, -sign) in forward['allowed']:
        return False
    gate = context['interactionGate']
    if gate is not None:
        gate.exclusions.append(dict(label, frame=context.get('frameId'), horizontal=list(region),
            input=context['program']['profile']['duration'],
            proof=dict(reason='entry_against_guide', side=axis+('+' if sign > 0 else '-'))))
    return True


def walk_off_allowed(context, landing_boxes, *, label):
    """A no-jump run-off from the ground must leave its launch support on its first input (`label` as above)."""
    if 'interactionGate' not in context:
        raise ValueError('Generation needs an interactionGate; set it to None to run without reachability')
    gate = context['interactionGate']
    return gate is None or gate.walk_off(context, landing_boxes, label=label)


def candidate_faces(context, kind):
    """The merged top faces with a block in the launch face's table of `kind`; every face without a gate or launch face."""
    from parkour_solver.movement.contact_combinations import top_faces
    faces = top_faces(context['scene'])
    face = context.get('launchFace')
    if face is None or context['interactionGate'] is None:
        return faces
    kept = context['interactionGate'].reachable_faces(face)[kind]
    solids = {b['solidIndex'] for b in context['scene']['blocks'] if b['id'] in kept}
    return [f for f in faces if solids.intersection(f.members)]


def candidate_option(context, option, kind='solids'):
    face = context.get('launchFace')
    if face is None or context['interactionGate'] is None:
        return True
    solids = context['interactionGate'].reachable_faces(face)[kind]
    return any(i in solids for i in option.members)


def face_table(scene, name, traces):
    """Blocks one flight from face `name` can touch, by kind (generous: no route, as-given speed limits).

    Reach is measured from every block of the face's owners (contact_combinations.face_owners), so a face of
    one owner measures from all of that owner's blocks. Heights are relative to each launch block's top
    (feet): any block whose raw bottom is within head reach can be hit; `landing` tops are within the game's
    jump rise; `ceiling` bottoms lie between head height and head reach.
    `stepping` adds blip secondaries, touched one (air) tick after a landing-block
    primary. `traces` caches threshold-0 jump traces by top height.
    """
    import math
    from parkour_solver.movement.contact_combinations import face_owners
    from parkour_solver.movement.normal import JUMP_RISE, first_descent, replay
    from parkour_solver.physics.vertical import REST_CARRY, STEP_HEIGHT
    from parkour_solver.scene.geometry import BODY_HEIGHT
    from parkour_solver.spatial.surfaces import scene_geometry
    blocks = scene['blocks']
    geometry = scene_geometry(scene['collisionScene'])
    lowest = min(b['top'] for b in blocks)
    by_solid = {}
    for b in blocks:
        by_solid.setdefault(b['solidIndex'], []).append(b['id'])
    tops = {b['id']: b['top'] for b in blocks}
    solid_of = {b['id']: b['solidIndex'] for b in blocks}
    kept, landing, ceiling, primaries, ticks = set(), set(), set(), [], None
    owners = face_owners(scene, name)

    def near(box, radius):
        foot = [box[0], box[2], box[3], box[5]]
        for i in geometry.near([foot[0]-radius, foot[1]-radius, foot[2]+radius, foot[3]+radius]):
            other = geometry.boxes[i]
            dx = max(0., other[0]-foot[2], foot[0]-other[3])
            dz = max(0., other[2]-foot[3], foot[1]-other[5])
            if math.hypot(dx, dz) <= radius:
                yield i, other

    for block in blocks:
        if block.get('sourceBlock', block['id']) not in owners:
            continue
        kept.add(block['id'])
        if block['top'] not in traces:
            state = dict(position=block['top'], velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
            traces[block['top']] = replay(state, 200, jumping=True, threshold=0.)
        descent = first_descent(traces[block['top']], lowest)
        flight = 200 if descent is None else descent+1
        ticks = flight if ticks is None else min(ticks, flight)
        feet = block['top']
        for i, box in near(geometry.boxes[block['solidIndex']], .4+.7+.4*(flight-1)):
            bottom = box[1]+BODY_HEIGHT
            if bottom > feet+BODY_HEIGHT+JUMP_RISE:
                continue
            names = by_solid.get(i, ())
            kept.update(names)
            if feet+BODY_HEIGHT <= bottom:
                ceiling.update(names)
            for name in names:
                if tops[name] <= feet+JUMP_RISE:
                    landing.add(name)
                    primaries.append((name, feet+JUMP_RISE))
    stepping = set(landing)
    for name, peak in primaries:
        for i, _ in near(geometry.boxes[solid_of[name]], .4):
            stepping.update(n for n in by_solid.get(i, ()) if tops[n] <= peak+STEP_HEIGHT)
    solids = lambda names: frozenset(solid_of[n] for n in names)
    return dict(blocks=frozenset(kept), solids=solids(kept), ticks=ticks, landing=frozenset(landing),
                stepping=frozenset(stepping), ceiling=frozenset(ceiling), ceilingSolids=solids(ceiling))


def walk_off_tick(program):
    """The program's last run-off walk-off tick (grounded, no jump, descending) without a carried
    face selection, else None."""
    p=program['profile']
    carried={w['offset'] for w in program.get('walkOffs',[])}
    cut=next((c for c in reversed(range(p['duration'])) if p['grounded'][c] and not p['jump'][c]
              and not p['ladderTicks'][c] and p['position'][c+1]<p['position'][c]),None)
    return None if cut in carried else cut


def landing_boxes(program, cut):
    """The boxes a run-off from `cut` first lands on: its landing or step contact, or a flight case's
    lower face; None when it lands on nothing modeled."""
    p=program['profile']
    events=[(e['offset'],e['boxes']) for e in p['contactEvents'] if e['offset']>=cut and e.get('role') in ('landing','step') and 'boxes' in e]
    events+=[(e['input'],e['lowerBoxes']) for e in p['caseEvents'] if e['input']>=cut and 'lowerBoxes' in e]
    return min(events,key=lambda e:e[0])[1] if events else None


class InteractionGate:
    """Session-shared preparation, coefficient and exact-model proof cache.

    Under PKC_NATIVE_CORE, for a collision-box scene and a compiler whose replies equal the native tables, a native
    gate (`core`) keeps the scene's preparations, analyses and program screens in its own cache and metrics; `cores`
    ({scene digest: core}) shares one across the gates of a search, as `cache` and `metrics` are shared."""
    def __init__(self, scene, compiler, *, seconds=1, cores=None):
        from parkour_solver import native_core
        from parkour_solver.canonical import digest
        self.scene, self.compiler, self.seconds = scene, compiler, seconds
        self.native = native_core.from_environment()
        self.cache = {}
        self.metrics = {}
        self.checked = 0
        self.exclusions = []
        self.core = None
        if (self.native is not None and scene.get('sceneModel')=='collision_boxes'
                and getattr(compiler,'table_coefficients',False)):
            cores = {} if cores is None else cores
            key = digest({k: v for k, v in scene.items() if k != 'movementSearch'})
            self.core = cores.get(key) or cores.setdefault(key, self.native.InteractionGate(scene))
        # This gate's share of the core's query log: the core is shared across a search's gates.
        self.core_start = (self.core.checked, self.core.excluded) if self.core is not None else (0, 0)
        from parkour_solver.numerics.disk import stats
        self.relaxation_start = {kind: collections.Counter(counts) for kind, counts in stats().items()}
        self.face_tables, self.jump_traces = {}, {}
        self.approaches = {}
        self.native_parents = {}  # id(program) -> (program, its native program); see _sequenced

    def reachable_faces(self, name):
        if name in self.face_tables:
            return self.face_tables[name]
        if self.core is not None:
            table = self.core.reachable_faces(name)
            self.face_tables[name] = {k: frozenset(v) if isinstance(v, set) else v for k, v in table.items()}
            return self.face_tables[name]
        import time
        started = time.process_time()
        self.face_tables[name] = face_table(self.scene, name, self.jump_traces)
        # Reports and profiling read these metric names.
        self.metrics['blockTableBuilds'] = self.metrics.get('blockTableBuilds', 0)+1
        self.metrics['blockTableSeconds'] = self.metrics.get('blockTableSeconds', 0.)+time.process_time()-started
        return self.face_tables[name]

    def _cached(self, key, build, kind):
        hit = key in self.cache
        name=kind+('Hits' if hit else 'Misses')
        self.metrics[name]=self.metrics.get(name,0)+1
        if hit:
            value=self.cache.pop(key)
        else:
            value=build()  # A timeout never commits a result or advances a cursor.
        self.cache[key]=value
        while len(self.cache)>512:
            self.cache.pop(next(iter(self.cache)))
        return value

    def prepare(self, program, *, scene=None, require_complete=False):
        """The preparation with this program's own warm yaw: the cache key is physical, so a cached route carries
        the guideSeedYaw of the program that built it."""
        prepared=self._prepared(program,scene=scene,require_complete=require_complete)
        if prepared is None:
            return None
        prepared=prepared if isinstance(prepared,dict) else prepared.to_python()
        route={k:v for k,v in prepared['route'].items() if k!='warmYaw'}
        if 'guideSeedYaw' in program:
            route['warmYaw']=program['guideSeedYaw'][:]
        return dict(prepared,route=route)

    def _prepared(self, program, *, scene=None, require_complete=False):
        """The cached preparation: a dict, or under PKC_NATIVE_CORE a native one for the routes it covers
        (collision-box scenes without ladder events), whose dict is built only when Python reads it. `program` may
        be a native program from `_sequenced`."""
        from parkour_solver.movement.program import compile_program, program_physics_key
        from parkour_solver.scene.route import prepare_route, schedule
        from parkour_solver.validation.vertical import validate_vertical_schedule
        from parkour_solver.canonical import digest
        if self.core is not None and scene is None:
            return self.core.prepare(program,require_complete)
        scene=self.scene if scene is None else scene
        typed=not isinstance(program,dict)
        key=('prepared',digest(dict(scene={k:v for k,v in scene.items() if k!='movementSearch'},
            program=program.physics_key() if typed else program_physics_key(program),complete=require_complete)))
        def build():
            if typed or (self.native is not None and scene['sceneModel']=='collision_boxes'
                         and not program.get('ladderEvents')):
                return self.native.prepare(scene,program,require_complete,key[1])
            compiled=compile_program(scene,program,require_complete=require_complete)
            route=prepare_route(compiled['route']);plan=schedule(route,[0])
            vertical=validate_vertical_schedule(route,compiled['profiles'],plan)
            if not vertical['feasible']:return None
            plan['vertical']=dict(vertical['vertical'],referencePosition=vertical['y'],referenceVelocity=vertical['vy'])
            return dict(key=key[1],route=route,profiles=compiled['profiles'],plan=plan,vertical=vertical)
        return self._cached(key,build,'preparation')

    def analyze_model(self, route, plan, vertical, *, open_terminal=False, scope=None, initial_intervals=None,
                      prepared_key=None, parent_ticks=None):
        """`prepared_key`: the `prepare` key that produced route, plan and vertical. The model is a function of it,
        so it names the analysis instead of the model's own content. `parent_ticks`: see `reachable_envelopes`."""
        from parkour_solver.bridge.compiler import coefficient_request
        from parkour_solver.canonical import digest
        def build_model():
            request=coefficient_request(route,plan)
            coefficient=self._cached(('coefficients',digest(request)),
                lambda:self.compiler.compile(route,plan,self.seconds),'coefficient')
            if not isinstance(coefficient,dict):coefficient=coefficient.to_python()  # cached by a native analysis
            rows=[r for r in plan['walls'] if not open_terminal or not r.get('name','').startswith('landing:1:')]
            result=dict(coefficient,ticks=plan['ticks'],walls=rows,contacts=plan['contacts'],
                position=route['initial']['position'][::2],velocity=route['initial']['velocity'][::2],
                threshold=route.get('physicsThreshold',.005),vertical=vertical['vertical'],
                ladderTicks=plan.get('ladderTicks',[False]*(plan['ticks']+1)))
            if initial_intervals is not None:result['initialIntervals']=initial_intervals
            return result,coefficient
        built=None
        if prepared_key is not None:
            key=analysis_key(prepared_key,open_terminal,scope,initial_intervals,parent_ticks)
        else:
            built=build_model()
            key=interaction_context_key(dict(model=built[0],y=vertical['y'],scope=scope,parentTicks=parent_ticks))
        def build():
            model,coefficient=built or build_model()
            envelope=reachable_envelopes(model,vertical['y'],parent_ticks=parent_ticks)
            envelope['modelKey']=digest(coefficient)
            return dict(key=key,model=model,envelopes=envelope,coefficient=coefficient,y=vertical['y'])
        return self._cached(('analysis',key),build,'analysis')

    def analyze(self, program, *, scene=None, require_complete=False, open_terminal=True, scope=None):
        analysis=self._analysis(program,scene=scene,require_complete=require_complete,open_terminal=open_terminal,
                                scope=scope)
        return analysis if analysis is None or isinstance(analysis,dict) else analysis.to_python()

    def _analysis(self, program, *, scene=None, require_complete=False, open_terminal=True, scope=None):
        """analyze's cached result: a dict, or a native analysis when the preparation is native."""
        if self.core is not None and scene is None and not require_complete and open_terminal and scope is None:
            return self.core.analysis(program)
        prepared=self._prepared(program,scene=scene,require_complete=require_complete)
        if prepared is None:return None
        parent_ticks=program.get('parentTicks') if isinstance(program,dict) else program.parent_ticks
        # A native analysis compiles coefficients from the native tables, so only for a compiler whose replies
        # equal them; any other compiler (test doubles) gets the Python model of the same preparation.
        if isinstance(prepared,dict) or not getattr(self.compiler,'table_coefficients',False):
            if not isinstance(prepared,dict):prepared=prepared.to_python()
            return self.analyze_model(prepared['route'],prepared['plan'],prepared['vertical'],
                                      open_terminal=open_terminal,scope=scope,prepared_key=prepared['key'],
                                      parent_ticks=parent_ticks)
        key=analysis_key(prepared.key,open_terminal,scope,None,parent_ticks)
        def build():
            coefficient=self._cached(('coefficients',prepared.coefficient_digest()),prepared.compile_coefficients,
                                     'coefficient')
            if isinstance(coefficient,dict):coefficient=self.native.coefficients(coefficient)  # cached by Python
            return prepared.analyze(coefficient,open_terminal,parent_ticks,key)
        return self._cached(('analysis',key),build,'analysis')

    def condition(self, analysis, rows):
        """Add X/Z prerequisites and repropagate, retaining the compiled physics."""
        model=dict(analysis['model'],walls=list(analysis['model']['walls'])+list(rows))
        key=interaction_context_key(dict(model=model,y=analysis['y']))
        from parkour_solver.reachability.screen import condition_envelopes
        return self._cached(('conditioned',key),lambda:dict(analysis,key=key,model=model,
            **condition_envelopes(analysis['envelopes'],rows,refine=False)),'conditioned')

    def envelopes(self, program):
        analysis=self.analyze(program)
        return analysis['envelopes'] if analysis else None

    def sequence(self, context, choice):
        """Screen a shared named outcome before multiplying its side variants."""
        if context.get('checkBudget'):context['checkBudget']()
        program=self._sequenced(context['program'],choice)
        self.metrics['sequenceChecks']=self.metrics.get('sequenceChecks',0)+1
        proof=dict(reason='composition_rejected') if program is None else self.required(program)
        if proof:
            self.metrics['sequenceExclusions']=self.metrics.get('sequenceExclusions',0)+1
            self.exclusions.append(dict(reason='joint_prerequisites_unreachable',proof=proof,frame=context.get('frameId'),
                input=context['program']['profile']['duration'],cases=choice['profile'].get('caseEvents',[])))
        return proof

    def _sequenced(self, parent, choice):
        """append_choice(parent, choice), built only for this screen. Under PKC_NATIVE_CORE, when the gate screens
        natively, it is a native program. The parent's native program is kept by identity: what a screen reads of a
        program (its states, trace, threshold and seed yaw) never changes once it is built."""
        from parkour_solver.movement.program import append_choice
        if (self.native is not None and self.scene['sceneModel']=='collision_boxes'
                and getattr(self.compiler,'table_coefficients',False)):
            entry=self.native_parents.pop(id(parent),None)
            if entry is None or entry[0] is not parent:
                entry=parent,self.native.program(parent)
            self.native_parents[id(parent)]=entry
            while len(self.native_parents)>64:
                self.native_parents.pop(next(iter(self.native_parents)))
            try:
                return entry[1].append_choice(choice)
            except self.native.NativeUnsupported:  # a ceiling a ladder climb owns outside a ladder append
                pass
        return append_choice(parent,choice)

    def check_program(self, program, objective=None):
        """None or a rejection proof. A run-off's launch-support face selection is appended to program['walkOffs']."""
        if self.core is not None:
            proof,selection=self.core.check_program(program,objective)
            if selection is not None:
                program['walkOffs']=program.get('walkOffs',[])+[selection]
            return proof
        analysis=self._analysis(program)
        if analysis is None:return dict(reason='vertical_schedule')
        def materialized():  # Python readers need the analysis dict, built once for a native analysis
            return analysis if isinstance(analysis,dict) else analysis.to_python()
        from parkour_solver.canonical import digest
        objective=self.scene['objective'] if objective is None else objective
        cut=walk_off_tick(program)
        key=('screen',analysis['key'] if isinstance(analysis,dict) else analysis.key,digest(objective),cut)
        def relax(rows):
            self.metrics['relaxationChecks']=self.metrics.get('relaxationChecks',0)+1
            return self.relaxation(program,materialized(),objective,rows)
        def build():
            proof=self.required(program)
            if proof:return proof,None
            faces=None
            if cut is not None:
                from parkour_solver.constraints.contacts import walk_off_faces
                faces=walk_off_faces(self.scene,program,cut,materialized()['envelopes'],landing_boxes(program,cut))
            if faces is None:
                if self.scene.get('maxWallHits',0):return None,None
                checked=relax([])
                return (checked['proof'] if checked is not None and checked['outcome']=='infeasible' else None),None
            if not faces['options']:
                return dict(reason='launch_support_not_left',input=cut,solids=faces['solids']),None
            selection=dict(offset=cut,solids=faces['solids'],options=[])
            proof=None
            for row in faces['options']:
                checked=None if self.scene.get('maxWallHits',0) else relax([row])
                if checked is not None and checked['outcome']=='infeasible':
                    proof=checked['proof']
                    continue
                selection['options'].append(row)
                # Without the relaxed screen (wall hits, or outside its class) every open reachable face goes to the
                # node solve; restricting where the screen runs widens what reaches this fallback.
                if checked is not None and not faces['diagonal']:
                    break
            return (None,selection) if selection['options'] else (proof,None)
        proof,selection=self._cached(key,build,'screen')
        if selection is not None:
            program['walkOffs']=program.get('walkOffs',[])+[copy.deepcopy(selection)]
        return proof

    def relaxation(self, program, analysis, objective, rows=()):
        """The disk relaxation of the request a decision check's repair loop sends for this program with
        avoidance `rows` (numerics.builders.numerical_problem), optimizing `objective`: one Clarabel solve
        that the repair loop's identical request finds in the content cache. None outside the supported class."""
        import copy
        from parkour_solver.numerics.disk import screen
        from parkour_solver.scene.route import schedule
        from parkour_solver.scene.route import derive_footprints
        from parkour_solver.numerics.builders import numerical_problem
        prepared=self.prepare(program)
        route=dict(copy.deepcopy(prepared['route']),objective=dict(axis='X',sense='MAX'),guideObjective=objective)
        route['constraints'].extend(copy.deepcopy(list(rows)))
        if hasattr(self.compiler,'derive_footprints'):
            derive_footprints(route,self.compiler,self.seconds)
        plan=schedule(route,[0])
        plan['vertical']=prepared['plan']['vertical']
        return screen(numerical_problem(route,plan,analysis['coefficient']))

    def approach(self, context, tick, *, jumping=False):
        """The envelope of the prefix plus a free approach covering input `tick`: prefix inputs and free approach
        inputs are known, the proposed interaction has not changed the height/ground state yet. None if unknown."""
        from parkour_solver.movement.program import append_choice, program_physics_key
        from parkour_solver.movement.normal import approaches, event_choice, first_descent
        from parkour_solver.canonical import digest
        prefix = context['program']
        if '_prefixKey' not in context:
            context['_prefixKey'] = (digest(program_physics_key(prefix)), context.get('threshold', .005))
        prefix_key = context['_prefixKey']
        def remember(key, build):
            if key not in self.approaches:
                self.approaches[key] = build()
                while len(self.approaches) > 512:
                    self.approaches.pop(next(iter(self.approaches)))
            return self.approaches[key]
        def approach(inputs):
            def build():
                self.metrics['approachBuilds'] = self.metrics.get('approachBuilds', 0)+1
                choice = event_choice(context, 'normal', context['state'], inputs, jumping=jumping)
                p = append_choice(prefix, choice) if choice is not None else None
                return self.envelopes(p) if p is not None else None
            return remember((prefix_key, jumping, inputs), build)
        def horizon():
            lowest = min(b['top'] for b in context['scene']['blocks'])
            trace = next((t for _, j, t in approaches(context) if j == jumping), None)
            landing = first_descent(trace, lowest) if trace is not None else None
            return context.get('maxInputs', 0) if landing is None else min(landing+2, context.get('maxInputs', 0))
        horizon = remember((prefix_key, jumping, 'horizon', context.get('maxInputs', 0)), horizon)
        envelope = approach(horizon) if tick < horizon else None
        if envelope is None:
            self.metrics['approachFallbacks'] = self.metrics.get('approachFallbacks', 0)+1
            envelope = approach(tick+1)
        return envelope

    def query(self, context, bounds, tick, *, label, jumping=False, phases=('Y', 'X', 'Z')):
        return self.query_many(context, [bounds], tick, label=label, jumping=jumping, phases=phases)[0]

    def query_many(self, context, bounds, tick, *, label, jumping=False, phases=('Y', 'X', 'Z')):
        requests, prepared = [], []
        # Retain per-query cache/fallback accounting while sharing the native check.
        # Use the approach's horizontal envelope, keeping Y unconstrained for stepping and ceiling outcomes.
        for box in bounds:
            envelope = self.approach(context, tick, jumping=jumping)
            box = list(box)
            box[1], box[4] = -float('inf'), float('inf')
            at = context['program']['profile']['duration']+tick
            prepared.append((envelope, box, at))
            requests.append(dict(alternatives=[dict(regions=[dict(tick=at, phase=phase, bounds=box)])
                                                for phase in phases]))
        proofs = []
        index = 0
        while index < len(prepared):
            envelope = prepared[index][0]
            end = index+1
            while end < len(prepared) and prepared[end][0] is envelope:
                end += 1
            checked = ([dict(ruledOut=False, reason='unknown_approach') for _ in range(index, end)]
                       if envelope is None else interaction_reachability_many(envelope, requests[index:end]))
            for (current, box, at), proof in zip(prepared[index:end], checked):
                if current is not None:
                    self.checked += 1
                    if proof['ruledOut']:
                        self.exclusions.append(dict(label, frame=context.get('frameId'),
                            horizontal=[box[0], box[2], box[3], box[5]], input=at, proof=proof))
                proofs.append(proof)
            index = end
        return proofs

    def walk_off(self, context, landing_boxes, *, label):
        """Whether a run-off from the context's ground state onto `landing_boxes` can leave its launch support
        on its first input (constraints.contacts.walk_off_faces on the no-jump approach envelope)."""
        from parkour_solver.constraints.contacts import walk_off_faces
        envelope = self.approach(context, 0)
        if envelope is None:
            return True
        cut = context['program']['profile']['duration']
        faces = walk_off_faces(context['scene'], context['program'], cut, envelope, landing_boxes)
        self.checked += 1
        if faces is None or faces['options']:
            return True
        self.exclusions.append(dict(label, frame=context.get('frameId'), horizontal=faces['region'], input=cut,
                                    proof=dict(ruledOut=True, reason='launch_support_not_left', tick=cut, solids=faces['solids'])))
        return False

    def required(self, program):
        if self.core is not None:
            return self.core.required(program)
        analysis = self._analysis(program)
        if analysis is None:
            return dict(reason='vertical_schedule')
        if not isinstance(analysis, dict):
            return analysis.required()
        envelope = analysis['envelopes']
        profile = program['profile']
        if envelope.get('contradiction'):
            return dict(reason='required_interaction_unreachable',input=envelope['contradiction'].get('state'),
                        proof=dict(ruledOut=True,**envelope['contradiction']))
        context = dict(envelopes=envelope)
        # The envelope is a function of its analysis, so the analysis key names this proof context.
        context_key = analysis['key']
        for event in profile.get('contactEvents', [])+profile.get('caseEvents', []):
            if 'boxes' not in event:
                continue
            tick = event.get('offset', event.get('input'))
            proofs = [query_interaction(dict(box=box, role='required_contact',
                windows=[dict(tick=tick, phase=p) for p in ('Y', 'X', 'Z')], interior=False),
                context, context_key=context_key) for box in event['boxes']]
            if all(proof['status'] == 'absent' for proof in proofs):
                return dict(reason='required_interaction_unreachable', input=tick, boxes=event['boxes'], proof=proofs[0])
        return None

    def context(self, context):
        context['interactionGate'] = self

    def summary(self):
        from parkour_solver.numerics.disk import stats
        metrics,checked,exclusions=collections.Counter(self.metrics),self.checked,self.exclusions
        if self.core is not None:
            metrics.update(self.core.metrics)
            checked+=self.core.checked-self.core_start[0]
            exclusions=exclusions+self.core.exclusions_since(self.core_start[1])
        return dict(checked=checked,excluded=len(exclusions),exclusions=exclusions,metrics=dict(metrics),
                    relaxation={kind: dict(counts-self.relaxation_start[kind]) for kind, counts in stats().items()})
