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
cp "$W"/rtpkg/runtimes/osx-arm64/lib/net8.0/*.dll "$W/rt/"; cp "$W"/rtpkg/runtimes/osx-arm64/native/*.dylib "$W"/rtpkg/runtimes/osx-arm64/native/*.dll "$W/rt/"
ls "$W/rt" | grep -v "^System\.\|^Microsoft\.\|^mscorlib\|^netstandard\|^WindowsBase"

echo "== 3. the mods, from their public source"
git clone -q https://github.com/jglassmaker/GTAV-BetterChasesPlus "$W/bc" && git -C "$W/bc" checkout -q c9b695efd2f9c300a4d2a22f7f69ac47f6e0f354
BCSRC="$W/bc/Better Chases+"
git clone -q https://github.com/Guad/NativeUI "$W/nui" && git -C "$W/nui" checkout -q 1c6bfd48a6bce610acbec687a67b08d3c533ac64
NUI=""
for v in 2.10.12 2.10.10 2.10.9 2.10.8; do
  if dotnet build "$H/mods/NativeUI.csproj" -c Release -nologo -p:NUISRC="$W/nui/NativeUI" -p:SHVDN2VER=$v -o "$W/nuiout" > "$OUT/nui-build-$v.log" 2>&1; then NUI=$v; break; fi
  grep -E "error" "$OUT/nui-build-$v.log" | head -5
done
[ -n "$NUI" ] || { echo "couldn't build NativeUI"; exit 1; }
echo "built NativeUI against ScriptHookVDotNet2 $NUI"
SHV3=""
for v in 3.0.2 3.0.4 3.1.0 3.6.0; do
  if dotnet build "$H/mods/BetterChases.csproj" -c Release -nologo -p:BCSRC="$BCSRC" -p:NUIDLL="$W/nuiout/NativeUI.dll" -p:SHVDN3VER=$v -o "$W/modsout" > "$OUT/mod-build-$v.log" 2>&1; then SHV3=$v; break; fi
  grep -E "error" "$OUT/mod-build-$v.log" | head -5
done
[ -n "$SHV3" ] || { echo "couldn't build the mod"; exit 1; }
echo "built Better Chases+ against ScriptHookVDotNet3 $SHV3"
DOCS=$W/docs; mkdir -p "$DOCS/scripts"
cp "$W/modsout/Better Chases+.dll" "$DOCS/scripts/"; cp "$W/nuiout/NativeUI.dll" "$DOCS/scripts/"
# LemonUI (the menu library of most newer mods) + a small test mod that uses it
git clone -q https://github.com/LemonUIbyLemon/LemonUI "$W/lemon" && git -C "$W/lemon" checkout -q 72a7a9ab511f3b1d614c544d7d7075f3d4c2438b
rm -f "$W/lemon/Directory.Build.props"  # style checker only
if dotnet build "$W/lemon/LemonUI.SHVDN3/LemonUI.SHVDN3.csproj" -c Release -nologo -p:GeneratePackageOnBuild=false -p:LangVersion=12 -o "$W/lemonout" > "$OUT/lemonui-build.log" 2>&1 \
   && dotnet build "$H/mods/LemonTest.csproj" -c Release -nologo -p:LEMONDLL="$W/lemonout/LemonUI.SHVDN3.dll" -o "$W/lemontest" > "$OUT/lemontest-build.log" 2>&1; then
  cp "$W/lemonout/LemonUI.SHVDN3.dll" "$W/lemontest/LemonTest.dll" "$DOCS/scripts/"; echo "built LemonUI + test mod"
else grep -E "error" "$OUT"/lemon*-build.log | head -10; fi
ls -la "$DOCS/scripts"

# benchmark: popular open-source .NET mods, built from their public source
bench() {  # repo commit name srcsubdir uses_lemon
  local repo=$1 commit=$2 name=$3 sub=$4 lemon=$5 d="$W/bench/$3"
  git clone -q https://github.com/$repo "$d" && git -C "$d" checkout -q $commit || { echo "BENCH $name: clone failed"; return; }
  local L=""; [ "$lemon" = 1 ] && L="$W/lemonout/LemonUI.SHVDN3.dll"
  # natives these mods call by names older ScriptHookV .NET versions used (the built mods carry the numbers)
  grep -rl "Hash\._" "$d" --include=*.cs | while read -r f; do
    sed -i '' -e 's/Hash\._DOOR_CONTROL/(Hash)0x9B12F9A24FABEDB0/g' -e 's/Hash\._IS_VEHICLE_ROCKET_BOOST_ACTIVE/(Hash)0x3D34E80EED4AE3BE/g' "$f"; done
  if dotnet build "$H/mods/Generic.csproj" -c Release -nologo -p:SRC="$d/$sub" -p:NAME="$name" -p:LEMONDLL="$L" -p:SHVDNBUILD="$ROOT/out-shvdn-ios/build" -o "$W/bench-out/$name" > "$OUT/bench-$name-build.log" 2>&1; then
    cp "$W/bench-out/$name/$name.dll" "$DOCS/scripts/"; echo "BENCH $name: built"
  else echo "BENCH $name: build failed"; grep -E " error " "$OUT/bench-$name-build.log" | sed 's/.*error/error/' | sort -u | head -5; fi
}
bench lucasvinbr/GTA5GangMod 42bc3d5c07a9965c75c7eb18a96d19914bedca9f GangAndTurfMod . 1
bench kagikn/StuntBonusV eecb3dd87be2cb38a8d386cb6fd3528778629645 StuntBonusV StuntBonusV 0
bench waynieoaks/I-surrender 79c6d445c21a835d8bdb7ce11969ea4f951efe5a Surrender . 0
bench I-E-A-I-A-I-O/VigilanteMissions d2b3dea3afe8191a34f78a12ce7016ebffd8091b VigilanteMissions . 1
ls "$DOCS/scripts"

echo "== 4. native parts (the iPhone's ScriptHook .NET server code + the fake game)"
clang -O1 -g -dynamiclib -o "$W/libfakegame.dylib" "$H/fakegame.c" "$H/native/shvdn_host.c" "$H/native/shvdn_exports.S" "$H/native/pathfix.c" || exit 1
clang -O1 -g -o "$W/host" "$H/host.c" || exit 1

echo "== 5. run for $SECS s (under the debugger, so a crash shows where)"
lldb --batch \
  -o "process handle -p true -s false -n false SIGUSR1 SIGUSR2 SIGXCPU SIGPWR SIGPIPE" \
  -o "run" \
  -k "thread backtrace all" -k "quit 1" \
  -- "$W/host" "$W/rt" "$DOTNET" "$DOCS" "$W/libfakegame.dylib" "$SECS" 2>&1 | tee "$OUT/run.log"
cp "$DOCS"/*.log "$OUT/" 2>/dev/null; cp -R "$DOCS/posthook" "$OUT/" 2>/dev/null
echo "== files the mods wrote:"; (cd "$DOCS" && find . -type f | grep -v "\.dll$" | sort)
grep -q "SURVIVED" "$OUT/run.log" && echo "RESULT: the mods ran without crashing" || echo "RESULT: CRASHED (see run.log)"
