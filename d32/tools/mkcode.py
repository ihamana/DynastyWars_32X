#!/usr/bin/env python3
"""meta/code_addr.json (instruction addresses found by the code discovery) + the program ROM -> code.json
(pc -> [length, Musashi disassembly]) for tools/x68.py.  usage: mkcode.py ADDR_JSON PRG_BIN OUT_JSON"""
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m68k
m = json.load(open(sys.argv[1])); img = m68k.Image(open(sys.argv[2], 'rb').read())
code = {}
for a in m['code']:
    n, t = img.dasm(a); code[str(a)] = [n, t]
json.dump({'code': code, 'indirect': m['indirect'], 'wtab': m['wtab'], 'ptabs': m['ptabs']}, open(sys.argv[3], 'w'))
