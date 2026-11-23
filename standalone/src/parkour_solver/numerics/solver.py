import atexit
import collections
import json
import queue
import subprocess
import sys
import threading
import time

if __package__:
    from parkour_solver.schema import REPLAY_TOLERANCE, number
    from parkour_solver.numerics.model import fingerprint, prepare; from parkour_solver.validation.numerical import validate
else:
    from parkour_solver.schema import REPLAY_TOLERANCE, number
    from parkour_solver.numerics.model import fingerprint, prepare; from parkour_solver.validation.numerical import validate


def _command(python_executable):
    return [str(python_executable or sys.executable), '-u', '-m', 'parkour_solver.numerics.worker']


class _Worker:
    """A worker process and the reader threads feeding its message queue and stderr tail."""
    def __init__(self, python_executable):
        import os
        environment = os.environ.copy()
        environment['OPENBLAS_NUM_THREADS'] = '1'
        environment['OMP_NUM_THREADS'] = '1'
        self.messages = queue.Queue(maxsize=64)
        self.stderr = collections.deque(maxlen=20)
        self.process = subprocess.Popen(_command(python_executable), stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, encoding='utf-8', errors='replace', env=environment)
        self.output = threading.Thread(target=self._read_output, daemon=True)
        self.errors = threading.Thread(target=self._read_errors, daemon=True)
        self.output.start()
        self.errors.start()

    def publish(self, kind, value):
        try:
            self.messages.put_nowait((kind, value))
        except queue.Full:
            try:
                self.messages.get_nowait()
            except queue.Empty:
                pass
            try:
                self.messages.put_nowait((kind, value))
            except queue.Full:
                pass

    def _read_output(self):
        stream = self.process.stdout
        try:
            while True:
                line = stream.readline(2000001)
                if not line:
                    break
                if len(line) > 2000000:
                    self.publish('io_error', 'Oversized worker response')
                    break
                self.publish('message', line)
        except (OSError, ValueError) as failure:
            self.publish('io_error', str(failure))

    def _read_errors(self):
        try:
            for line in self.process.stderr:
                self.stderr.append(line[-400:].rstrip())
        except (OSError, ValueError):
            pass

    def send(self, problem, *, close):
        """Writes one request from a thread (a full pipe must not block the deadline); `close` ends the worker's input."""
        def write():
            try:
                self.process.stdin.write(json.dumps(problem, allow_nan=False, separators=(',', ':')) + '\n')
                if close:
                    self.process.stdin.close()
                else:
                    self.process.stdin.flush()
            except (OSError, ValueError) as failure:
                self.publish('io_error', 'Could not write worker request: ' + str(failure))
        threading.Thread(target=write, daemon=True).start()

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=0.2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=1)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            try:
                stream.close()
            except (OSError, ValueError):
                pass


# Kept-alive workers by Python executable; a call takes its worker out while it runs.
_WORKERS = {}


@atexit.register
def _close_workers():
    while _WORKERS:
        _WORKERS.popitem()[1].close()


