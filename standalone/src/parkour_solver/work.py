"""Search budgets and compiler accounting, independent of route adapters."""

import collections
import contextlib
import hashlib
import json
import time

# Work-only mode (case setting `work_only`): no time limit decides an outcome, only work counts do. Process-wide
# because the disk screens and their decided cache are; find_checkpoint_route sets it for the duration of a run.
WORK_ONLY = False


@contextlib.contextmanager
def work_only(enabled):
    """WORK_ONLY = enabled for the block. The disk's decided cache holds verdicts under the time limits in force, so a
    change of mode empties it (and the native core's)."""
    previous = WORK_ONLY

    def switch(value):
        global WORK_ONLY
        if value == WORK_ONLY:
            return
        WORK_ONLY = value
        from parkour_solver import native_core
        from parkour_solver.numerics import disk
        disk.clear_decided()
        native = native_core.from_environment()
        if native is not None:
            native.set_work_only(value)

    switch(enabled)
    try:
        yield
    finally:
        switch(previous)


class SearchWork:
    def __init__(self, max_solver_calls=None, *, deadline=None):
        if max_solver_calls is not None and (type(max_solver_calls) is not int or max_solver_calls < 1):
            raise ValueError('max_solver_calls must be a positive integer')
        self.max_solver_calls = max_solver_calls
        self.deadline = deadline
        self.started = time.monotonic()
        self.termination = None
        self.counts = collections.Counter()
        self.program_keys = set()
        self.first_proposal = None
        self.first_accepted = None
        self.program_domains = collections.Counter()

    def check(self):
        if self.deadline is not None and time.monotonic() >= self.deadline:
            self.termination = 'deadline'
        if self.max_solver_calls is not None and self.counts['solverCalls'] >= self.max_solver_calls:
            self.termination = 'numerical_call_limit'
        if self.termination:
            raise TimeoutError(self.termination)

    def program(self, program):
        profile = program['profile']
        key = hashlib.sha256(json.dumps([profile, program['ladderEvents']], sort_keys=True).encode()).hexdigest()
        self.program_keys.add(key)
        self.counts['completePrograms'] += 1
        self.counts['completeProgramInputs'] += profile['duration']
        for u in program['movementUnits']:
            self.program_domains[u['type']] += 1
        self.counts['selectedFiniteContacts'] += len(profile.get('contactEvents', []))
        self.counts['selectedSneakInputs'] += sum(profile.get('sneak', []))

    def proposal(self, problem):
        self.counts['numericalProposals'] += 1
        if self.first_proposal is None:
            self.first_proposal = time.monotonic() - self.started

    def before_solve(self, problem):
        self.check()
        self.counts['solverCalls'] += 1

    def accepted(self, best):
        self.counts['acceptedBranches'] += 1
        if self.first_accepted is None:
            self.first_accepted = time.monotonic() - self.started

    def summary(self):
        return dict(self.counts, uniqueCompletePrograms=len(self.program_keys),
            unitsByType=dict(self.program_domains), firstProposalSeconds=self.first_proposal,
            firstAcceptedSeconds=self.first_accepted, maxSolverCalls=self.max_solver_calls,
            termination=self.termination)
