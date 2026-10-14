"""Job agent running INSIDE Windows Sandbox (started by the .wsb LogonCommand).

Stable outer loop only: the job logic lives in sbx_runner.py and is re-loaded
for every job, so it can change without restarting the sandbox.
"""
import os
import runpy
import time
import traceback

SBX = r'C:\sbx'
JOBS = os.path.join(SBX, 'jobs')
DONE = os.path.join(SBX, 'done')
RUNNER = r'C:\bcsrc\tools\sbx_runner.py'


def status(msg):
    with open(os.path.join(SBX, 'agent_status.txt'), 'a') as f:
        f.write('%s %s\n' % (time.strftime('%H:%M:%S'), msg))


def main():
    os.makedirs(JOBS, exist_ok=True)
    os.makedirs(DONE, exist_ok=True)
    status('agent start')
    try:
        runpy.run_path(RUNNER)['initial_sync'](status)
    except Exception:
        status('initial sync failed: ' + traceback.format_exc().replace('\n', ' | '))
    while True:
        jobs = sorted(f for f in os.listdir(JOBS) if f.endswith('.json'))
        if not jobs:
            with open(os.path.join(SBX, 'agent_alive.txt'), 'w') as f:
                f.write(time.strftime('%Y-%m-%d %H:%M:%S'))
            time.sleep(2)
            continue
        p = os.path.join(JOBS, jobs[0])
        try:
            runpy.run_path(RUNNER)['run_job'](p, status)
        except Exception:
            status('job failed: ' + traceback.format_exc().replace('\n', ' | '))
        try:
            os.replace(p, os.path.join(DONE, jobs[0]))
        except OSError:
            os.remove(p)


if __name__ == '__main__':
    main()