def solve(request, *, seconds=None, stop_on_feasible=None, python_executable=None,
          cancel_event=None, on_incumbent=None, keep_alive=False, grace=0.):
    """One worker solve. `keep_alive` reuses one worker process per executable across calls; it is killed after any
    call that does not end with its `done` message. `grace` extends only the parent's wait for the worker's messages
    past `seconds`, which stays the worker's own budget."""
    overrides = dict(request)
    if seconds is not None:
        overrides['seconds'] = seconds
    if stop_on_feasible is not None:
        overrides['stopOnFeasible'] = stop_on_feasible
    problem = prepare(overrides)
    if number(grace, 'grace') < 0:
        raise ValueError('grace must be nonnegative')
    started = time.monotonic()
    deadline = started + problem['seconds'] + grace
    phases = []
    best = None
    first_feasible = None
    rejected = 0
    last_rejection = None
    termination = 'worker_finished'
    worker_status = None
    error = None
    cancelled = False
    worker = None
    stderr = []
    launched = reused = finished = False
    key = str(python_executable or sys.executable)

    def accept(candidate):
        nonlocal best, first_feasible
        if not candidate['feasible']:
            return
        direction = 1 if problem['objective']['sense'] == 'MAX' else -1
        if best is None or direction * candidate['objective'] > direction * best['objective']:
            if first_feasible is None:
                first_feasible = time.monotonic() - started
            best = candidate
            if on_incumbent is not None:
                on_incumbent(json.loads(json.dumps(best)))

    if cancel_event is None or not cancel_event.is_set():
        try:
            seed = validate(problem, problem['yaw'])
        except (ValueError, TypeError, OverflowError):
            seed = None
        if seed is not None:
            accept(seed)

    try:
        if cancel_event is not None and cancel_event.is_set():
            cancelled, termination = True, 'cancelled'
        elif best is not None and problem['stopOnFeasible']:
            termination = 'first_feasible'
        else:
            if keep_alive:
                worker = _WORKERS.pop(key, None)
                if worker is not None and worker.process.poll() is not None:
                    worker.close()
                    worker = None
            reused = worker is not None
            if worker is None:
                worker = _Worker(python_executable)
                launched = True
            else:
                worker.stderr.clear()
            worker.send(problem, close=not keep_alive)
            while worker.process.poll() is None or worker.output.is_alive() or not worker.messages.empty():
                if cancel_event is not None and cancel_event.is_set():
                    cancelled, termination = True, 'cancelled'
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    termination = 'time_limit'
                    break
                try:
                    kind, value = worker.messages.get(timeout=min(0.025, remaining))
                except queue.Empty:
                    continue
                if kind == 'io_error':
                    error, termination = value, 'worker_error'
                    break
                try:
                    message = json.loads(value)
                    message_type = message['type']
                    if message_type == 'candidate':
                        try:
                            candidate = validate(problem, message['yaw'])
                        except (ValueError, TypeError, OverflowError) as invalid:
                            rejected += 1
                            last_rejection = str(invalid)
                            continue
                        if not candidate['feasible']:
                            rejected += 1
                            last_rejection = {k: v for k, v in candidate.items()
                                              if k not in ('yaw', 'positions', 'velocity', 'y', 'vy', 'onGround',
                                                           'horizontalCollision', 'ladderPattern')}
                            continue
                        accept(candidate)
                        if problem['stopOnFeasible']:
                            termination = 'first_feasible'
                            break
                    elif message_type == 'phase':
                        phases.append(message)
                    elif message_type == 'done':
                        worker_status = str(message['status'])
                        if keep_alive:
                            finished = True
                            break
                    elif message_type == 'error':
                        error, termination = str(message['message']), 'worker_error'
                        break
                    else:
                        raise ValueError('Unknown worker response type')
                except (ValueError, KeyError, TypeError) as invalid:
                    error, termination = 'Invalid worker response: ' + str(invalid), 'protocol_error'
                    break
            if not finished:
                worker.errors.join(timeout=0.1)
            if termination == 'worker_finished' and not finished:
                if keep_alive:
                    error, termination = 'Worker exited before finishing the request', 'worker_error'
                elif worker.process.poll() not in (None, 0):
                    error, termination = 'Worker exited with code ' + str(worker.process.returncode), 'worker_error'
    except OSError as failure:
        error, termination = str(failure), 'runtime_error'
    finally:
        if worker is not None:
            if not (finished and termination == 'worker_finished' and _WORKERS.setdefault(key, worker) is worker):
                worker.close()
            stderr = list(worker.stderr)
    if cancelled:
        best = None
    status = 'cancelled' if cancelled else 'feasible' if best is not None else 'error' if error else 'unknown'
    return {'version': 1, 'status': status, 'feasible': best is not None, 'candidate': best,
            'objectiveMode': problem['objectiveMode'],
            'problemHash': fingerprint(problem), 'elapsedSeconds': time.monotonic() - started,
            'firstFeasibleSeconds': first_feasible,
            'problem': {'ticks': problem['ticks'], 'constraints': len(problem['walls']),
                        'contacts': len(problem['contacts']), 'threshold': problem['threshold'],
                        'objective': problem['objective'], 'source': problem.get('source')},
            'termination': termination, 'workerStatus': worker_status, 'workerLaunched': launched, 'workerReused': reused,
            'error': error,
            'tolerance': REPLAY_TOLERANCE, 'phases': phases,
            'diagnostics': {'rejectedCandidates': rejected, 'lastRejection': last_rejection,
                            'stderr': stderr}}
