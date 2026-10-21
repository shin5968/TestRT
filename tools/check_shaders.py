#!/usr/bin/env python3
"""Extract embedded HLSL from src/shaders.h and syntax-check with glslangValidator.

This is a development aid only; the shipping exe compiles HLSL at runtime with
D3DCompile (DXBC). glslangValidator accepts most HLSL but is not bit-identical
to FXC, so treat failures carefully.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "src" / "shaders.h").read_text()
OUT = ROOT / "build" / "shader_check"
OUT.mkdir(parents=True, exist_ok=True)

blocks = re.findall(r'R"HLSL\((.*?)\)HLSL"', SRC, re.S)
if len(blocks) != 2:
    print(f"expected 2 HLSL blocks, found {len(blocks)}")
    sys.exit(1)

path = OUT / "pathtracer.hlsl"
path.write_text(blocks[0])
comp = OUT / "composite.hlsl"
comp.write_text(blocks[1])

checks = [
    ([str(path), "-D", "-S", "comp", "-e", "main", "--target-env", "vulkan1.0"], "pathtracer"),
    ([str(comp), "-D", "-S", "vert", "-e", "VSMain", "--target-env", "vulkan1.0"], "composite_vs"),
    ([str(comp), "-D", "-S", "frag", "-e", "PSMain", "--target-env", "vulkan1.0"], "composite_ps"),
]

failed = False
for i, (args, name) in enumerate(checks):
    print(f"== {name}")
    args += ["-o", str(OUT / f"check{i}.spv")]
    r = subprocess.run(["glslangValidator", *args], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if out:
        print(out)
    if r.returncode != 0:
        failed = True
sys.exit(1 if failed else 0)
