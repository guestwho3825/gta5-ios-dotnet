#!/usr/bin/env python3
"""Patches ScriptHookVDotNet (pinned commit) so it runs on .NET 8 inside the iOS port.

Every edit is an exact text replacement that must match once, so an upstream change can't be patched silently wrong.
Run from the SHVDN repo root.
"""
import re, sys

def edit(path, old, new, count=1):
    s = open(path, encoding='utf-8-sig').read()
    n = s.count(old)
    if n != count:
        sys.exit(f"patch failed: {path}: expected {count} match(es), found {n}:\n{old[:200]}")
    open(path, 'w', encoding='utf-8').write(s.replace(old, new))

def regex(path, pat, new, flags=re.S, count=1):
    s = open(path, encoding='utf-8-sig').read()
    s2, n = re.subn(pat, new, s, flags=flags)
    if n != count:
        sys.exit(f"patch failed: {path}: pattern matched {n} times, expected {count}: {pat[:120]}")
    open(path, 'w', encoding='utf-8').write(s2)

C = 'source/core/'
V3 = 'source/scripting_v3/'
V2 = 'source/scripting_v2/'

# ---- project files: .NET 8, no Windows-only packaging, our Windows Forms stand-in ----
for p in [C + 'ScriptHookVDotNet.csproj', V3 + 'ScriptHookVDotNet_APIv3.csproj', V2 + 'ScriptHookVDotNet_APIv2.csproj']:
    edit(p, '<TargetFramework>net48</TargetFramework>', '<TargetFramework>net8.0</TargetFramework>')
    s = open(p).read()
    s = s.replace('<UseWindowsForms>true</UseWindowsForms>', '').replace('<Platforms>x64</Platforms>', '')
    s = re.sub(r'<PackageReference Include="Costura\.Fody"[^>]*>.*?</PackageReference>', '', s, flags=re.S)
    s = s.replace('<Reference Include="System.Windows.Forms" />', '')
    s = s.replace('</Project>', '  <ItemGroup><ProjectReference Include="../../../shvdn/forms/System.Windows.Forms.csproj" /></ItemGroup>\n</Project>')
    s = s.replace('<PropertyGroup>', '<PropertyGroup><NoWarn>$(NoWarn);CS1591;SYSLIB0003;SYSLIB0004;SYSLIB0010;SYSLIB0011;SYSLIB0012;SYSLIB0051;CS0618;CS0162</NoWarn><GenerateDocumentationFile>false</GenerateDocumentationFile><TreatWarningsAsErrors>false</TreatWarningsAsErrors>', 1)
    open(p, 'w').write(s)
# v2's after-build step re-assembles the DLL with Windows' ildasm/ilasm, only to mark two constants for very old
# C++/CLI-built mods; C# mods (NativeUI and nearly everything else) inline constants, so it's left out
regex(V2 + 'ScriptHookVDotNet_APIv2.csproj', r'<UsingTask TaskName="ReplaceTextInFile".*?</UsingTask>', '')
regex(V2 + 'ScriptHookVDotNet_APIv2.csproj', r'<Target Name="AfterCompile">.*?</Target>', '')
# core's Fody (only used for Costura) goes too; the API keeps Fody for InlineIL
regex(C + 'ScriptHookVDotNet.csproj', r'<PackageReference Include="Fody"[^>]*>.*?</PackageReference>', '')
open(C + 'FodyWeavers.xml', 'w').write('<Weavers xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" />\n')

# ---- one script domain: .NET 8 has no AppDomain.CreateDomain ----
D = C + 'Scripting/ScriptDomain.cs'
regex(D, r'var setup = new AppDomainSetup\(\);.*?appdomain\.InitializeLifetimeService\(\);[^\n]*\n',
      '// iOS port: .NET 8 has a single app domain; scripts load into it directly\n')
regex(D, r'scriptdomain = \(ScriptDomain\)appdomain\.CreateInstanceFromAndUnwrap\([^;]*\);',
      's_iosScriptPath = scriptPath; scriptdomain = new ScriptDomain(basePath);')
# On PC the new domain's base directory *is* the scripts folder; here we say so directly
edit(D, 'public string ScriptPath => AppDomain.BaseDirectory;',
     'internal static string s_iosScriptPath;\n        public string ScriptPath => s_iosScriptPath ?? AppDomain.BaseDirectory;')
edit(D, 'AppDomain.Unload(appdomain);', '// (single domain: nothing to unload)')
edit(D, 'AppDomain.Unload(domain.AppDomain);', '// (single domain: nothing to unload)')

H = C + 'ScriptHookVDotNet.cs'
# the in-game console evaluates typed C# with the Roslyn compiler: no keyboard on iOS, and Roslyn needs newer
# libraries than this runtime has, so it stays off (scripts don't need it)
regex(H, r'console = \(Console\)domain\.AppDomain\.CreateInstanceFromAndUnwrap\([^;]*\);',
      'throw new PlatformNotSupportedException("the in-game console is not available on iOS (no keyboard); scripts are unaffected");')
edit(H, 'domain.AppDomain.DoCallBack(new CrossAppDomainDelegate(AssignConsoleFromDomain));', 'AssignConsoleFromDomain();')
# API next to the app, scripts/ini/log in Documents (the app bundle is read-only)
edit(H, 'domain = ScriptDomain.Load(".", Ini.ScriptPath);',
     'domain = ScriptDomain.Load(Environment.GetEnvironmentVariable("PH_SHVDN_API") ?? ".", '
     'System.IO.Path.Combine(Environment.GetEnvironmentVariable("PH_DOCS") ?? ".", Ini.ScriptPath));')
edit(C + 'Logging/Log.cs', 'private static string FilePath => Path.ChangeExtension(typeof(ScriptDomain).Assembly.Location, ".log");',
     'private static string FilePath => Path.Combine(Environment.GetEnvironmentVariable("PH_DOCS") ?? ".", "ScriptHookVDotNet.log");')
edit(C + 'Config/Ini.cs', 'File.ReadAllLines(Path.ChangeExtension(Assembly.GetExecutingAssembly().Location, ".ini"))',
     'File.ReadAllLines(Path.Combine(Environment.GetEnvironmentVariable("PH_DOCS") ?? ".", "ScriptHookVDotNet.ini"))')

# ---- memory scans: PC byte patterns can't match the iOS build, and scanning 70 MB per pattern in the interpreter
# would take minutes. Report "not found" at once; features needing them stay off, as their null checks expect. ----
M = C + 'Game/MemScanner.cs'
s = open(M).read()
n = s.count('ProcessModule module = Process.GetCurrentProcess().MainModule;')
if n != 4: sys.exit(f'patch failed: MemScanner: {n} module lookups')
s = s.replace('ProcessModule module = Process.GetCurrentProcess().MainModule;',
              'if (s_iosNoScan) return null; ProcessModule module = Process.GetCurrentProcess().MainModule;')
s = s.replace('public static class MemScanner\n    {', 'public static class MemScanner\n    {\n        private static readonly bool s_iosNoScan = true;', 1)
if 's_iosNoScan = true' not in s: sys.exit('patch failed: MemScanner class header')
open(M, 'w').write(s)
edit(C + 'Game/NativeMemory.cs',
     'GameFileVersion = new Version(FileVersionInfo.GetVersionInfo(Process.GetCurrentProcess().MainModule.FileName).FileVersion);',
     'GameFileVersion = new Version(1, 0, 2699, 0);  // iOS port: the game build is 2699')
print("SHVDN patched for iOS")
