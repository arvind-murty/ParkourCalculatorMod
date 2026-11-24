"""Shared test builders extracted from tests.unit.bridge.test_python_compiler."""


def shared_request(rng, n):
    grounded = [rng.random() < .5 for _ in range(n)]
    plan = dict(grounded=grounded, jump=[rng.random() < .4 for _ in range(n)],
                sceneContacts=[dict(axis=rng.choice([0, 1, 2]), state=rng.randint(1, n)) for _ in range(rng.randint(0, 4))])
    if rng.random() < .8:
        plan['sneak'] = [rng.random() < .15 for _ in range(n)]
    if rng.random() < .3:
        plan['ladderTicks'] = [rng.random() < .3 for _ in range(n + 1)]
    if rng.random() < .7:
        plan['vertical'] = dict(profile='minecraft-1.8.9', onGround=grounded[0],
                                previousHorizontalCollision=rng.random() < .3, threshold=0.,
                                contacts=[dict(state=1, plane=1, direction=-1)], sneak=None)
    return plan
