"""Persistence adapter: full checkpoints at transitions, small repair updates.

An evaluation only changes its program's repairState and cumulative call count
in the outer DFS state. Store those changes against the dispatch checkpoint,
including every alias of the active program. Audit windows are immutable
snapshots created by DFS; archive each once outside numerical callbacks.

A native search publishes a snapshot (search/dfs_search.hpp) instead of a dict:
the store encodes it natively, and it names the paths of its repair program and
targets itself where a dict's are found by identity.
"""
from parkour_solver import native_core
from parkour_solver.io.checkpoint_store import StoredValue


class _NativeState:
    """A snapshot with its audit windows replaced by their stored references, for the store to encode."""
    def __init__(self, snapshot, windows):
        self.snapshot, self.windows = snapshot, windows

    def checkpoint_encode(self, known, cache=None):
        return self.snapshot.checkpoint_encode(known, [w.key for w in self.windows], cache)


class SearchCheckpoint:
    def __init__(self, store, metadata):
        self.store, self.metadata = store, metadata
        self.windows = []
        self.base, self.program = None, None
        self.paths = []
        self.token = None
        self.frozen = {}  # id -> (part, its native copy)
        self.saver = None  # the native saver of the current native search
        self.final = None  # (a native search's result state, its snapshot)

    def native_saver(self, outer):
        """A native search's saver on this checkpoint's native store (io/search_checkpoint.hpp), or None. The search
        then saves every transition itself, with no Python call; `outer` is the event state, whose dfs it fills."""
        if getattr(self.store, 'native', None) is None:
            return None
        self.sync()
        self.final = None  # the next search may change the state it named
        self.saver = native_core.from_environment().SearchSaver(self.store.native, self.metadata, outer,
                                                                [w.key for w in self.windows])
        return self.saver

    def native_final(self, state, snapshot):
        """A native search's result state (a dict) and its snapshot: a later save of that dict encodes the snapshot."""
        self.final = state, snapshot

    def sync(self):
        """Take the windows the native saver stored."""
        if self.saver is not None:
            self.windows.extend(StoredValue(self.store.identity, key) for key in self.saver.windows[len(self.windows):])

    def state(self, state, encode_natively=True):
        self.sync()
        dfs=state.get('dfs') if state.get('kind')=='event_route_search' else state
        if dfs is None:return dict(state)
        if self.final is not None and dfs is self.final[0]:
            dfs = self.final[1]  # the same state, encoded natively from its snapshot
        native = hasattr(dfs, 'checkpoint_encode')
        count = dfs.length(['windows']) if native else len(dfs['windows'])
        if count < len(self.windows):
            raise ValueError('Checkpoint audit windows cannot shrink')
        for i in range(len(self.windows), count):
            reference, _, _ = self.store.archive(dfs.value(['windows', i]) if native else dfs['windows'][i])
            self.windows.append(reference)
        core = native_core.from_environment() if native or encode_natively else None
        if core is None:
            return dict(state,dfs=dict(dfs, windows=self.windows)) if dfs is not state else dict(state,windows=self.windows)
        # The native core encodes the state with its windows replaced, a snapshot or a dict alike. The event search
        # never changes its domain or graph after building them: the store encodes one native copy of each, which its
        # encode cache then answers.
        stored = _NativeState(dfs if native else core.FrozenValue(dfs), list(self.windows))
        if dfs is state:
            return stored
        return dict(state, dfs=stored, **{k: self.freeze(core, state[k]) for k in ('domain', 'graph') if k in state})

    def freeze(self, core, part):
        held = self.frozen.get(id(part))
        if held is None or held[0] is not part:
            held = self.frozen[id(part)] = part, core.FrozenValue(part)
        return held[1]

    def __call__(self, state, program=None, extra_targets=()):
        dfs=state.get('dfs') if state.get('kind')=='event_route_search' else state
        native = dfs is not None and hasattr(dfs, 'checkpoint_encode')
        if not native:
            active = dfs['stack'][-1].get('inFlight') if dfs and dfs['stack'] else None
            program = program if program is not None else active['program'] if active else None
        # A dict state is found by identity in the save's walk only when it has a program or targets to locate.
        saved = dict(self.metadata, windows=self.windows,
                     searchState=self.state(state, encode_natively=native or (program is None and not extra_targets)))
        self.dfs_path=['searchState','dfs'] if dfs is not state else ['searchState']
        if native:
            self.base, _, _ = self.store.archive(saved)
            self.store.publish(self.base)
            repair = dfs.repair
            self.token, self.program = repair['token'], None
            self.paths = [self.dfs_path+path for path in repair['program']]
            self.extra_targets = [[self.dfs_path+path for path in paths] for paths in repair['targets']]
            return
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
            changes = [(path+['repairState'], dfs.part(path[inner:]+['repairState'])) for path in self.paths]
            changes += [(path, dfs.part(path[inner:])) for paths in self.extra_targets for path in paths]
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
