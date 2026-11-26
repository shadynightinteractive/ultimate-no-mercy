#!/usr/bin/env python3
# [xplat 2026-11-12] Checks that the Vulkan shaders (src/main/shaders/*.vk.hlsl) hold the SAME HLSL as the Direct3D 12 backend's strings
# (src/main/arcade_view_d3d12.cpp: HLSL and HLSL_POST). Everything under each file's "---- the same as Direct3D 12 ----" mark must equal
# the Direct3D text from its first struct on, after two allowed differences: Vulkan's [[vk::location(N)]] on the vertex inputs, and ps_blit
# (a new pixel shader, Vulkan only). Run:  python3 tools/check_shaders.py   (exit 0: same)
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent / "src" / "main"
d3d = (root / "arcade_view_d3d12.cpp").read_text()
def raw(name):
    m = re.search(r'static const char\* ' + name + r' = R"\((.*?)\)";', d3d, re.S)
    if not m: sys.exit("no " + name + " in arcade_view_d3d12.cpp")
    return m.group(1)
def body(text, first):      # the lines from the first line starting with `first`
    lines = text.splitlines()
    i = next(i for i, l in enumerate(lines) if l.startswith(first))
    return lines[i:]
ok = True
for name, vk, first in (("HLSL", "arcade_view_scene.vk.hlsl", "struct VI"), ("HLSL_POST", "arcade_view_post.vk.hlsl", "struct VO")):
    a = body(raw(name), first)
    text = (root / "shaders" / vk).read_text()
    mark = "// ---- the same as Direct3D 12 ----"
    if mark not in text: sys.exit(vk + ": no mark")
    b = text.split(mark, 1)[1].lstrip("\n").splitlines()
    b = [re.sub(r"\[\[vk::location\(\d+\)\]\] ", "", l) for l in b if not l.startswith("float4 ps_blit") and l.strip() != ""]
    a = [l for l in a if l.strip() != ""]
    if a == b: print(f"{vk}: the same as {name} ({len(a)} lines)")
    else:
        ok = False; print(f"{vk}: DIFFERENT from {name}")
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y: print(f"  line {i}:\n    d3d12: {x[:160]}\n    vulkan: {y[:160]}"); break
        if len(a) != len(b): print(f"  {len(a)} lines vs {len(b)}")
sys.exit(0 if ok else 1)
