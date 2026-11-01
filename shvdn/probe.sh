#!/bin/bash
# Probe: build ScriptHookVDotNet (core + v3 API) for .NET 8 and list what fails, grouped, to size the iOS port.
set -uo pipefail
OUT=$PWD/out-shvdn; rm -rf "$OUT"; mkdir -p "$OUT"
git clone -q --depth 1 https://github.com/scripthookvdotnet/scripthookvdotnet src-shvdn
cd src-shvdn/source
for p in core/ScriptHookVDotNet.csproj scripting_v3/ScriptHookVDotNet_APIv3.csproj; do
  sed -i '' -e 's#<TargetFramework>net48</TargetFramework>#<TargetFramework>net8.0</TargetFramework>#' \
            -e 's#<UseWindowsForms>true</UseWindowsForms>##' -e 's#<Platforms>x64</Platforms>##' "$p"
  # drop Fody/Costura weaving (Windows packaging helpers)
  perl -0pi -e 's#<PackageReference Include="(Costura\.Fody|Fody)"[^>]*>.*?</PackageReference>##gs' "$p"
done
dotnet --version
for p in core/ScriptHookVDotNet.csproj scripting_v3/ScriptHookVDotNet_APIv3.csproj; do
  n=$(basename "$p" .csproj)
  dotnet build "$p" -c Release -nologo -clp:NoSummary 2>&1 | tee "$OUT/$n.log" | tail -3
  grep -oE "error CS[0-9]+: [^\[]*" "$OUT/$n.log" | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -40 > "$OUT/$n.errors-by-kind.txt"
  grep -oE "[A-Za-z0-9_/.]+\.cs\([0-9]+" "$OUT/$n.log" | sed -E 's/\([0-9]+$//' | sort | uniq -c | sort -rn | head -60 > "$OUT/$n.errors-by-file.txt"
  grep -E "error CS" "$OUT/$n.log" | sed -E 's#.*/source/##' | sort -u | head -400 > "$OUT/$n.errors.txt"
done
ls -la "$OUT"
