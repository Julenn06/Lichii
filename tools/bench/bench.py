"""Banco de pruebas reproducible de Lichii.

Lanza el emulador del banco aislado (E:/lbench/app), lleva el juego hasta un punto fijo con el
inyector de entrada del servidor de depuracion (sin teclado ni capturas de pantalla), mide FPS y
tiempos de frame en una ventana fija y lo cierra. Los resultados van a E:/lbench/runs/<nombre>/.

    python bench.py --name base --exe E:/lbuild/bin --env EDEN_NX2_X=1 --seconds 30 --sample
"""
import argparse
import json
import os
import shutil
import socket
import statistics
import subprocess
import sys
import time
import ctypes


def _filetime(value):
    return ((value.hi << 32) | value.lo) / 1e7


class _FileTime(ctypes.Structure):
    _fields_ = [('lo', ctypes.c_uint32), ('hi', ctypes.c_uint32)]


def cpu_times():
    # (inactivo, total) del sistema, en segundos de CPU sumados entre todos los nucleos.
    idle, kernel, user = _FileTime(), _FileTime(), _FileTime()
    ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel),
                                          ctypes.byref(user))
    return _filetime(idle), _filetime(kernel) + _filetime(user)


def process_cpu(pid):
    # Segundos de CPU consumidos por un proceso (0 si ya no existe).
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
    if not handle:
        return 0.0
    times = [_FileTime() for _ in range(4)]
    ok = ctypes.windll.kernel32.GetProcessTimes(handle, *[ctypes.byref(t) for t in times])
    ctypes.windll.kernel32.CloseHandle(handle)
    return _filetime(times[2]) + _filetime(times[3]) if ok else 0.0


def system_busy(seconds=3.0):
    # Porcentaje de CPU ocupada en todo el sistema durante un intervalo.
    idle0, total0 = cpu_times()
    time.sleep(seconds)
    idle1, total1 = cpu_times()
    return round(100.0 * (1.0 - (idle1 - idle0) / max(total1 - total0, 1e-9)), 1)

APP = 'E:/lbench/app'
RUNS = 'E:/lbench/runs'
GAME = ('E:/Emuladores/Nintendo Switch 2/Juegos/'
        'The Legend of Zelda Ocarina of Time [0400486024dec000][v131072].nup')
PORT = 6545


class Debug:
    """Cliente JSON por lineas del servidor de depuracion (EDEN_NX2_DEBUG_SERVER)."""

    def __init__(self, port):
        self.port = port
        self.ident = 0

    def call(self, method, params=None, timeout=120.0):
        self.ident += 1
        with socket.create_connection(('127.0.0.1', self.port), timeout=5.0) as sock:
            sock.settimeout(timeout)
            request = {'id': self.ident, 'method': method, 'params': params or {}}
            sock.sendall((json.dumps(request) + '\n').encode())
            data = b''
            while not data.endswith(b'\n'):
                chunk = sock.recv(1 << 20)
                if not chunk:
                    break
                data += chunk
        reply = json.loads(data.decode('utf-8', errors='replace'))
        if 'error' in reply:
            raise RuntimeError(f'{method}: {reply["error"]}')
        return reply['result']

    def status(self):
        return self.call('status', timeout=10.0)

    def press(self, *buttons, frames=5, pause=30):
        self.call('input_sequence', {'steps': [{'buttons': list(buttons), 'frames': frames},
                                               {'frames': pause}]})

    def stick(self, x, y, frames):
        self.call('input', {'left_stick': [x, y], 'frames': frames})

    def shot(self, path):
        import base64
        result = self.call('screenshot', timeout=30.0)
        open(path, 'wb').write(base64.b64decode(result['png_base64']))
        try:
            from PIL import Image
            image = Image.open(path).convert('RGB')
            image.thumbnail((480, 270))
            small = os.path.splitext(path)[0] + '.jpg'
            image.save(small, quality=45)
            os.remove(path)
            return small
        except Exception as error:  # pragma: no cover - solo informativo
            return f'{path} ({error})'


