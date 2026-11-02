// iOS port: ped / vehicle / object / pickup lists come from the native ScriptHook (the same lists C plugins use,
// with its script-ID pool protection), made on the game thread. Model and distance filters are applied there too,
// so a whole GetNearbyPeds etc. is a single trip to the game thread.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace SHVDN
{
    internal static unsafe class IosEntities
    {
        public const int Peds = 0, Vehicles = 1, Objects = 2, Pickups = 3;

        [DllImport("ScriptHookV.dll", EntryPoint = "shv_worldGetFiltered", ExactSpelling = true)]
        static extern int WorldGetFiltered(int which, int* arr, int size, float* pos, float radius, int* models, int nModels);

        [DllImport("ScriptHookV.dll", EntryPoint = "shv_breadcrumb", ExactSpelling = true)]
        static extern void Breadcrumb(byte* s);

        // tells the native side which mod script is running, so a crash note can name it
        public static void Crumb(string s)
        {
            if (s == null) { Breadcrumb(null); return; }
            int n = Math.Min(s.Length, 120);
            byte* b = stackalloc byte[n + 1];
            for (int i = 0; i < n; i++) b[i] = s[i] < 128 ? (byte)s[i] : (byte)'?';
            b[n] = 0;
            Breadcrumb(b);
        }

        // scripts turned off after they crashed the game (the native side writes the list; one name per line)
        static HashSet<string> s_disabled;
        public static bool IsDisabled(string name)
        {
            if (s_disabled == null)
            {
                var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                try
                {
                    string docs = Environment.GetEnvironmentVariable("PH_DOCS");
                    string f = docs == null ? null : Path.Combine(docs, "posthook", "disabled-scripts.txt");
                    if (f != null && File.Exists(f))
                        foreach (string line in File.ReadAllLines(f))
                            if (line.Trim().Length > 0) set.Add(line.Trim());
                }
                catch { }
                s_disabled = set;
            }
            return name != null && s_disabled.Contains(name);
        }

        public static int[] Get(int which, FVector3? position, float radius, int[] modelHashes)
        {
            var buf = new int[2048];
            int n;
            bool byModel = modelHashes != null && modelHashes.Length > 0;
            float* pos = stackalloc float[3];
            if (position.HasValue) { pos[0] = position.Value.X; pos[1] = position.Value.Y; pos[2] = position.Value.Z; }
            fixed (int* p = buf)
            fixed (int* m = byModel ? modelHashes : null)
                n = WorldGetFiltered(which, p, buf.Length, position.HasValue ? pos : null, radius, m, byModel ? modelHashes.Length : 0);
            if (n <= 0) return Array.Empty<int>();
            var result = new int[n];
            Array.Copy(buf, result, n);
            return result;
        }
    }
}
