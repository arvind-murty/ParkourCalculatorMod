def iter_movement_programs(scene, *, settings, report, initial_programs, continuation, deadline, cancel_event, work,
                           screen, context_adapter, program_filter, stop_supports, node_evaluator, priority_key,
                           policy_context, live_programs, run_policy, threshold=.005, native_node=None):
    """The anchor frame's persistent movement source (search/frontier.py)."""
    from parkour_solver.search.frontier import persistent_programs
    yield from persistent_programs(scene, settings=settings, report=report, continuation=continuation,
        deadline=deadline, cancel_event=cancel_event, work=work, initial_programs=initial_programs,
        threshold=threshold, screen=screen, context_adapter=context_adapter, program_filter=program_filter,
        stop_supports=stop_supports, node_evaluator=node_evaluator, priority_key=priority_key,
        policy_context=policy_context, live_programs=live_programs, run_policy=run_policy,
        native_node=native_node)


def screen_program(scene, program, compiler, seconds, *, gate=None, objective=None):
    """Compatibility entry point for the shared exact-proposal analysis."""
    from parkour_solver.reachability.service import InteractionGate
    return (gate or InteractionGate(scene,compiler,seconds=seconds)).check_program(program,objective)
