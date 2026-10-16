"""Launch an automated 2-player multiplayer test (host + client) on this machine.

Usage: python -I mptest.py <name> [--seconds N] [--fixes on|off] [--latency MS] [--jitter MS]
                           [--loss PERMILLE] [--rate HZ]
Writes logs to E:/dev/bc/work/runs/<name>/.

--fixes off (and --variant ROLE=off) sets every [Fixes] key to its stock value, including
NoScriptErrorConsole=0: a windowed stock game freezes in the blocking Python debug console on any
script error, and the starbase mission (--mission 5) raises one on every client. Add
--set NoScriptErrorConsole=1 to log script errors and continue instead.
"""
import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import threading
import time

GAME = os.environ.get('BCMP_GAME', r'E:\dev\bc\game')
RUNS = os.environ.get('BCMP_RUNS', r'E:\dev\bc\work\runs')
IN_SANDBOX = os.environ.get('USERNAME', '').upper() == 'WDAGUTILITYACCOUNT'
# the client refuses to join its own resolved host IP (0x006B9460), which is
# 127.0.0.1 without a network adapter, so the host listens on another loopback IP
HOST_ADDR = '127.0.0.2'


NAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliett', 'Kilo', 'Lima',
         'Mike', 'November', 'Oscar', 'Papa']

# --fixes off: every [Fixes] key of the DLL (bcmpfix/src/util.c) at its stock value. A key missing
# here falls back to the DLL default, which is the fix turned on, so a new key must be added here.
# Stock numbers: CorrectionGainPercent=100, UpdateDistanceScalePercent=100, StateUpdateIntervalMs=100,
# ResendIntervalMs=1000; TrackingLeadOffsetMs and MaxLatencyCompensationMs only matter with their
# fixes on. NoScriptErrorConsole=0 is the stock blocking console (see the usage text).
FIXES_OFF = {'StateUpdateHz': 0, 'AlwaysAbsolutePosition': 0, 'FixDeltaBase': 0, 'FixProxyFirstFrame': 0,
             'NoCorrectionDeadband': 0, 'FixTorpedoFireFlags': 0, 'SymmetricTorpedoSpawn': 0,
             'CorrectionGainPercent': 100, 'SmoothCorrection': 0, 'OrientationCorrection': 0, 'ArcExtrapolation': 0,
             'VelocityExtrapolation': 0, 'KnockbackCorrection': 0, 'VelocityTracking': 0, 'TrackingLeadOffsetMs': 0,
             'TrackingArc': 0, 'UpdateDistanceScalePercent': 100, 'StateUpdateIntervalMs': 100, 'ResendIntervalMs': 1000,
             'TorpedoHitAuthority': 0, 'LatencyCompensation': 0, 'MaxLatencyCompensationMs': 0,
             'HostRelayReceivedState': 0, 'NoScriptErrorConsole': 0, 'FixGhostShip': 0, 'FixDedicatedObjectIds': 0,
             'FixConditionInRange': 0, 'FixRestartCrashes': 0, 'SnapNewShips': 0, 'TorpedoLatencyCompensation': 0,
             'PredictTorpedoHits': 0, 'PhaserHitAuthority': 0, 'PhaserSync': 0, 'DirectPhaserReports': 0,
             'ScriptLayer': 0, 'TimerResolution1ms': 0, 'ClockFromProcessStart': 0, 'MaxFPS': 0,
             'InputOnlyWhenFocused': 0}
# --fixes on: a key missing here keeps its DLL default, which is the release setting
# (release/bcmpfix.ini), so FIXES_ON does not need every key
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


# --variant ROLE=NAME: 'off' runs that instance as the original game (the DLL only serves the
# test harness: loopback binding, network emulation, the test pilot's tick; every fix and the
# script layer are off), 'vX.Y.Z' runs it from a copy of the game folder with that release's
# dinput.dll and scripts/bcmpfix_mp.py (from <game>/bcmpfix_variants/vX.Y.Z/)
VARIANT_FILES = (('dinput.dll', 'dinput.dll'), ('bcmpfix_mp.py', os.path.join('scripts', 'bcmpfix_mp.py')))