def wait_for(debug, proc, predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f'el emulador termino esperando {what}')
        try:
            status = debug.status()
            if predicate(status):
                return status
        except (OSError, RuntimeError, ValueError):
            pass
        time.sleep(1.0)
    raise RuntimeError(f'tiempo agotado esperando {what}')


def measure(debug, seconds, step=0.5):
    """FPS por ventanas de `step` segundos a partir del contador de frames presentados."""
    points = []
    start = time.perf_counter()
    while True:
        now = time.perf_counter()
        points.append((now, debug.status()['presents']))
        if now - start >= seconds:
            break
        time.sleep(max(0.0, step - (time.perf_counter() - now)))
    total = (points[-1][1] - points[0][1]) / (points[-1][0] - points[0][0])
    windows = [(b[1] - a[1]) / (b[0] - a[0]) for a, b in zip(points, points[1:])]
    return {'fps_avg': round(total, 2), 'fps_min_window': round(min(windows), 2),
            'fps_max_window': round(max(windows), 2),
            'fps_stdev': round(statistics.pstdev(windows), 2), 'windows': len(windows)}


def apply_affinity(pid, spec):
    """Experimento: fija la afinidad de hilos por nombre. spec = 'GPU=0,1;GPUBack=2,3;*=4-11'
    (procesadores logicos; '*' es el resto de hilos). Devuelve cuantos hilos toco por regla."""
    import ctypes
    from ctypes import wintypes

    def mask_of(text):
        mask = 0
        for part in text.split(','):
            low, _, high = part.partition('-')
            for cpu in range(int(low), int(high or low) + 1):
                mask |= 1 << cpu
        return mask

    rules = {}
    for rule in spec.split(';'):
        name, _, cpus = rule.partition('=')
        rules[name.strip()] = mask_of(cpus)

    class ThreadEntry(ctypes.Structure):
        _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                    ('th32ThreadID', wintypes.DWORD), ('th32OwnerProcessID', wintypes.DWORD),
                    ('tpBasePri', wintypes.LONG), ('tpDeltaPri', wintypes.LONG),
                    ('dwFlags', wintypes.DWORD)]

    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernelbase = ctypes.WinDLL('KernelBase', use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.OpenThread.restype = wintypes.HANDLE
    kernel32.SetThreadAffinityMask.restype = ctypes.c_size_t
    kernel32.SetThreadAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
    kernelbase.GetThreadDescription.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.LPWSTR)]
    snapshot = kernel32.CreateToolhelp32Snapshot(0x4, 0)
    entry = ThreadEntry()
    entry.dwSize = ctypes.sizeof(ThreadEntry)
    touched = {}
    more = kernel32.Thread32First(wintypes.HANDLE(snapshot), ctypes.byref(entry))
    while more:
        if entry.th32OwnerProcessID == pid:
            # THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION
            handle = kernel32.OpenThread(0x0020 | 0x0040 | 0x0800, False, entry.th32ThreadID)
            if handle:
                text = wintypes.LPWSTR()
                name = ''
                if kernelbase.GetThreadDescription(wintypes.HANDLE(handle),
                                                   ctypes.byref(text)) >= 0:
                    name = text.value or ''
                    kernel32.LocalFree(text)
                key = name if name in rules else '*'
                if key in rules and kernel32.SetThreadAffinityMask(wintypes.HANDLE(handle),
                                                                   rules[key]):
                    touched[key] = touched.get(key, 0) + 1
                kernel32.CloseHandle(wintypes.HANDLE(handle))
        more = kernel32.Thread32Next(wintypes.HANDLE(snapshot), ctypes.byref(entry))
    kernel32.CloseHandle(wintypes.HANDLE(snapshot))
    return touched


