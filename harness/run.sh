#!/bin/bash
# Test lab: runs ScriptHookV .NET (built exactly like the iPhone's) with real mods against a fake game, on GitHub's
# Mac, with .NET in the iPhone's mode. Output: hout/ (run.log = everything the run printed, incl. any crash backtrace)
set -uo pipefail
ROOT=$PWD; H=$ROOT/harness; W=$ROOT/hwork; OUT=$ROOT/hout
rm -rf "$W" "$OUT"; mkdir -p "$W" "$OUT"
SECS=${SECS:-75}

echo "== 1. ScriptHookV .NET for iOS"
shvdn/build-ios.sh > "$OUT/shvdn-build.log" 2>&1 || { tail -40 "$OUT/shvdn-build.log"; exit 1; }
DOTNET=$ROOT/out-shvdn-ios/dotnet; ls "$DOTNET"

echo "== 2. the same .NET runtime version as the iPhone, for macOS"
VER=8.0.33; id=microsoft.netcore.app.runtime.mono.osx-arm64
curl -fsSL -o "$W/rt.nupkg" "https://api.nuget.org/v3-flatcontainer/$id/$VER/$id.$VER.nupkg" || { echo "no runtime package"; exit 1; }
mkdir -p "$W/rtpkg" "$W/rt"; (cd "$W/rtpkg" && unzip -q ../rt.nupkg)
cp "$W"/rtpkg/runtimes/osx-arm64/lib/net8.0/*.dll "$W/rt/"; cp "$W"/rtpkg/runtimes/osx-arm64/native/* "$W/rt/"
ls "$W/rt" | grep -E "monosgen|CoreLib|System.Native"

echo "== 3. the mods, from their public source"
git clone -q https://github.com/jglassmaker/GTAV-BetterChasesPlus "$W/bc" && git -C "$W/bc" checkout -q c9b695efd2f9c300a4d2a22f7f69ac47f6e0f354
BCSRC="$W/bc/Better Chases+"
SHV3=""
for v in 3.0.2 3.0.4 3.1.0 3.6.0; do
  if dotnet build "$H/mods/BetterChases.csproj" -c Release -nologo -p:BCSRC="$BCSRC" -p:SHVDN3VER=$v -o "$W/modsout" > "$OUT/mod-build-$v.log" 2>&1; then SHV3=$v; break; fi
  grep -E "error" "$OUT/mod-build-$v.log" | head -5
done
[ -n "$SHV3" ] || { echo "couldn't build the mod"; exit 1; }
echo "built Better Chases+ against ScriptHookVDotNet3 $SHV3"
DOCS=$W/docs; mkdir -p "$DOCS/scripts"
cp "$W/modsout/Better Chases+.dll" "$DOCS/scripts/"; cp "$BCSRC/lib/NativeUI.dll" "$DOCS/scripts/"
ls -la "$DOCS/scripts"

echo "== 4. native parts (the iPhone's ScriptHook .NET server code + the fake game)"
clang -O1 -g -dynamiclib -o "$W/libfakegame.dylib" "$H/fakegame.c" "$H/native/shvdn_host.c" "$H/native/shvdn_exports.S" || exit 1
clang -O1 -g -o "$W/host" "$H/host.c" || exit 1

echo "== 5. run for $SECS s (under the debugger, so a crash shows where)"
lldb --batch \
  -o "process handle -p true -s false -n false SIGUSR1 SIGUSR2 SIGXCPU SIGPWR SIGPIPE" \
  -o "run" \
  -k "thread backtrace all" -k "quit 1" \
  -- "$W/host" "$W/rt" "$DOTNET" "$DOCS" "$W/libfakegame.dylib" "$SECS" 2>&1 | tee "$OUT/run.log"
cp "$DOCS"/*.log "$OUT/" 2>/dev/null; cp -R "$DOCS/posthook" "$OUT/" 2>/dev/null
grep -q "SURVIVED" "$OUT/run.log" && echo "RESULT: the mods ran without crashing" || echo "RESULT: CRASHED (see run.log)"