def variant_game(name):
    """<game>_<name>: hard links to every file of the game folder, except the mod's own files,
    which come from the variant (the .pyc of the mod script is left out, the game compiles it)."""
    src = os.path.join(GAME, 'bcmpfix_variants', name)
    if not os.path.isfile(os.path.join(src, 'dinput.dll')):
        sys.exit('variant %s: %s has no dinput.dll' % (name, src))
    dst = GAME.rstrip('\\/') + '_' + name
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    own = {os.path.normcase(p) for _, p in VARIANT_FILES} | {os.path.normcase(os.path.join('scripts', 'bcmpfix_mp.pyc'))}
    for root, dirs, files in os.walk(GAME):
        rel = os.path.relpath(root, GAME)
        if rel.split(os.sep)[0] == 'bcmpfix_variants':
            dirs[:] = []
            continue
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            r = os.path.normpath(os.path.join(rel, f))
            if os.path.normcase(r) in own:
                continue
            try:
                os.link(os.path.join(root, f), os.path.join(dst, r))
            except OSError:
                shutil.copy2(os.path.join(root, f), os.path.join(dst, r))
    for s, d in VARIANT_FILES:
        if os.path.isfile(os.path.join(src, s)):
            shutil.copy2(os.path.join(src, s), os.path.join(dst, d))
    return dst


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong), ('ullTotalPhys', ctypes.c_ulonglong),
                ('ullAvailPhys', ctypes.c_ulonglong), ('ullTotalPageFile', ctypes.c_ulonglong),
                ('ullAvailPageFile', ctypes.c_ulonglong), ('ullTotalVirtual', ctypes.c_ulonglong),
                ('ullAvailVirtual', ctypes.c_ulonglong), ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]


