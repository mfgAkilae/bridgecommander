"""Submit a test job to the Windows Sandbox agent and wait for it (runs on the host).

Usage: python -I sbx_job.py <name> [--seconds N] [--fixes on|off] [--latency MS] ...
Results appear in <sbx>\\runs\\<name>; the sbx folder is env BCMP_SBX (default E:\\dev\\bc\\work\\sbx).
"""
import argparse
import json
import os
import sys
import time

SBX = os.environ.get('BCMP_SBX', r'E:\dev\bc\work\sbx')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('--seconds', type=int, default=90)
    ap.add_argument('--fixes', default='off')
    ap.add_argument('--rate', type=int, default=0)
    ap.add_argument('--latency', type=int, default=0)
    ap.add_argument('--jitter', type=int, default=0)
    ap.add_argument('--loss', type=int, default=0)
    ap.add_argument('--species', default='1,2')
    ap.add_argument('--clients', type=int, default=1)
    ap.add_argument('--solo', action='store_true')
    ap.add_argument('--nowait', action='store_true')
    ap.add_argument('--nodll', action='store_true')
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--set-host', action='append', default=[])
    ap.add_argument('--set-client', action='append', default=[])
    ap.add_argument('--test-set', action='append', default=[])
    ap.add_argument('--raise-once', action='store_true')
    ap.add_argument('--test-infopane', action='store_true')
    ap.add_argument('--damage', default='')
    ap.add_argument('--kick', default='')
    ap.add_argument('--shots', default='', help='comma separated seconds for screenshots')
    ap.add_argument('--steps', default='', help='JSON file with program steps instead of a game run (see sbx_runner)')
    a = ap.parse_args()
    # the agent touches agent_alive.txt only while idle; during a job agent_status.txt changes
    def age(f):
        p = os.path.join(SBX, f)
        return time.time() - os.path.getmtime(p) if os.path.exists(p) else 1e9
    if age('agent_alive.txt') > 30 and age('agent_status.txt') > 600:
        sys.exit('sandbox agent not running (start bc-test.wsb)')
    job = {k: v for k, v in vars(a).items() if k not in ('nowait', 'shots', 'steps')}
    job['shots'] = [int(x) for x in a.shots.split(',') if x]
    if a.steps:
        job['steps'] = json.load(open(a.steps))
    done = os.path.join(SBX, 'done', a.name + '.json')
    if os.path.exists(done):
        os.remove(done)  # an older run of the same name must not count as finished
    tmp = os.path.join(SBX, 'jobs', a.name + '.tmp')
    json.dump(job, open(tmp, 'w'))
    os.replace(tmp, os.path.join(SBX, 'jobs', a.name + '.json'))
    print('submitted', a.name, flush=True)
    if a.nowait:
        return
    deadline = time.time() + a.seconds + 300
    while time.time() < deadline:
        if os.path.exists(done):
            print('finished', a.name)
            return
        time.sleep(2)
    sys.exit('timeout waiting for job')


if __name__ == '__main__':
    main()
