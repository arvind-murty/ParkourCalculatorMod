"""Persistence adapter: full checkpoints at transitions, small repair updates.

An evaluation only changes its program's repairState and cumulative call count
in the outer DFS state. Store those changes against the dispatch checkpoint,
including every alias of the active program. Audit windows are immutable
snapshots created by DFS; archive each once outside numerical callbacks.

A native search publishes a snapshot (search/dfs_search.hpp) instead of a dict:
the store encodes it natively, and it names the paths of its repair program and
targets itself where a dict's are found by identity.
"""
class _NativeState:
    """A snapshot with its audit windows replaced by their stored references, for the store to encode."""
    def __init__(self, snapshot, windows):
        self.snapshot, self.windows = snapshot, windows

    def checkpoint_encode(self, known):
        return self.snapshot.checkpoint_encode(known, [w.key for w in self.windows])


class SearchCheckpoint:
    def __init__(self, store, metadata):
        self.store, self.metadata = store, metadata
        self.windows = []
        self.base, self.program = None, None
        self.paths = []
        self.token = None

    def state(self, state):
        dfs=state.get('dfs') if state.get('kind')=='event_route_search' else state
        if dfs is None:return dict(state)
        native = hasattr(dfs, 'checkpoint_encode')
        count = dfs.length(['windows']) if native else len(dfs['windows'])
        if count < len(self.windows):
            raise ValueError('Checkpoint audit windows cannot shrink')
        for i in range(len(self.windows), count):
            reference, _, _ = self.store.archive(dfs.value(['windows', i]) if native else dfs['windows'][i])
            self.windows.append(reference)
        stored = _NativeState(dfs, list(self.windows)) if native else dict(dfs, windows=self.windows)
        return dict(state,dfs=stored) if dfs is not state else stored if native else dict(state,windows=self.windows)

    def __call__(self, state, program=None, extra_targets=()):
        dfs=state.get('dfs') if state.get('kind')=='event_route_search' else state
        saved = dict(self.metadata, windows=self.windows, searchState=self.state(state))
        self.dfs_path=['searchState','dfs'] if dfs is not state else ['searchState']
        if dfs is not None and hasattr(dfs, 'checkpoint_encode'):
            self.base, _, _ = self.store.archive(saved)
            self.store.publish(self.base)
            repair = dfs.repair
            self.token, self.program = repair['token'], None
            self.paths = [self.dfs_path+path for path in repair['program']]
            self.extra_targets = [[self.dfs_path+path for path in paths] for paths in repair['targets']]
            return
        active = dfs['stack'][-1].get('inFlight') if dfs and dfs['stack'] else None
        program = program if program is not None else active['program'] if active else None
        self.base, _, paths = self.store.archive(saved, ([program] if program is not None else [])+list(extra_targets))
        self.store.publish(self.base)
        self.token = None
        self.program = program
        self.paths = paths.pop(0) if program is not None else []
        self.extra_targets = list(zip(paths, extra_targets))

    def begin_repair(self,state,program,extra_targets=()):
        self(state,program,extra_targets)

    def repair_progress(self, state):
        dfs=state.get('dfs') if state.get('kind')=='event_route_search' else state
        if dfs is not None and hasattr(dfs, 'checkpoint_encode'):
            repair = dfs.repair
            if self.base is None or not repair['patch'] or repair['token'] != self.token:
                self(state)
                return
            inner = len(self.dfs_path)
            changes = [(path+['repairState'], dfs.value(path[inner:]+['repairState'])) for path in self.paths]
            changes += [(path, dfs.value(path[inner:])) for paths in self.extra_targets for path in paths]
            changes.append((self.dfs_path+['cumulativeCalls'], dfs['cumulativeCalls']))
            self.store.patch(self.base, changes)
            return
        active = dfs['stack'][-1].get('inFlight') if dfs and dfs['stack'] else None
        if self.base is None or self.program is None or active and active['program'] is not self.program:
            # Defensive fallback; normal DFS publishes dispatch before repair.
            self(state)
            return
        changes = [(list(path)+['repairState'],self.program['repairState']) for path in self.paths]
        changes += [(list(path),target) for paths,target in self.extra_targets for path in paths]
        changes.append((self.dfs_path+['cumulativeCalls'],dfs['cumulativeCalls']))
        self.store.patch(self.base,changes)

    def finish(self, result, validation):
        state = self.state(result['searchState'])
        result = dict(result, windows=self.windows, searchState=state)
        self.store.save(dict(self.metadata,windows=self.windows,result=result,validation=validation))
        self.store.compact()
