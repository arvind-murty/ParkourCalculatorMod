"""Persistence adapter: full checkpoints at transitions, small repair updates.

An evaluation only changes its program's repairState and cumulative call count
in the outer DFS state. Store those changes against the dispatch checkpoint,
including every alias of the active program. Audit windows are immutable
snapshots created by DFS; archive each once outside numerical callbacks.
"""
def identity_paths(value, target):
    memo = {}

    def paths(node):
        if node is target:
            return [()]
        if not isinstance(node, (dict,list,tuple)):
            return []
        if id(node) not in memo:
            children = node.items() if isinstance(node,dict) else enumerate(node)
            memo[id(node)] = [(key,)+tail for key,child in children for tail in paths(child)]
        return memo[id(node)]

    return paths(value)


class SearchCheckpoint:
    def __init__(self, store, metadata):
        self.store, self.metadata = store, metadata
        self.windows = []
        self.base, self.program = None, None
        self.paths = []

    def state(self, state):
        if len(state['windows']) < len(self.windows):
            raise ValueError('Checkpoint audit windows cannot shrink')
        for window in state['windows'][len(self.windows):]:
            reference, _ = self.store.archive(window)
            self.windows.append(reference)
        return dict(state, windows=self.windows)

    def __call__(self, state):
        saved = dict(self.metadata, windows=self.windows, searchState=self.state(state))
        self.base, _ = self.store.archive(saved)
        self.store.publish(self.base)
        active = state['stack'][-1].get('inFlight') if state['stack'] else None
        self.program = active['program'] if active else None
        self.paths = identity_paths(saved,self.program) if self.program is not None else []

    def repair_progress(self, state):
        active = state['stack'][-1]['inFlight']
        if self.base is None or active['program'] is not self.program:
            # Defensive fallback; normal DFS publishes dispatch before repair.
            self(state)
            return
        changes = [(list(path)+['repairState'],self.program['repairState']) for path in self.paths]
        changes.append((['searchState','cumulativeCalls'],state['cumulativeCalls']))
        self.store.patch(self.base,changes)

    def finish(self, result, validation):
        state = self.state(result['searchState'])
        result = dict(result, windows=self.windows, searchState=state)
        self.store.save(dict(self.metadata,windows=self.windows,result=result,validation=validation))
        self.store.compact()
