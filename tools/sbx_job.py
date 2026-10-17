"""Submit a test job to the Windows Sandbox agent and wait for it (runs on the host).

Usage: python -I sbx_job.py <name> [--seconds N] [--fixes on|off] [--latency MS] ...
Results appear in <sbx>\\runs\\<name>; the sbx folder is env BCMP_SBX (default E:\\dev\\bc\\work\\sbx).
"""
import argparse
import json
import os
import shutil
import sys
import time

SBX = os.environ.get('BCMP_SBX', r'E:\dev\bc\work\sbx')

def sync_runtime_tools():
    """Refresh the read-only folder mapped into the already-running sandbox."""
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    runtime = os.path.join(os.path.dirname(SBX), 'tools')
    copies = [
        (os.path.join(repo, 'tools', 'sbx_agent.py'), os.path.join(runtime, 'sbx_agent.py')),
        (os.path.join(repo, 'tools', 'sbx_runner.py'), os.path.join(runtime, 'sbx_runner.py')),
        (os.path.join(repo, 'tools', 'mptest.py'), os.path.join(runtime, 'mptest.py')),
        (os.path.join(repo, 'tools', 'wincap.py'), os.path.join(runtime, 'wincap.py')),
        (os.path.join(repo, 'tools', 'game_scripts', 'Local.py'),
         os.path.join(runtime, 'game_scripts', 'Local.py')),
        (os.path.join(repo, 'tools', 'game_scripts', 'bcmptest.py'),
         os.path.join(runtime, 'game_scripts', 'bcmptest.py')),
        (os.path.join(repo, 'out', 'dinput.dll'),
         os.path.join(runtime, 'current', 'dinput.dll')),
        (os.path.join(repo, 'scripts', 'bcmpfix_mp.py'),
         os.path.join(runtime, 'current', 'bcmpfix_mp.py')),
    ]
    for src, dst in copies:
        if not os.path.isfile(src):
            raise SystemExit('required sandbox input missing: %s' % src)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)


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
    ap.add_argument('--dedicated', action='store_true')
    ap.add_argument('--max-players', type=int, default=0)
    ap.add_argument('--variant', action='append', default=[], help="ROLE=off|vX.Y.Z (see mptest.py)")
    ap.add_argument('--ai-ships', type=int, default=0)
    ap.add_argument('--target', default='nearest')
    ap.add_argument('--mission', type=int, default=1)
    ap.add_argument('--teams', default='')
    ap.add_argument('--range', default='')
    ap.add_argument('--switch', action='store_true')
    ap.add_argument('--ram', type=float, default=0.0)
    ap.add_argument('--ram-nofire', action='store_true')
    ap.add_argument('--nowait', action='store_true')
    ap.add_argument('--nodll', action='store_true')
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--set-host', action='append', default=[])
    ap.add_argument('--set-client', action='append', default=[])
    ap.add_argument('--test-set', action='append', default=[])
    ap.add_argument('--raise-once', action='store_true')
    ap.add_argument('--test-infopane', action='store_true')
    ap.add_argument('--attack', action='store_true')
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
    sync_runtime_tools()
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
