#!/bin/bash
# Precompiles System.Private.CoreLib for the iPhone so Mono's iOS runtime can start without JIT.
# Runs on a GitHub macOS machine. Output: out/libcorlib-aot.dylib (+ build log).
#
# Microsoft's iOS Mono runtime has its code generator removed; at startup it needs the trampolines (small pieces of
# glue code) from an ahead-of-time compiled CoreLib. The "interp" AOT mode compiles only those, the rest of .NET
# then runs in the interpreter.
set -euo pipefail
VER=8.0.33
WORK=$PWD/work; OUT=$PWD/out
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK" "$OUT"
cd "$WORK"

get() {  # id -> extracted folder
  local id=$1 lid; lid=$(echo "$1" | tr 'A-Z' 'a-z')
  curl -fsSL -o "$lid.nupkg" "https://api.nuget.org/v3-flatcontainer/$lid/$VER/$lid.$VER.nupkg" || return 1
  mkdir -p "$lid" && (cd "$lid" && unzip -q ../"$lid.nupkg")
  echo "$WORK/$lid"
}

RT=$(get Microsoft.NETCore.App.Runtime.Mono.ios-arm64)
HOSTARCH=$(uname -m)
CROSS=""
for host in osx-arm64 osx-x64; do
  if CROSS=$(get Microsoft.NETCore.App.Runtime.AOT.$host.Cross.ios-arm64); then echo "precompiler: $host"; break; fi
done
[ -n "$CROSS" ] || { echo "no precompiler package found"; exit 1; }
AOTBIN=$(find "$CROSS" -name mono-aot-cross -type f | head -1)
chmod +x "$AOTBIN"
echo "precompiler binary: $AOTBIN ($(file -b "$AOTBIN"))"

CORELIB=$(find "$RT" -name System.Private.CoreLib.dll | head -1)
echo "corelib: $CORELIB"
echo "corelib sha256: $(shasum -a 256 "$CORELIB" | cut -c1-64)"
echo "app's corelib : $(cat ../aot/corlib.sha256)"

LIBDIR=$(dirname "$(find "$RT" -path '*lib/net8.0/System.Runtime.dll' | head -1)")
mkdir -p asm
cp "$CORELIB" asm/
cp "$LIBDIR"/*.dll asm/ 2>/dev/null || true

# interp: only what the interpreter needs (trampolines + wrappers); static: exported module symbol for registration
MONO_PATH="$WORK/asm" "$AOTBIN" \
  --aot=mtriple=arm64-ios,static,asmonly,interp,outfile="$WORK/corlib.s" \
  "$WORK/asm/System.Private.CoreLib.dll" 2>&1 | tail -40

SDK=$(xcrun --sdk iphoneos --show-sdk-path)
xcrun --sdk iphoneos clang -c -arch arm64 -miphoneos-version-min=15.0 "$WORK/corlib.s" -o "$WORK/corlib.o"
# the module's info symbol is what the host passes to mono_aot_register_module()
nm -g "$WORK/corlib.o" | grep -i "mono_aot_module" | head -5
xcrun --sdk iphoneos clang -dynamiclib -arch arm64 -miphoneos-version-min=15.0 -isysroot "$SDK" \
  -install_name @rpath/libcorlib-aot.dylib -Wl,-undefined,dynamic_lookup \
  "$WORK/corlib.o" -o "$OUT/libcorlib-aot.dylib"
nm -gU "$OUT/libcorlib-aot.dylib" | grep -i "mono_aot_module" > "$OUT/exports.txt" || true
ls -la "$OUT"
echo "done"