def host_pacing(log_path, last=3):
    """Frames que llegan a pantalla (incluye los generados), de los informes del pacer cada 10 s."""
    import re
    pattern = re.compile(r'Host pacing \w+: (\d+) frames at .*?interval mean ([0-9.]+) ms, '
                         r'max ([0-9.]+)')
    rows = []
    try:
        for line in open(log_path, encoding='utf-8', errors='replace'):
            match = pattern.search(line)
            if match:
                rows.append({'host_fps': int(match.group(1)) / 10.0,
                             'interval_mean_ms': float(match.group(2)),
                             'interval_max_ms': float(match.group(3))})
    except OSError:
        pass
    return rows[-last:]


def scenario_kokiri(debug, proc, run_dir, args):
    """Titulo -> Continuar -> balcon de la casa de Link (Bosque Kokiri), partida del banco."""
    wait_for(debug, proc, lambda s: s['presents'] > 200 and s['fps'] > 20, 300, 'el titulo')
    time.sleep(args.title_wait)
    if args.title_steps == 'auto':
        # Continuar -> primera partida -> Cargar: se pulsa A hasta que el ritmo de frames cae
        # (pantalla de carga), asi no depende de tiempos fijos.
        loading = False
        for _ in range(12):
            debug.call('input', {'buttons': ['A'], 'frames': 5})
            slow = 0
            last = (time.perf_counter(), debug.status()['presents'])
            for _ in range(8):
                time.sleep(0.5)
                now = (time.perf_counter(), debug.status()['presents'])
                rate = (now[1] - last[1]) / (now[0] - last[0])
                last = now
                slow = slow + 1 if rate < 12 else 0
                if slow >= 2:
                    loading = True
                    break
            if loading:
                break
        if not loading:
            raise RuntimeError('no se detecto la carga de la partida')
    else:
        for step in args.title_steps.split(','):
            step = step.strip()
            if not step:
                continue
            if step.startswith('wait'):
                time.sleep(float(step[4:] or 1))
            else:
                debug.press(step, frames=5, pause=25)
    time.sleep(args.load_wait)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('--exe', help='carpeta con lichii.exe y lichii.pdb a probar')
    parser.add_argument('--env', action='append', default=[])
    parser.add_argument('--seconds', type=float, default=30.0)
    parser.add_argument('--warmup', type=float, default=8.0)
    parser.add_argument('--repeat', type=int, default=1, help='mediciones seguidas en la misma sesion')
    parser.add_argument('--sample', action='store_true', help='perfil por muestreo tras medir')
    parser.add_argument('--sample-threads', default='CPUCore_*,GPU,GPUBack,VulkanWorker')
    parser.add_argument('--timeline', type=float, default=0.0)
    parser.add_argument('--train', type=float, default=0.0,
                        help='segundos de recorrido de entrenamiento PGO (build instrumentada)')
    parser.add_argument('--title-wait', type=float, default=6.0)
    parser.add_argument('--title-steps', default='auto')
    parser.add_argument('--load-wait', type=float, default=30.0)
    parser.add_argument('--explore', action='store_true', help='deja el emulador abierto y sale')
    parser.add_argument('--shots', action='store_true')
    parser.add_argument('--port', type=int, default=PORT)
    parser.add_argument('--app', help='carpeta de aplicacion alternativa (con su propio user/)')
    parser.add_argument('--affinity', default='',
                        help="experimento de afinidad por nombre de hilo: 'GPU=0,1;GPUBack=2,3;*=4-11'")
    parser.add_argument('--config', action='append', default=[],
                        help='clave=valor de qt-config.ini solo para esta ejecucion')
    parser.add_argument('--max-busy', type=float, default=15.0,
                        help='CPU ocupada por otros programas (porcentaje) que invalida la medida')
    parser.add_argument('--allow-busy', action='store_true',
                        help='mide aunque el PC este ocupado (el resultado queda marcado)')
    args = parser.parse_args()
    global APP
    if args.app:
        APP = args.app.replace(chr(92), '/')

    busy_before = system_busy()
    if busy_before > args.max_busy and not args.allow_busy:
        print(f'PC ocupado: {busy_before} % de CPU en uso antes de empezar; no se mide '
              '(--allow-busy para medir igualmente)')
        return 3
    run_dir = f'{RUNS}/{args.name}'
    os.makedirs(run_dir, exist_ok=True)
    if args.exe:
        for attempt in range(15):
            try:
                shutil.copyfile(f'{args.exe}/lichii.exe', f'{APP}/lichii.exe')
                break
            except PermissionError:
                time.sleep(2)
        else:
            raise RuntimeError('lichii.exe del banco sigue en uso')
        if os.path.exists(f'{args.exe}/lichii.pdb'):
            shutil.copyfile(f'{args.exe}/lichii.pdb', f'{APP}/lichii.pdb')
        elif os.path.exists(f'{APP}/lichii.pdb'):
            os.remove(f'{APP}/lichii.pdb')
    for name in os.listdir(f'{APP}/user/log'):
        path = f'{APP}/user/log/{name}'
        if os.path.isfile(path):
            os.remove(path)
    # Cada ejecucion parte de la misma partida: los autoguardados de una no pasan a la siguiente.
    pristine = 'E:/lbench/save-pristine'
    save_dir = f'{APP}/user/nand/user/save'
    if os.path.isdir(pristine) and APP.lower().startswith('e:/lbench'):
        shutil.rmtree(save_dir, ignore_errors=True)
        shutil.copytree(pristine, save_dir)

    config_path = f'{APP}/user/config/qt-config.ini'
    config_backup = open(config_path, 'rb').read()
    if args.config:
        want = dict(pair.split('=', 1) for pair in args.config)
        suffix = chr(92) + 'default'
        patched = []
        newline = chr(10)
        for line in config_backup.decode('utf-8', errors='surrogateescape').split(newline):
            key, sep, _ = line.partition('=')
            if sep and key.endswith(suffix) and key[:-len(suffix)] in want:
                line = f'{key}=false'
            elif sep and key in want:
                line = f'{key}={want.pop(key)}'
            patched.append(line)
        if want:
            raise RuntimeError(f'claves de configuracion no encontradas: {sorted(want)}')
        open(config_path, 'wb').write(
            newline.join(patched).encode('utf-8', errors='surrogateescape'))

    env = dict(os.environ)
    env['EDEN_NX2_DEBUG_SERVER'] = f'127.0.0.1:{args.port}'
    for pair in args.env:
        key, _, value = pair.partition('=')
        env[key] = value
    started = time.monotonic()
    proc = subprocess.Popen([f'{APP}/lichii.exe', '-g', GAME], cwd=APP, env=env)
    open(f'{RUNS}/manual.pid', 'w').write(str(proc.pid))
    debug = Debug(args.port)
    result = {'name': args.name, 'env': args.env, 'exe': args.exe or 'sin cambiar'}
    result['system_busy_before_pct'] = busy_before
    try:
        scenario_kokiri(debug, proc, run_dir, args)
        result['boot_to_spot_s'] = round(time.monotonic() - started, 1)
        if args.shots or args.explore:
            result['shot'] = debug.shot(f'{run_dir}/spot.png')
        if args.explore:
            print(json.dumps(result, indent=1))
            return 0
        if args.affinity:
            result['affinity'] = apply_affinity(proc.pid, args.affinity)
        time.sleep(args.warmup)
        if args.train > 0:
            # Recorrido de entrenamiento para PGO: camara, caminar, menu. No mide.
            tour = [
                {'right_stick': [1, 0], 'frames': 45}, {'left_stick': [0, 1], 'frames': 50},
                {'left_stick': [1, 0], 'frames': 40}, {'buttons': ['A'], 'frames': 4},
                {'frames': 20}, {'left_stick': [0, -1], 'frames': 40},
                {'right_stick': [-1, 0], 'frames': 45}, {'left_stick': [-1, 0], 'frames': 40},
                {'buttons': ['Plus'], 'frames': 4}, {'frames': 40},
                {'buttons': ['L'], 'frames': 4}, {'frames': 25},
                {'buttons': ['Plus'], 'frames': 4}, {'frames': 40},
                {'left_stick': [0.7, 0.7], 'frames': 60}, {'buttons': ['B'], 'frames': 4},
                {'right_stick': [0, 1], 'frames': 20}, {'right_stick': [0, -1], 'frames': 20},
            ]
            end = time.monotonic() + args.train
            laps = 0
            while time.monotonic() < end and proc.poll() is None:
                debug.call('input_sequence', {'steps': tour}, timeout=300.0)
                laps += 1
            result['train_laps'] = laps
            try:
                result['pgo_dump'] = debug.call('pgo_dump')
            except RuntimeError as error:
                result['pgo_dump'] = str(error)
            args.repeat = 0
        idle0, total0 = cpu_times()
        own0 = process_cpu(proc.pid)
        result['runs'] = [measure(debug, args.seconds) for _ in range(args.repeat)]
        idle1, total1 = cpu_times()
        if total1 > total0:
            # Lo que gastaron los demas programas mientras se media, sobre el total de nucleos.
            others = (total1 - total0) - (idle1 - idle0) - (process_cpu(proc.pid) - own0)
            result['other_cpu_pct'] = round(100.0 * max(others, 0.0) / (total1 - total0), 1)
            result['disturbed'] = result['other_cpu_pct'] > args.max_busy
            if result['disturbed']:
                print(f"AVISO: otros programas usaron el {result['other_cpu_pct']} % de la CPU "
                      'durante la medida; resultado no valido')
        if not result['runs']:
            result['runs'] = [{'fps_avg': debug.status()['fps']}]
        result['fps_avg'] = round(statistics.mean(r['fps_avg'] for r in result['runs']), 2)
        if result['fps_avg'] < 1.0 and proc.poll() is None:
            # Juego congelado: las pilas de todos los hilos antes de cerrar (al cerrar cambian).
            result['frozen'] = True
            try:
                frozen = debug.call('sample', {'threads': '*', 'seconds': 1, 'max_chars': 0},
                                    timeout=120.0)
                shutil.copyfile(frozen['file'], f'{run_dir}/freeze_samples.txt')
                result['freeze_samples'] = f'{run_dir}/freeze_samples.txt'
            except Exception as error:
                result['freeze_samples'] = f'sin pilas: {error}'
        if args.timeline > 0:
            result['timeline'] = debug.call('timeline', {'seconds': args.timeline})
        if args.sample:
            sample = debug.call('sample', {'threads': args.sample_threads, 'seconds': 10,
                                           'max_chars': 0}, timeout=120.0)
            target = f'{run_dir}/samples.txt'
            shutil.copyfile(sample['file'], target)
            result['samples'] = target
            result['fps_while_sampling'] = debug.status()['fps']
    finally:
        if not args.explore:
            closed = 'ya cerrado'
            if proc.poll() is None:
                subprocess.run(['powershell', '-NoProfile', '-Command',
                                f'(Get-Process -Id {proc.pid}).CloseMainWindow() | Out-Null'],
                               capture_output=True)
                try:
                    proc.wait(timeout=45)
                    closed = 'limpio'
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=30)
                    time.sleep(3)
                    closed = 'forzado'
            result['close'] = closed
            for name in os.listdir(f'{APP}/user/log'):
                path = f'{APP}/user/log/{name}'
                if os.path.isfile(path):
                    shutil.copyfile(path, f'{run_dir}/{name}')
            result['host_pacing'] = host_pacing(f'{run_dir}/eden_log.txt')
            # La configuracion del banco vuelve a su estado previo (el emulador la reescribe al salir).
            open(config_path, 'wb').write(config_backup)
    json.dump(result, open(f'{run_dir}/result.json', 'w'), indent=1)
    print(json.dumps(result, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
