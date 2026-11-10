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
    args = parser.parse_args()
    global APP
    if args.app:
        APP = args.app.replace(chr(92), '/')

    run_dir = f'{RUNS}/{args.name}'
    os.makedirs(run_dir, exist_ok=True)
    if args.exe:
        shutil.copyfile(f'{args.exe}/lichii.exe', f'{APP}/lichii.exe')
        if os.path.exists(f'{args.exe}/lichii.pdb'):
            shutil.copyfile(f'{args.exe}/lichii.pdb', f'{APP}/lichii.pdb')
        elif os.path.exists(f'{APP}/lichii.pdb'):
            os.remove(f'{APP}/lichii.pdb')
    for name in os.listdir(f'{APP}/user/log'):
        path = f'{APP}/user/log/{name}'
        if os.path.isfile(path):
            os.remove(path)

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
    try:
        scenario_kokiri(debug, proc, run_dir, args)
        result['boot_to_spot_s'] = round(time.monotonic() - started, 1)
        if args.shots or args.explore:
            result['shot'] = debug.shot(f'{run_dir}/spot.png')
        if args.explore:
            print(json.dumps(result, indent=1))
            return 0
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
        result['runs'] = [measure(debug, args.seconds) for _ in range(args.repeat)]
        if not result['runs']:
            result['runs'] = [{'fps_avg': debug.status()['fps']}]
        result['fps_avg'] = round(statistics.mean(r['fps_avg'] for r in result['runs']), 2)
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
                    closed = 'forzado'
            result['close'] = closed
            for name in os.listdir(f'{APP}/user/log'):
                path = f'{APP}/user/log/{name}'
                if os.path.isfile(path):
                    shutil.copyfile(path, f'{run_dir}/{name}')
    json.dump(result, open(f'{run_dir}/result.json', 'w'), indent=1)
    print(json.dumps(result, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
