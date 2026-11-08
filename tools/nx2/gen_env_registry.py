import os, re, sys, collections

root = sys.argv[1]
out_inc = sys.argv[2] if len(sys.argv) > 2 else None
names = collections.defaultdict(lambda: {"num": 0, "files": set()})
pat = re.compile(r'"(EDEN_(?:NX2_)?[A-Z0-9_]+)"')
num_re = re.compile(
    r"strto(u?ll|u?l|d|f)|ato(i|l|f)|stoi|stoul|from_chars|EnvDouble|EnvInt|EnvSeconds|ReadUnsigned|Nx2Env::(Unsigned|UnsignedOr|Signed|Decimal)|number\("
)
for d, _, fs in os.walk(os.path.join(root, "src")):
    if "android" in d:
        continue
    for f in fs:
        if not f.endswith((".cpp", ".h", ".inc")):
            continue
        p = os.path.join(d, f)
        L = open(p, encoding="utf-8", errors="replace").read().splitlines()
        for i, l in enumerate(L):
            if l.lstrip().startswith("//"):
                continue
            for m in pat.finditer(l):
                n = m.group(1)
                if n.endswith("_") or n in ("EDEN_DIR",):
                    continue
                names[n]["files"].add(os.path.relpath(p, root).replace(os.sep, "/"))
                if num_re.search(" ".join(L[max(0, i - 1) : i + 4])):
                    names[n]["num"] += 1
print(len(names), sum(1 for v in names.values() if v["num"]))
for n in sorted(names):
    print(n, "num" if names[n]["num"] else "text", ",".join(sorted(names[n]["files"]))[:140])
if out_inc:
    with open(out_inc, "w", newline="\n") as o:
        o.write("// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project\n")
        o.write("// SPDX-License-Identifier: GPL-3.0-or-later\n\n")
        o.write("// Every environment switch the emulator reads. Generated from the sources by\n")
        o.write("// tools/nx2/gen_env_registry.py; regenerate after adding or removing a switch.\n")
        o.write("// NX2_ENV(name, kind): Number values are parsed as integers or decimals, Text is\n")
        o.write("// anything (paths, hash lists, flags).\n\n")
        for n in sorted(names):
            kind = "Number" if names[n]["num"] else "Text"
            o.write(f"NX2_ENV({n}, {kind})\n")
