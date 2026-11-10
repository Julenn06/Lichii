"""Adapta la copia de qt-config.ini del banco de pruebas: rutas propias, sin sonido, sin dialogos."""
import sys

BS = chr(92)
path = sys.argv[1]
user = sys.argv[2].replace(BS, '/')
src = open(path, encoding='utf-8', errors='surrogateescape').read()
lines = src.split('\n')
want = {
    'nand_directory': f'{user}/nand',
    'sdmc_directory': f'{user}/sdmc',
    'load_directory': f'{user}/load',
    'dump_directory': f'{user}/dump',
    'tas_directory': f'{user}/tas',
    'Screenshots' + BS + 'screenshot_path': f'{user}/screenshots',
    'volume': '0',
    'audio_muted': 'true',
    'confirmStop': '2',
    'check_for_updates': 'false',
    'pauseWhenInBackground': 'false',
    'muteWhenInBackground': 'false',
    'fullscreen': 'false',
}
for extra in sys.argv[3:]:
    key, _, value = extra.partition('=')
    want[key] = value
suffix = BS + 'default'
seen = set()
out = []
for line in lines:
    key, sep, _ = line.partition('=')
    if sep:
        if key.endswith(suffix) and key[:-len(suffix)] in want:
            out.append(f'{key}=false')
            continue
        if key in want:
            out.append(f'{key}={want[key]}')
            seen.add(key)
            continue
    out.append(line)
open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(out))
print('patched:', sorted(seen))
print('missing:', sorted(set(want) - seen))
