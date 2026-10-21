#!/usr/bin/env python3
"""Generate a C++ header with an embedded SPIR-V module.

usage: gen_spv_header.py <input.spv> <output.h> <symbol>
"""
import sys
from pathlib import Path

if len(sys.argv) != 4:
    print("usage: gen_spv_header.py <input.spv> <output.h> <symbol>")
    sys.exit(2)

src = Path(sys.argv[1]).read_bytes()
if len(src) % 4 != 0:
    print("SPIR-V size is not a multiple of 4")
    sys.exit(1)

out = Path(sys.argv[2])
symbol = sys.argv[3]
words = [int.from_bytes(src[i:i + 4], "little") for i in range(0, len(src), 4)]

lines = []
lines.append("#pragma once")
lines.append("#include <cstddef>")
lines.append("#include <cstdint>")
lines.append("")
lines.append(f"static const uint32_t {symbol}[] = {{")
for i in range(0, len(words), 8):
    chunk = ", ".join(f"0x{w:08x}u" for w in words[i:i + 8])
    lines.append(f"    {chunk},")
lines.append("};")
lines.append(f"static const size_t {symbol}WordCount = {len(words)};")
lines.append("")
out.write_text("\n".join(lines))
print(f"wrote {out} ({len(words)} words)")
