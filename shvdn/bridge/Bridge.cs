// Bridge between the iOS port's native ScriptHook (d3d11.dylib) and ScriptHookV .NET.
// Called from native code with mono_runtime_invoke. Every entry point catches its own errors and returns them as
// text, so a broken mod can never take the game down through here.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace PostHook
{
    public static class Bridge
    {
        static IntPtr s_hook;  // d3d11.dylib: our ScriptHook, which also stands in for ScriptHookV.dll, kernel32 and user32

        static IntPtr Resolve(string name)
        {
            string n = name.ToLowerInvariant();
            if (n.EndsWith(".dll")) n = n.Substring(0, n.Length - 4);
            if (n == "scripthookv" || n == "kernel32" || n == "user32")
            {
                if (s_hook == IntPtr.Zero) s_hook = NativeLibrary.Load(Environment.GetEnvironmentVariable("PH_D3D11"));
                return s_hook;
            }
            return IntPtr.Zero;
        }

        // ScriptHookV .NET finds some PC game functions by searching the game's code for byte patterns; those don't exist
        // on iOS, so it has no address for them. A mod that used one called address 0 and the game died without a word.
        // Every missing one now points at a native stub that does nothing and returns 0 (callers treat 0 as "not found").
        static void FillMissingGameFunctions()
        {
            IntPtr hook = Resolve("ScriptHookV.dll");
            IntPtr stub = hook == IntPtr.Zero ? IntPtr.Zero : NativeLibrary.GetExport(hook, "shv_null_stub");
            if (stub == IntPtr.Zero) { SHVDN.Log.Message(SHVDN.Log.Level.Error, "iOS: no stub for missing game functions"); return; }
            int n = 0;
            foreach (Type t in typeof(SHVDN.ScriptHookVDotNet).Assembly.GetTypes())
                foreach (MethodInfo m in t.GetMethods(BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly))
                    if (m.Name.StartsWith("IosFill_", StringComparison.Ordinal)) { m.Invoke(null, new object[] { stub }); n++; }
            SHVDN.Log.Message(SHVDN.Log.Level.Info, "iOS: ", n.ToString(), " PC-only game functions point at a safe stub");
        }

        // Called once, on the .NET thread, after the runtime starts. Returns null on success, else the error.
        public static string Init(long getTls, long setTls, int gameThreadId)
        {
            try
            {
                string docs = Environment.GetEnvironmentVariable("PH_DOCS");
                string app = Environment.GetEnvironmentVariable("PH_APP");
                Directory.SetCurrentDirectory(docs);

                // an error nobody caught on any thread (a mod's own thread, a timer, a task): write it down first,
                // before the runtime deals with it (Files > GTA V > posthook > errors.txt)
                AppDomain.CurrentDomain.UnhandledException += (s, e) =>
                {
                    try
                    {
                        Directory.CreateDirectory(Path.Combine(docs, "posthook"));
                        File.AppendAllText(Path.Combine(docs, "posthook", "errors.txt"),
                            $"[{DateTime.Now:HH:mm:ss}] error nobody caught (thread \"{System.Threading.Thread.CurrentThread.Name}\"):\n{e.ExceptionObject}\n\n");
                        SHVDN.Log.Message(SHVDN.Log.Level.Error, "Error nobody caught: ", e.ExceptionObject?.ToString());
                    }
                    catch { }
                };
                System.Threading.Tasks.TaskScheduler.UnobservedTaskException += (s, e) => e.SetObserved();

                // calls into ScriptHookV / Windows: point them at our ScriptHook
                NativeLibrary.SetDllImportResolver(typeof(SHVDN.ScriptHookVDotNet).Assembly, (name, asm, path) => Resolve(name));
                AssemblyLoadContext.Default.ResolvingUnmanagedDll += (asm, name) => Resolve(name);  // mods' own imports
                // mods built against Windows Forms get our stand-in
                AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
                    new AssemblyName(e.Name).Name == "System.Windows.Forms" ? typeof(System.Windows.Forms.Keys).Assembly : null;
                System.Windows.Forms.MessageBox.Sink = text => SHVDN.Log.Message(SHVDN.Log.Level.Warning, "[MessageBox] ", text);

                // ScriptHookV .NET's settings file (it reports an error if there's none)
                string ini = Path.Combine(docs, "ScriptHookVDotNet.ini");
                if (!File.Exists(ini))
                    File.WriteAllText(ini, "; ScriptHookV .NET settings (iOS port). Scripts go in the scripts folder next to this file.\nScriptsLocation=scripts\n");

                // the scripts folder, with the bundled test mod
                string scripts = Path.Combine(docs, "scripts");
                Directory.CreateDirectory(scripts);
                // mods bundled with this build: .dll files are refreshed every launch, other files (configs) are only
                // copied when missing so your own settings stay
                string bundled = Path.Combine(app, "dotnet-scripts");
                if (Directory.Exists(bundled))
                    foreach (string f in Directory.GetFiles(bundled))
                    {
                        string dst = Path.Combine(scripts, Path.GetFileName(f));
                        if (f.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) || !File.Exists(dst)) File.Copy(f, dst, true);
                    }
                // the HelloIOS test mod has done its job
                string hello = Path.Combine(scripts, "HelloIOS.dll");
                if (File.Exists(hello) && !File.Exists(Path.Combine(bundled, "HelloIOS.dll"))) File.Delete(hello);

                SHVDN.ScriptHookVDotNet.Init(new IntPtr(getTls), new IntPtr(setTls), IntPtr.Zero, (uint)gameThreadId);
                FillMissingGameFunctions();
                return null;
            }
            catch (Exception ex) { return ex.ToString(); }
        }

        // Once per game frame, on the .NET thread, while the game's script thread waits.
        public static string Tick()
        {
            try { SHVDN.ScriptHookVDotNet.Tick(); return null; }
            catch (Exception ex) { return ex.ToString(); }
        }

        public static string Key(int key, int down, int ctrl, int shift, int alt)
        {
            try { SHVDN.ScriptHookVDotNet.KeyboardMessage((ulong)key, down != 0, ctrl != 0, shift != 0, alt != 0); return null; }
            catch (Exception ex) { return ex.ToString(); }
        }
    }
}
