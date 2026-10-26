import json
import os
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[4]))

import numpy as np

from standalone.ipopt import worker
from standalone.ipopt.model import validate

original_send = worker.send
original_phase = worker.phase
phase_number = 0
active_problem = None
active_pattern = None
started = time.monotonic()
trace_path = pathlib.Path(os.environ['PKC_SEED_DIAGNOSTIC_TRACE'])


def summary(problem, yaw):
    result = validate(problem, yaw)
    return {k: v for k, v in result.items() if k not in ('yaw', 'positions', 'velocity')}


def record(event):
    event['seconds'] = time.monotonic() - started
    with trace_path.open('a') as stream:
        stream.write(json.dumps(event, allow_nan=False) + '\n')


def send(message):
    if message['type'] == 'candidate':
        nominal_p, _, _ = worker.forward(active_problem, message['yaw'], active_problem['threshold'], active_pattern)
        record({'type': 'candidate', 'phase': phase_number, 'yaw': message['yaw'],
                'replay': summary(active_problem, message['yaw']),
                'nominalEndX': float(nominal_p[-1, 0] + active_problem['position'][0])})
    original_send(message)


def phase(problem, yaw, pattern, seconds, stop):
    global phase_number, active_problem, active_pattern
    phase_number += 1
    active_problem, active_pattern = problem, pattern
    record({'type': 'phase_start', 'phase': phase_number, 'yaw': yaw,
            'pattern': pattern.tolist(), 'budget': seconds, 'replay': summary(problem, yaw)})
    result_yaw, status = original_phase(problem, yaw, pattern, seconds, stop)
    record({'type': 'phase_end', 'phase': phase_number, 'status': status,
            'yaw': result_yaw, 'replay': summary(problem, result_yaw)})
    return result_yaw, status


worker.send = send
worker.phase = phase
worker.main()
