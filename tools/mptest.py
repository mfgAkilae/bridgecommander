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


NAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel']

FIXES_OFF = {'StateUpdateHz': 0, 'AlwaysAbsolutePosition': 0, 'FixDeltaBase': 0, 'FixProxyFirstFrame': 0,
             'NoCorrectionDeadband': 0, 'CorrectionGainPercent': 100, 'StateUpdateIntervalMs': 100,
             'TimerResolution1ms': 0, 'ClockFromProcessStart': 0, 'ResendIntervalMs': 1000, 'ScriptLayer': 0, 'TorpedoHitAuthority': 0,
             'LatencyCompensation': 0, 'HostRelayReceivedState': 0, 'NoScriptErrorConsole': 0, 'SmoothCorrection': 0, 'OrientationCorrection': 0, 'FixTorpedoFireFlags': 0,
             'MaxFPS': 0}
FIXES_ON = {'StateUpdateHz': 0, 'AlwaysAbsolutePosition': 1, 'FixDeltaBase': 1, 'FixProxyFirstFrame': 1,
            'NoCorrectionDeadband': 1, 'CorrectionGainPercent': 500, 'StateUpdateIntervalMs': 50,
            'TimerResolution1ms': 1, 'ClockFromProcessStart': 1, 'ResendIntervalMs': 300, 'ScriptLayer': 1, 'TorpedoHitAuthority': 1,
            'LatencyCompensation': 1, 'HostRelayReceivedState': 1, 'NoScriptErrorConsole': 1, 'SmoothCorrection': 1, 'OrientationCorrection': 1, 'FixTorpedoFireFlags': 1,
            'MaxFPS': 0}


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
             'PythonTick=Local.Tick', 'PythonTickMs=50'] + list(test.get('tset') or []) + [
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
    ap.add_argument('--species', default='1,2', help='comma separated, one per player (cycled)')
    ap.add_argument('--clients', type=int, default=1, help='number of client instances (1-7)')
    ap.add_argument('--solo', action='store_true')
    ap.add_argument('--set', action='append', default=[], help='override a [Fixes] key, e.g. CorrectionGainPercent=300')
    ap.add_argument('--set-host', action='append', default=[], help='like --set, host only')
    ap.add_argument('--set-client', action='append', default=[], help='like --set, client only')
    ap.add_argument('--test-set', action='append', default=[], help='extra [Test] line, e.g. PythonTickDelayMs=1500')
    ap.add_argument('--raise-once', action='store_true', help='clients raise once in a script event handler')
    ap.add_argument('--test-infopane', action='store_true', help='call RebuildInfoPane once with no info pane')
    ap.add_argument('--kick', default='', help='AT:SPEED[:EVERY] - every player kicks its own ship sideways/backwards (player i at AT+4*i s), e.g. 30:20:30')
    ap.add_argument('--damage', default='', help='AT:FRACTION[:hull] - host damages its impulse engines (or the hull of all other ships) once, e.g. 60:0.2')
    a = ap.parse_args()
    if not IN_SANDBOX and os.environ.get('BCMP_ALLOW_HOST') != '1':
        sys.exit('refusing to start the game outside Windows Sandbox (set BCMP_ALLOW_HOST=1 to override)')
    out = os.path.join(RUNS, a.name)
    os.makedirs(out, exist_ok=True)
    species = a.species.split(',')
    procs = []
    # (role, name, bind address, seed); each client gets its own loopback IP because
    # every instance binds UDP 22101; the first client keeps the 2-player names
    roles = [('host', NAMES[0], HOST_ADDR, 12345)]
    for k in range(1, a.clients + 1):
        roles.append(('client' if k == 1 else 'client%d' % k, NAMES[k],
                      '127.0.0.1' if k == 1 else '127.0.0.%d' % (k + 2), 54321 if k == 1 else 54321 + 7919 * k))
    if a.solo:
        roles = roles[:1]
    stagger = 3 if len(roles) > 2 else 0
    for i, (role, name, bind, seed) in enumerate(roles):
        sp = species[i % len(species)]
        if IN_SANDBOX:
            x, y = (i % 3) * 660, (i // 3) * 520
        else:
            x, y = 2900 + (i % 2) * 680, 650 + (i // 2) * 200
        ini = os.path.join(out, 'bcmpfix_%s.ini' % role)
        write_ini(ini, a.fixes, a.rate,
                  {'x': x, 'y': y, 'lat': a.latency, 'jit': a.jitter, 'loss': a.loss, 'bind': bind,
                   'set': a.set + (a.set_host if role == 'host' else a.set_client), 'tset': a.test_set})
        env = dict(os.environ)
        env.update({
            'BCMPFIX_INI': ini,
            'BCMPFIX_LOG': os.path.join(out, 'dll_%s.log' % role),
            'BCMP_ROLE': role, 'BCMP_NAME': name, 'BCMP_JOIN': HOST_ADDR,
            'BCMP_SPECIES': sp, 'BCMP_SYSTEM': '1', 'BCMP_AI': 'attack',
            'BCMP_TLOG': os.path.join(out, 'tlog_%s.txt' % role),
            'BCMPFIX_PYLOG': os.path.join(out, 'py_%s.log' % role),
            'BCMPFIX_TRACE': os.path.join(out, 'trace_%s.txt' % role),
            # the host stays until the last client has quit
            'BCMP_QUIT_AFTER': str(a.seconds + (12 + stagger * (len(roles) - 1) if role == 'host' else 8)),
            'BCMP_SEED': str(seed),
            'BCMP_RAISE_ONCE': '1' if a.raise_once and role != 'host' else '0',
            'BCMP_TEST_INFOPANE': '1' if a.test_infopane else '0',
            'BCMP_KICK_AT': a.kick.split(':')[0] if a.kick else '-1',
            'BCMP_KICK': a.kick.split(':')[1] if ':' in a.kick else '20',
            'BCMP_KICK_EVERY': a.kick.split(':')[2] if a.kick.count(':') >= 2 else '0',
            'BCMP_DAMAGE_AT': a.damage.split(':')[0] if a.damage else '-1',
            'BCMP_DAMAGE': a.damage.split(':')[1] if ':' in a.damage else '0.2',
            'BCMP_DAMAGE_KIND': a.damage.split(':')[2] if a.damage.count(':') >= 2 else 'impulse',
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
        time.sleep(6 if role == 'host' else stagger)
    deadline = time.time() + a.seconds + 30 + stagger * len(roles)
    while time.time() < deadline and any(p.poll() is None for p in procs):
        time.sleep(1)
    for i, p in enumerate(procs):
        if p.poll() is None:
            # still running after the scripted quit: hung - dump its threads first
            dump = os.path.join(out, 'hang_%s.txt' % roles[i][0])
            # window titles/texts and screenshots (e.g. the Python exception console)
            subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wincap.py'),
                            '--pid', str(p.pid), os.path.join(out, 'hangwin_%s' % roles[i][0])],
                           capture_output=True, text=True, timeout=60)
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
