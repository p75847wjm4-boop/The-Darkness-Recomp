"""List weapon and NPC (ai_*) template names and weapon models in each level archive.

Usage: python tools/list_weapon_templates.py [game dir] > weapons.txt
       python tools/list_weapon_templates.py --npcs [game dir] > npcs.txt
Reads Darkness/Content/Xdf/*.XDF (version 0x101: filename table, then one zlib
payload) and reports every weapon_* name found in the decompressed payload.
"""
import re
import struct
import sys
import zlib
from pathlib import Path

NAME = re.compile(rb'weapon_[A-Za-z0-9_]{2,40}')
if '--npcs' in sys.argv:
    sys.argv.remove('--npcs')
    NAME = re.compile(rb'ai_[A-Za-z0-9_]{2,48}')


def scan(path: Path):
    data = path.read_bytes()
    word = lambda at: struct.unpack_from('<I', data, at)[0]
    if len(data) < 16 or word(0) != 0x101:
        return None, set(), []
    strings = word(4)
    names = [n.decode('latin1') for n in data[8:8 + strings].split(b'\0') if n]
    files = 8 + strings + 4
    count = word(files - 4)
    blocks = files + count * 24 + 4
    block_count = word(blocks - 4)
    stream = blocks + block_count * 20
    try:
        payload = zlib.decompressobj().decompress(data[stream:])
    except zlib.error as error:
        return str(error), set(), names
    found = {m.group().decode() for m in NAME.finditer(payload)}
    found |= {m.group().decode() for m in NAME.finditer(data[8:8 + strings])}
    return None, found, names


def main() -> int:
    game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'Darkness'
    archives = sorted((game / 'Content' / 'Xdf').glob('*.XDF'))
    if not archives:
        print(f'No XDF archives under {game}', file=sys.stderr)
        return 1
    totals = {}
    for path in archives:
        error, found, names = scan(path)
        models = sorted(n for n in names if 'weapon' in n.lower() and n.lower().endswith('.xmd'))
        print(f'== {path.name}' + (f'  (error: {error})' if error else ''))
        for name in sorted(found, key=str.lower):
            print(f'  template {name}')
            totals.setdefault(name.lower(), set()).add(path.name)
        for name in models:
            print(f'  model    {name}')
    print('== SUMMARY')
    for name, where in sorted(totals.items()):
        print(f'  {name}: {len(where)} archives')
    return 0


if __name__ == '__main__':
    sys.exit(main())
