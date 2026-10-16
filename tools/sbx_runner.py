"""Job logic for the sandbox agent (re-loaded for every job, runs INSIDE the sandbox).

Copies the game from the read-only mapped folder to local disk, applies the
sandbox overlay (dgVoodoo2), runs mptest.py and takes optional screenshots.
"""
import json
import os
import shutil
import subprocess
import sys
import time

SRC = r'C:\bcsrc\game'
GAME = r'C:\bc\game'
SBX = r'C:\sbx'
RUNS = os.path.join(SBX, 'runs')
TOOLS = r'C:\bcsrc\tools'


def sync_game(status):
    """Copy new or changed files from the mapped game folder, then the overlay."""
    n = 0
    for root, dirs, files in os.walk(SRC):
        rel = os.path.relpath(root, SRC)
        dst_dir = os.path.join(GAME, rel)
        os.makedirs(dst_dir, exist_ok=True)
        for f in files:
            s = os.path.join(root, f)
            d = os.path.join(dst_dir, f)
            try:
                st = os.stat(s)
                if os.path.exists(d):
                    dt = os.stat(d)
                    if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                        continue
                shutil.copy2(s, d)
                n += 1
            except OSError as e:
                status('copy failed %s: %s' % (s, e))
    overlay = os.path.join(SBX, 'overlay')
    if os.path.isdir(overlay):
        for f in os.listdir(overlay):
            s = os.path.join(overlay, f)
            d = os.path.join(GAME, f)
            if os.path.isfile(s) and (not os.path.exists(d) or int(os.path.getmtime(d)) != int(os.path.getmtime(s))):
                shutil.copy2(s, d)
                n += 1
    for root, dirs, files in os.walk(os.path.join(GAME, 'scripts')):
        for f in files:
            if f.endswith('.pyc') and os.path.exists(os.path.join(root, f[:-1])):
                pyc = os.path.join(root, f)
                try:
                    if os.path.getmtime(pyc) < os.path.getmtime(pyc[:-1]):
                        os.remove(pyc)
                except OSError:
                    pass
    return n


def initial_sync(status):
    status('initial sync')
    status('initial copy %d files' % sync_game(status))


def run_steps(job, status):
    """Run other programs (e.g. the installer) step by step with screenshots; no game run.

    job['steps']: [{'pre': [shell commands], 'cmd': [argv], 'cwd': dir, 'seconds': N,
                    'title': window title substring, 'shots': [seconds]}]
    """
    out = os.path.join(RUNS, job['name'])
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'steps_output.txt'), 'w') as log:
        for i, st in enumerate(job['steps']):
            for c in st.get('pre', []):
                try:
                    r = subprocess.run(c, shell=True, capture_output=True, text=True, timeout=st.get('pre_timeout', 120))
                except subprocess.TimeoutExpired:
                    r = subprocess.CompletedProcess(c, -9, '', 'timeout')
                log.write('step %d pre %s -> rc=%s %s %s\n' % (i, c, r.returncode, r.stdout.strip(), r.stderr.strip()))
            p = subprocess.Popen(st['cmd'], cwd=st.get('cwd'))
            t0 = time.time()
            shots = sorted(st.get('shots', []))
            while p.poll() is None and time.time() - t0 < st.get('seconds', 10):
                if shots and time.time() - t0 >= shots[0]:
                    sec = shots.pop(0)
                    subprocess.run([sys.executable, '-I', os.path.join(TOOLS, 'wincap.py'), st.get('title', ''),
                                    os.path.join(out, 'step%d_%02d.png' % (i, sec))], capture_output=True, timeout=20)
                time.sleep(0.2)
            if p.poll() is None:
                p.kill()
                p.wait()
            log.write('step %d rc=%s cmd=%s\n' % (i, p.returncode, st['cmd']))
            log.flush()
    status('job %s done rc=steps' % job['name'])


def run_job(path, status):
    job = json.load(open(path))
    name = job['name']
    status('job %s start %s' % (name, job))
    if job.get('steps'):
        return run_steps(job, status)
    status('synced %d files' % sync_game(status))
    args = [sys.executable, '-I', os.path.join(TOOLS, 'mptest.py'), name]
    for k in ('seconds', 'fixes', 'rate', 'latency', 'jitter', 'loss', 'species', 'clients'):
        if k in job:
            args += ['--' + k, str(job[k])]
    if job.get('solo'):
        args.append('--solo')
    if job.get('dedicated'):
        args.append('--dedicated')
    if job.get('max_players'):
        args += ['--max-players', str(job['max_players'])]
    for kv in job.get('variant', []):
        args += ['--variant', kv]
    if job.get('raise_once'):
        args.append('--raise-once')
    if job.get('test_infopane'):
        args.append('--test-infopane')
    if job.get('damage'):
        args += ['--damage', job['damage']]
    if job.get('kick'):
        args += ['--kick', job['kick']]
    for opt in ('set', 'set_host', 'set_client', 'test_set'):
        for kv in job.get(opt, []):
            args += ['--' + opt.replace('_', '-'), kv]
    env = dict(os.environ)
    env['BCMP_GAME'] = GAME
    env['BCMP_RUNS'] = RUNS
    out = os.path.join(RUNS, name)
    os.makedirs(out, exist_ok=True)
    dll = os.path.join(GAME, 'dinput.dll')
    if job.get('nodll') and os.path.exists(dll):
        os.replace(dll, dll + '.off')
    rc = None
    try:
        with open(os.path.join(out, 'mptest_output.txt'), 'w') as log:
            p = subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT)
            t0 = time.time()
            shots = sorted(job.get('shots', []))
            while p.poll() is None and time.time() - t0 < job.get('seconds', 120) + 180:
                if shots and time.time() - t0 >= shots[0]:
                    sec = shots.pop(0)
                    for idx in (0, 1):
                        subprocess.run([sys.executable, '-I', os.path.join(TOOLS, 'wincap.py'), 'Bridge Commander',
                                        os.path.join(out, 'shot_%03d_%d.png' % (sec, idx)), str(idx)],
                                       capture_output=True, timeout=20)
                time.sleep(0.5)
            if p.poll() is None:
                p.kill()
            rc = p.returncode
    finally:
        if job.get('nodll') and os.path.exists(dll + '.off'):
            os.replace(dll + '.off', dll)
    status('job %s done rc=%s' % (name, rc))
