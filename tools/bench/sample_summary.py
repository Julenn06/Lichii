"""Resume un informe samples-*.txt del muestreador: por hilo, las N funciones con mas muestras."""
import re, sys
path = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 14
sections = (sys.argv[3].split(',') if len(sys.argv) > 3 else ['self (where'])
thread = None; mode = None; shown = 0
for line in open(path, encoding='utf-8', errors='replace'):
    line = line.rstrip('\n')
    m = re.match(r'=== thread "(.*)": (\d+) samples', line)
    if m:
        thread = m.group(1); print(f'\n### {thread}: {m.group(2)} muestras'); mode = None; continue
    if line.startswith('-- '):
        mode = line[3:]; shown = 0
        if any(mode.startswith(s) for s in sections): print('  [' + mode + ']')
        continue
    if mode and any(mode.startswith(s) for s in sections) and shown < top and line.strip():
        print('  ' + line.strip()[:150]); shown += 1
