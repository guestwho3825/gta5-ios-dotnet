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

        // Called once, on the .NET thread, after the runtime starts. Returns null on success, else the error.
        public static string Init(long getTls, long setTls, int gameThreadId)
        {
            try
            {
                string docs = Environment.GetEnvironmentVariable("PH_DOCS");
                string app = Environment.GetEnvironmentVariable("PH_APP");
                Directory.SetCurrentDirectory(docs);

                // calls into ScriptHookV / Windows: point them at our ScriptHook
                NativeLibrary.SetDllImportResolver(typeof(SHVDN.ScriptHookVDotNet).Assembly, (name, asm, path) => Resolve(name));
                AssemblyLoadContext.Default.ResolvingUnmanagedDll += (asm, name) => Resolve(name);  // mods' own imports
                // mods built against Windows Forms get our stand-in
                AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
                    new AssemblyName(e.Name).Name == "System.Windows.Forms" ? typeof(System.Windows.Forms.Keys).Assembly : null;
                System.Windows.Forms.MessageBox.Sink = text => SHVDN.Log.Message(SHVDN.Log.Level.Warning, "[MessageBox] ", text);

                // the scripts folder, with the bundled test mod
                string scripts = Path.Combine(docs, "scripts");
                Directory.CreateDirectory(scripts);
                string bundled = Path.Combine(app, "dotnet-scripts");
                if (Directory.Exists(bundled))
                    foreach (string f in Directory.GetFiles(bundled, "*.dll"))
                        File.Copy(f, Path.Combine(scripts, Path.GetFileName(f)), true);

                SHVDN.ScriptHookVDotNet.Init(new IntPtr(getTls), new IntPtr(setTls), IntPtr.Zero, (uint)gameThreadId);
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
