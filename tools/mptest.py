"""Launch an automated 2-player multiplayer test (host + client) on this machine.

Usage: python -I mptest.py <name> [--seconds N] [--fixes on|off] [--latency MS] [--jitter MS]
                           [--loss PERMILLE] [--rate HZ]
Writes logs to E:/dev/bc/work/runs/<name>/.
"""
import argparse
import os
import subprocess
import sys
import time

GAME = os.environ.get('BCMP_GAME', r'E:\dev\bc\game')
RUNS = os.environ.get('BCMP_RUNS', r'E:\dev\bc\work\runs')
IN_SANDBOX = os.environ.get('USERNAME', '').upper() == 'WDAGUTILITYACCOUNT'
# the client refuses to join its own resolved host IP (0x006B9460), which is
# 127.0.0.1 without a network adapter, so the host listens on another loopback IP
HOST_ADDR = '127.0.0.2'


FIXES_OFF = {'StateUpdateHz': 0, 'AlwaysAbsolutePosition': 0, 'FixDeltaBase': 0, 'FixProxyFirstFrame': 0,
             'NoCorrectionDeadband': 0, 'CorrectionGainPercent': 100, 'StateUpdateIntervalMs': 100,
             'TimerResolution1ms': 0, 'ClockFromProcessStart': 0, 'ResendIntervalMs': 1000, 'ScriptLayer': 0, 'TorpedoHitAuthority': 0,
             'LatencyCompensation': 0, 'MaxFPS': 0}
FIXES_ON = {'StateUpdateHz': 0, 'AlwaysAbsolutePosition': 1, 'FixDeltaBase': 1, 'FixProxyFirstFrame': 1,
            'NoCorrectionDeadband': 1, 'CorrectionGainPercent': 500, 'StateUpdateIntervalMs': 50,
            'TimerResolution1ms': 1, 'ClockFromProcessStart': 1, 'ResendIntervalMs': 300, 'ScriptLayer': 1, 'TorpedoHitAuthority': 1,
            'LatencyCompensation': 1, 'MaxFPS': 0}


def write_ini(path, fixes, rate, test):
    fx = dict(FIXES_ON if fixes == 'on' else FIXES_OFF)
    for kv in (test.get('set') or []):
        k, v = kv.split('=', 1)
        fx[k] = int(v)
    lines = ['[Fixes]'] + ['%s=%d' % (k, v) for k, v in fx.items()] + [
             '', '[Test]', 'Enabled=1', 'InputIsolation=%d' % (0 if IN_SANDBOX else 1), 'NoInput=1',
             'BindAddress=%s' % test['bind'], 'ClientBindPort=22101',
             'WindowX=%d' % test['x'], 'WindowY=%d' % test['y'],
             'LatencyMs=%d' % test['lat'], 'JitterMs=%d' % test['jit'],
             'LossPerMille=%d' % test['loss'],
             'PythonTick=Local.Tick', 'PythonTickMs=50',
             '', '[Debug]', 'NetStatsSec=5', 'Verbose=1', '']
    open(path, 'w').write('\n'.join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('--seconds', type=int, default=120)
    ap.add_argument('--fixes', default='off')
    ap.add_argument('--rate', type=int, default=30)
    ap.add_argument('--latency', type=int, default=0)
    ap.add_argument('--jitter', type=int, default=0)
    ap.add_argument('--loss', type=int, default=0)
    ap.add_argument('--species', default='1,2')
    ap.add_argument('--solo', action='store_true')
    ap.add_argument('--set', action='append', default=[], help='override a [Fixes] key, e.g. CorrectionGainPercent=300')
    ap.add_argument('--set-host', action='append', default=[], help='like --set, host only')
    ap.add_argument('--set-client', action='append', default=[], help='like --set, client only')
    a = ap.parse_args()
    if not IN_SANDBOX and os.environ.get('BCMP_ALLOW_HOST') != '1':
        sys.exit('refusing to start the game outside Windows Sandbox (set BCMP_ALLOW_HOST=1 to override)')
    out = os.path.join(RUNS, a.name)
    os.makedirs(out, exist_ok=True)
    species = a.species.split(',')
    procs = []
    if IN_SANDBOX:
        roles = [('host', 'Alpha', 0, 0, species[0]), ('client', 'Bravo', 660, 0, species[1])]
    else:
        roles = [('host', 'Alpha', 2900, 650, species[0]), ('client', 'Bravo', 3580, 650, species[1])]
    if a.solo:
        roles = roles[:1]
    for role, name, x, y, sp in roles:
        ini = os.path.join(out, 'bcmpfix_%s.ini' % role)
        write_ini(ini, a.fixes, a.rate,
                  {'x': x, 'y': y, 'lat': a.latency, 'jit': a.jitter, 'loss': a.loss,
                   'bind': HOST_ADDR if role == 'host' else '127.0.0.1',
                   'set': a.set + (a.set_host if role == 'host' else a.set_client)})
        env = dict(os.environ)
        env.update({
            'BCMPFIX_INI': ini,
            'BCMPFIX_LOG': os.path.join(out, 'dll_%s.log' % role),
            'BCMP_ROLE': role, 'BCMP_NAME': name, 'BCMP_JOIN': HOST_ADDR,
            'BCMP_SPECIES': sp, 'BCMP_SYSTEM': '1', 'BCMP_AI': 'attack',
            'BCMP_TLOG': os.path.join(out, 'tlog_%s.txt' % role),
            'BCMPFIX_PYLOG': os.path.join(out, 'py_%s.log' % role),
            'BCMP_QUIT_AFTER': str(a.seconds + (8 if role == 'client' else 12)),
        })
        for f in ('tlog_%s.txt' % role,):
            p = os.path.join(out, f)
            if os.path.exists(p):
                os.remove(p)
        # built-in developer options parsed by the app start routine (0x004388A0)
        if role == 'host':
            gargs = ['-Listener', '-Mission', 'Mission1']
        else:
            gargs = ['-Client', '-IP', HOST_ADDR, '-Name', name]
        env['BCMP_CMDLINE'] = '1'
        procs.append(subprocess.Popen([os.path.join(GAME, 'stbc.exe'), '-ForceWindowed'] + gargs, cwd=GAME, env=env))
        print('started', role, procs[-1].pid, flush=True)
        time.sleep(6 if role == 'host' else 0)
    deadline = time.time() + a.seconds + 30
    while time.time() < deadline and any(p.poll() is None for p in procs):
        time.sleep(1)
    for i, p in enumerate(procs):
        if p.poll() is None:
            # still running after the scripted quit: hung - dump its threads first
            dump = os.path.join(out, 'hang_%s.txt' % roles[i][0])
            with open(dump, 'w') as f:
                for _ in range(3):
                    r = subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        'stackdump.py'), str(p.pid)], capture_output=True, text=True)
                    f.write(r.stdout + r.stderr + '\n----\n')
                    time.sleep(0.5)
            p.kill()
    print('done', [p.returncode for p in procs])


if __name__ == '__main__':
    main()
