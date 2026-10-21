#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

need=""
for b in x86_64-w64-mingw32-clang++ cmake ninja; do
    if ! command -v "$b" >/dev/null 2>&1; then
        need="$need $b"
    fi
done
if [ -n "$need" ]; then
    echo "missing tools:$need"
    echo "run: pkg install llvm-mingw-w64 cmake ninja"
    exit 1
fi

cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-llvm.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "--- output ---"
file build/rtprobe.exe
x86_64-w64-mingw32-objdump -p build/rtprobe.exe | grep 'DLL Name' || true