def log_memory(path, stop):
    """every 5 s: free physical memory and commit charge of the machine (memory.txt)"""
    with open(path, 'w') as f:
        while not stop.is_set():
            m = MEMORYSTATUSEX()
            m.dwLength = ctypes.sizeof(m)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
                f.write('%d load=%d%% free_mb=%d commit_free_mb=%d\n' % (
                    int(time.time() * 1000), m.dwMemoryLoad, m.ullAvailPhys // 2 ** 20, m.ullAvailPageFile // 2 ** 20))
                f.flush()
            stop.wait(5)


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
    ap.add_argument('--clients', type=int, default=1, help='number of client instances (1-15)')
    ap.add_argument('--dedicated', action='store_true', help='the host is a dedicated server (-Dedicated, no ship)')
    ap.add_argument('--max-players', type=int, default=0, help='the host raises the player limit (menu: 2-8)')
    ap.add_argument('--ai-ships', type=int, default=0, help='the host keeps this many AI ships attacking the players')
    ap.add_argument('--target', default='nearest', help="'ai': the pilots only attack the AI ships; "
                    "'team': only the other team (attackers go for the starbase)")
    ap.add_argument('--mission', type=int, default=1, help='1 = deathmatch, 5 = Defend the Starbase')
    ap.add_argument('--range', default='', help='range test: distances d1,d2,... (the host fires at a stationary '
                    'ship it moves to each distance for 15 s; the clients only watch)')
    ap.add_argument('--switch', action='store_true', help='target switch test: the host keeps firing while its '
                    'two target ships take turns behind it (the banks switch targets without stopping)')
    ap.add_argument('--teams', default='', help='team per instance in role order (host, client, client2, ...), '
                    'e.g. 1,0,1,0 (Mission 5: 0 = attackers, 1 = defenders)')
    ap.add_argument('--variant', action='append', default=[],
                    help="ROLE=NAME: 'off' (original game) or a release vX.Y.Z for one instance, e.g. client2=v1.6.0")
    ap.add_argument('--solo', action='store_true')
    ap.add_argument('--set', action='append', default=[], help='override a [Fixes] key, e.g. CorrectionGainPercent=300')
    ap.add_argument('--set-host', action='append', default=[], help='like --set, host only')
    ap.add_argument('--set-client', action='append', default=[], help='like --set, client only')
    ap.add_argument('--test-set', action='append', default=[], help='extra [Test] line, e.g. PythonTickDelayMs=1500')
    ap.add_argument('--raise-once', action='store_true', help='clients raise once in a script event handler')
    ap.add_argument('--test-infopane', action='store_true', help='call RebuildInfoPane once with no info pane')
    ap.add_argument('--kick', default='', help='AT:SPEED[:EVERY] - every player kicks its own ship sideways/backwards (player i at AT+4*i s), e.g. 30:20:30')
    ap.add_argument('--damage', default='', help='AT:FRACTION[:hull[:EVERY]] - host damages its impulse engines (or the hull of all other ships) once '
                         'or every EVERY s, e.g. 60:0.2 or 40:0.05:hull:30')
    a = ap.parse_args()
    if not IN_SANDBOX and os.environ.get('BCMP_ALLOW_HOST') != '1':
        sys.exit('refusing to start the game outside Windows Sandbox (set BCMP_ALLOW_HOST=1 to override)')
    out = os.path.join(RUNS, a.name)
    os.makedirs(out, exist_ok=True)
    species = a.species.split(',')
    procs = []
    stop_mem = threading.Event()
    threading.Thread(target=log_memory, args=(os.path.join(out, 'memory.txt'), stop_mem), daemon=True).start()
    # (role, name, bind address, seed); each client gets its own loopback IP because
    # every instance binds UDP 22101; the first client keeps the 2-player names
    roles = [('host', NAMES[0], HOST_ADDR, 12345)]
    for k in range(1, a.clients + 1):
        roles.append(('client' if k == 1 else 'client%d' % k, NAMES[k],
                      '127.0.0.1' if k == 1 else '127.0.0.%d' % (k + 2), 54321 if k == 1 else 54321 + 7919 * k))
    if a.solo:
        roles = roles[:1]
    stagger = 3 if len(roles) > 2 else 0
    many = len(roles) > 6
    variants = dict(kv.split('=', 1) for kv in a.variant)
    unknown = set(variants) - {r[0] for r in roles}
    if unknown:
        sys.exit('--variant for unknown roles: %s' % ', '.join(sorted(unknown)))
    games = {}
    for name in sorted(set(variants.values()) - {'off'}):
        t0 = time.time()
        games[name] = variant_game(name)
        print('variant', name, 'game folder', games[name], '%.1f s' % (time.time() - t0), flush=True)
    json.dump({r[0]: variants.get(r[0], 'off' if a.fixes == 'off' else 'current') for r in roles},
              open(os.path.join(out, 'variants.json'), 'w'), indent=1)
    for i, (role, name, bind, seed) in enumerate(roles):
        sp = species[i % len(species)]
        if IN_SANDBOX and many:
            x, y = (i % 4) * 420, (i // 4) * 300
        elif IN_SANDBOX:
            x, y = (i % 3) * 660, (i // 3) * 520
        else:
            x, y = 2900 + (i % 2) * 680, 650 + (i // 2) * 200
        ini = os.path.join(out, 'bcmpfix_%s.ini' % role)
        var = variants.get(role, '')
        game = games.get(var, GAME)
        write_ini(ini, 'off' if var == 'off' else a.fixes, a.rate,
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
            'BCMP_DAMAGE_EVERY': a.damage.split(':')[3] if a.damage.count(':') >= 3 else '0',
            'BCMP_DEDICATED': '1' if a.dedicated and role == 'host' else '0',
            'BCMP_MAX_PLAYERS': str(a.max_players) if role == 'host' else '0',
            'BCMP_AI_SHIPS': str(a.ai_ships),
            'BCMP_TARGET': a.target,
            'BCMP_MISSION': str(a.mission),
            'BCMP_RANGE': a.range,
            'BCMP_SWITCH': '1' if a.switch else '0',
            'BCMP_TEAM': (a.teams.split(',') + ['0'] * 32)[i] if a.teams else '0',
        })
        for f in ('tlog_%s.txt' % role,):
            p = os.path.join(out, f)
            if os.path.exists(p):
                os.remove(p)
        # built-in developer options parsed by the app start routine (0x004388A0)
        if role == 'host':
            gargs = ['-Dedicated' if a.dedicated else '-Listener', '-Mission', 'Mission%d' % a.mission]
        else:
            gargs = ['-Client', '-IP', HOST_ADDR, '-Name', name]
        env['BCMP_CMDLINE'] = '1'
        procs.append(subprocess.Popen([os.path.join(game, 'stbc.exe'), '-ForceWindowed'] + gargs, cwd=game, env=env))
        print('started', role, procs[-1].pid, var or a.fixes, flush=True)
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
    stop_mem.set()
    print('done', [p.returncode for p in procs])


if __name__ == '__main__':
    main()
