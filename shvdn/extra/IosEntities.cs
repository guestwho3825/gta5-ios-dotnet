// iOS port: ped / vehicle / object / pickup lists come from the native ScriptHook (the same lists C plugins use,
// with its script-ID pool protection), fetched on the game thread. Model and distance filters are applied here.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace SHVDN
{
    internal static unsafe class IosEntities
    {
        public const int Peds = 0, Vehicles = 1, Objects = 2, Pickups = 3;
        const ulong GET_ENTITY_MODEL = 0x9F47B058362C84B5UL, GET_ENTITY_COORDS = 0x3FEF770D40960D5AUL;

        [DllImport("ScriptHookV.dll", EntryPoint = "shv_worldGetAll", ExactSpelling = true)]
        static extern int WorldGetAll(int which, int* arr, int size);

        public static int[] Get(int which, FVector3? position, float radius, int[] modelHashes)
        {
            var buf = new int[2048];
            int n;
            fixed (int* p = buf) n = WorldGetAll(which, p, buf.Length);
            if (n <= 0) return Array.Empty<int>();

            bool byModel = modelHashes != null && modelHashes.Length > 0, byPos = position.HasValue;
            if (!byModel && !byPos) { var all = new int[n]; Array.Copy(buf, all, n); return all; }

            var list = new List<int>(n);
            float r2 = radius * radius;
            for (int i = 0; i < n; i++)
            {
                int h = buf[i];
                if (byModel)
                {
                    int model = (int)*NativeFunc.Invoke(GET_ENTITY_MODEL, (ulong)(uint)h);
                    if (Array.IndexOf(modelHashes, model) < 0) continue;
                }
                if (byPos)
                {
                    ulong* c = NativeFunc.Invoke(GET_ENTITY_COORDS, (ulong)(uint)h, 1UL);
                    float x = *(float*)&c[0], y = *(float*)&c[1], z = *(float*)&c[2];
                    FVector3 o = position.Value;
                    float dx = x - o.X, dy = y - o.Y, dz = z - o.Z;
                    if (dx * dx + dy * dy + dz * dz > r2) continue;
                }
                list.Add(h);
            }
            return list.ToArray();
        }
    }
}
