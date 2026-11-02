#!/bin/bash
# Builds ScriptHookV .NET for the iOS port (.NET 8 on Mono): patched SHVDN core + v3 API, the Windows Forms
# stand-in, the native bridge and the test mod. Output: out-shvdn-ios/
set -euo pipefail
SHVDN_COMMIT=681938f2002db44b05adccbe711cee828059acb7
ROOT=$PWD; OUT=$ROOT/out-shvdn-ios
rm -rf "$OUT" src-shvdn; mkdir -p "$OUT"
git clone -q https://github.com/scripthookvdotnet/scripthookvdotnet src-shvdn
git -C src-shvdn checkout -q $SHVDN_COMMIT
(cd src-shvdn && python3 ../shvdn/patch_shvdn.py)
dotnet --version
B="-c Release -nologo -clp:NoSummary -p:SHVDN_VERSION=3.6.0.0"
dotnet build shvdn/forms/System.Windows.Forms.csproj $B -o "$OUT/build" 2>&1 | tee "$OUT/forms.log" | tail -3
dotnet build src-shvdn/source/core/ScriptHookVDotNet.csproj $B -o "$OUT/build" 2>&1 | tee "$OUT/core.log" | grep -E "error|Warn|Error" | tail -40
dotnet build src-shvdn/source/scripting_v3/ScriptHookVDotNet_APIv3.csproj $B -o "$OUT/build" 2>&1 | tee "$OUT/v3.log" | grep -E "error|Error" | tail -40
dotnet build src-shvdn/source/scripting_v2/ScriptHookVDotNet_APIv2.csproj -c Release -nologo -clp:NoSummary -o "$OUT/build" 2>&1 | tee "$OUT/v2.log" | grep -E "error|Error" | tail -40
dotnet build shvdn/bridge/PostHook.DotNet.csproj $B -o "$OUT/build" 2>&1 | tee "$OUT/bridge.log" | grep -E "error|Error" | tail -20
dotnet build shvdn/testmod/HelloIOS.csproj $B -o "$OUT/testmod" 2>&1 | tee "$OUT/testmod.log" | grep -E "error|Error" | tail -20
mkdir -p "$OUT/dotnet" "$OUT/dotnet-scripts"
for f in ScriptHookVDotNet.dll ScriptHookVDotNet2.dll ScriptHookVDotNet3.dll System.Windows.Forms.dll PostHook.DotNet.dll; do cp "$OUT/build/$f" "$OUT/dotnet/"; done
cp "$OUT/testmod/HelloIOS.dll" "$OUT/dotnet-scripts/"
ls -la "$OUT/dotnet" "$OUT/dotnet-scripts" "$OUT/build" | head -60
