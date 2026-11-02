// A stand-in for GTA V and the port's native ScriptHook, so ScriptHookV .NET and real mods can be run and debugged
// on a Mac (GitHub's build machine) with the same .NET runtime mode as the iPhone. Together with native/shvdn_host.c
// (the exact code the iPhone build uses) this makes libfakegame.dylib, which mods see as "ScriptHookV.dll".
//
// The fake game runs at 40 frames a second on its own thread and answers game calls (natives) with plausible values:
// a player ped at a fixed spot, a few peds and cars nearby, every model/texture "loaded". Every native a mod calls is
// counted and listed at the end. After 20 s the player gets 2 stars so chase code runs too, and at 30 s F7 is pressed.
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "native/native_names.h"

#define API __attribute__((visibility("default")))

static double t0;
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

API void fx_log(const char *fmt, ...) {
  char b[4096]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  printf("[%7.3f] %s\n", now_s() - t0, b);
  fflush(stdout);
}

// ---- scripts and frames ----
static void (*g_scripts[16])(void);
static int g_nScripts;
typedef void (*KeyboardHandler)(uint32_t, uint16_t, uint8_t, int, int, int, int);
static KeyboardHandler g_keys[8];
static int g_nKeys;
static _Atomic unsigned g_frame;
API void scriptRegister(void *module, void (*fn)(void)) { (void)module; if (g_nScripts < 16) g_scripts[g_nScripts++] = fn; }
API void keyboardHandlerRegister(KeyboardHandler h) { if (g_nKeys < 8) g_keys[g_nKeys++] = h; }
static int g_wanted;
static int g_pressedF7;
API void scriptWait(uint32_t ms) {
  (void)ms;
  usleep(25000);  // one frame at 40 FPS
  unsigned f = atomic_fetch_add(&g_frame, 1) + 1;
  double t = now_s() - t0;
  if (!g_wanted && t > 20) { g_wanted = 2; fx_log("[game] the player now has 2 stars"); }
  static int pressedF9;
  if (!pressedF9 && t > 50) {
    pressedF9 = 1; fx_log("[game] pressing F9 (LemonUI test menu)");
    for (int i = 0; i < g_nKeys; i++) { g_keys[i](0x78, 1, 0, 0, 0, 0, 0); g_keys[i](0x78, 1, 0, 0, 0, 1, 1); }
  }
  if (!g_pressedF7 && t > 30) {
    g_pressedF7 = 1;
    fx_log("[game] pressing F7");
    for (int i = 0; i < g_nKeys; i++) { g_keys[i](0x76, 1, 0, 0, 0, 0, 0); g_keys[i](0x76, 1, 0, 0, 0, 1, 1); }
  }
  if (f % 400 == 0) fx_log("[game] frame %u", f);
  { // the on-screen menu pad: when would it show, and does pressing it work
    extern int net_menu_active(void); extern void net_vpad(int, int);
    static int last = -1; int m = net_menu_active();
    if (m != last) { fx_log("[game] mod menu %s (touch pad %s)", m ? "open" : "closed", m ? "shown" : "hidden"); last = m; }
    static const struct { double t; int b, down; } P[] = {{34, 1, 1}, {34.2, 1, 0}, {35, 1, 1}, {35.2, 1, 0}, {36, 4, 1}, {36.2, 4, 0},
                                                         {38, 5, 1}, {38.2, 5, 0}, {40, 5, 1}, {40.2, 5, 0},
                                                         {52, 1, 1}, {52.2, 1, 0}, {53, 1, 1}, {53.2, 1, 0}, {54, 1, 1}, {54.2, 1, 0},
                                                         {55, 4, 1}, {55.2, 4, 0}, {57, 5, 1}, {57.2, 5, 0}, {58, 0, 1}, {58.2, 0, 0},
                                                         {58.6, 0, 1}, {58.8, 0, 0}, {59.5, 4, 1}, {59.7, 4, 0}, {61, 5, 1}, {61.2, 5, 0}};
    static unsigned pi;
    while (pi < sizeof P / sizeof P[0] && t >= P[pi].t) {
      fx_log("[game] touch pad: %s %s", (const char *[]){"up", "down", "left", "right", "OK", "back"}[P[pi].b], P[pi].down ? "pressed" : "released");
      net_vpad(P[pi].b, P[pi].down); pi++;
    }
  }
}
// the game's script thread: runs the registered script (the .NET server script never returns)
API void fake_game_run(void) {
  fx_log("[game] script thread running %d script(s)", g_nScripts);
  if (g_nScripts) g_scripts[0]();
}

// ---- natives ----
typedef struct { uint64_t hash; const char *name; unsigned calls; } seen_t;
static seen_t g_seen[4096];
static int g_nSeen;
static const char *name_of(uint64_t h) {
  for (size_t i = 0; i < sizeof SH_NAMES / sizeof SH_NAMES[0]; i++) if (SH_NAMES[i].hash == h) return SH_NAMES[i].name;
  return "?";
}
static seen_t *seen(uint64_t h) {
  for (int i = 0; i < g_nSeen; i++) if (g_seen[i].hash == h) return &g_seen[i];
  if (g_nSeen >= 4096) return &g_seen[0];
  g_seen[g_nSeen].hash = h; g_seen[g_nSeen].name = name_of(h); g_seen[g_nSeen].calls = 0;
  return &g_seen[g_nSeen++];
}
API void fake_print_natives(void) {
  fx_log("[game] natives called by the mods (%d different):", g_nSeen);
  for (int i = 0; i < g_nSeen; i++) printf("    %6u  %s (%016llx)\n", g_seen[i].calls, g_seen[i].name, (unsigned long long)g_seen[i].hash);
  fflush(stdout);
}

static uint64_t g_hash, g_args[32], g_ret[4];
static int g_n;
API void nativeInit(uint64_t hash) { g_hash = hash; g_n = 0; }
API void nativePush64(uint64_t v) { if (g_n < 32) g_args[g_n++] = v; }

static uint64_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int ent_ok(uint64_t h) { return (int)h >= 1 && (int)h <= 40; }
static const char *g_label = "LABEL";

API uint64_t *nativeCall(void) {
  memset(g_ret, 0, sizeof g_ret);
  seen_t *s = seen(g_hash); s->calls++;
  const char *n = s->name;
  uint64_t a0 = g_n > 0 ? g_args[0] : 0;
  double t = now_s() - t0;
#define IS(x) (!strcmp(n, x))
  if (IS("PLAYER_ID") || IS("GET_PLAYER_INDEX")) g_ret[0] = 0;
  else if (IS("PLAYER_PED_ID") || IS("GET_PLAYER_PED") || IS("GET_PLAYER_PED_SCRIPT_INDEX")) g_ret[0] = 1;
  else if (IS("DOES_ENTITY_EXIST") || IS("IS_ENTITY_A_PED") ) g_ret[0] = ent_ok(a0) && (IS("DOES_ENTITY_EXIST") || (int)a0 < 10);
  else if (IS("IS_ENTITY_A_VEHICLE")) g_ret[0] = (int)a0 >= 10 && (int)a0 < 20;
  else if (IS("GET_ENTITY_COORDS") || IS("GET_PED_BONE_COORDS") || IS("GET_WORLD_POSITION_OF_ENTITY_BONE") ||
           IS("GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS") || IS("GET_GAMEPLAY_CAM_COORD") || IS("GET_FINAL_RENDERED_CAM_COORD")) {
    float k = (float)(int)a0;
    g_ret[0] = f2u(100 + k * 3); g_ret[1] = f2u(200 + k * 2); g_ret[2] = f2u(30);
  } else if (IS("GET_ENTITY_VELOCITY") || IS("GET_ENTITY_ROTATION") || IS("GET_GAMEPLAY_CAM_ROT")) { /* zeros */ }
  else if (IS("GET_ENTITY_MODEL")) g_ret[0] = (int)a0 == 1 ? 0x9B22DBAF : (int)a0 < 6 ? 0x5E3DA4A4 : (int)a0 < 10 ? 0x0D7114C9 : 0x79FBB0C5;
  else if (IS("GET_ENTITY_HEALTH") || IS("GET_ENTITY_MAX_HEALTH")) g_ret[0] = 200;
  else if (IS("GET_GAME_TIMER") || IS("GET_NETWORK_TIME")) g_ret[0] = (uint64_t)(t * 1000);
  else if (IS("GET_CLOCK_YEAR")) g_ret[0] = 2013;
  else if (IS("GET_CLOCK_MONTH")) g_ret[0] = 8;
  else if (IS("GET_CLOCK_DAY_OF_MONTH")) g_ret[0] = 17;
  else if (IS("GET_CLOCK_HOURS")) g_ret[0] = 13;
  else if (IS("GET_CLOCK_DAY_OF_WEEK")) g_ret[0] = 6;
  else if (IS("GET_FRAME_TIME")) g_ret[0] = f2u(0.025f);
  else if (IS("GET_FRAME_COUNT")) g_ret[0] = atomic_load(&g_frame);
  else if (IS("GET_PLAYER_WANTED_LEVEL")) g_ret[0] = (uint64_t)g_wanted;
  else if (IS("IS_PLAYER_PLAYING") || IS("IS_PED_HUMAN") || IS("IS_ENTITY_VISIBLE") || IS("IS_ENTITY_ON_SCREEN") ||
           IS("HAS_SCALEFORM_MOVIE_LOADED") || IS("HAS_STREAMED_TEXTURE_DICT_LOADED") || IS("HAS_MODEL_LOADED") ||
           IS("IS_MODEL_VALID") || IS("IS_MODEL_IN_CDIMAGE") || IS("HAS_ANIM_DICT_LOADED") || IS("HAS_THIS_ADDITIONAL_TEXT_LOADED") ||
           IS("IS_SCREEN_FADED_IN") || IS("HAS_COLLISION_LOADED_AROUND_ENTITY") || IS("IS_PED_ON_FOOT"))
    g_ret[0] = 1;
  else if (IS("REQUEST_SCALEFORM_MOVIE") || IS("REQUEST_SCALEFORM_MOVIE_INSTANCE")) g_ret[0] = 5;
  else if (IS("GET_PED_TYPE")) g_ret[0] = (int)a0 < 6 && (int)a0 > 1 ? 6 : 4;  // a few cops
  else if (IS("GET_ACTUAL_SCREEN_RESOLUTION") || IS("GET_SCREEN_RESOLUTION")) {
    if (g_n > 1 && g_args[0] && g_args[1]) { *(int *)(uintptr_t)g_args[0] = 1280; *(int *)(uintptr_t)g_args[1] = 720; }
  } else if (IS("GET_ASPECT_RATIO")) g_ret[0] = f2u(16.0f / 9.0f);
  else if (IS("GET_SAFE_ZONE_SIZE")) g_ret[0] = f2u(1.0f);
  else if (IS("GET_LABEL_TEXT") || IS("_GET_LABEL_TEXT") || IS("GET_FILENAME_FOR_AUDIO_CONVERSATION") ||
           IS("GET_STREET_NAME_FROM_HASH_KEY") || IS("GET_NAME_OF_ZONE") || IS("GET_DISPLAY_NAME_FROM_VEHICLE_MODEL") ||
           IS("GET_PLAYER_NAME") || IS("GET_LABEL_TEXT_FROM_HASH"))
    g_ret[0] = (uint64_t)(uintptr_t)g_label;
  else if (IS("GET_HASH_KEY")) {
    const char *str = (const char *)(uintptr_t)a0; uint32_t h = 0;
    for (; str && *str; str++) { h += (uint32_t)(*str | 0x20 * (*str >= 'A' && *str <= 'Z')); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15; g_ret[0] = h;
  }
#undef IS
  return g_ret;
}

// ---- the rest of the ScriptHook API ----
static int g_tex;
API int createTexture(const char *f) { (void)f; return ++g_tex; }
API void drawTexture(int id, int index, int level, int time, float sx, float sy, float cx, float cy, float px, float py,
                     float rot, float sf, float r, float g, float b, float a) {
  (void)id; (void)index; (void)level; (void)time; (void)sx; (void)sy; (void)cx; (void)cy; (void)px; (void)py; (void)rot; (void)sf;
  (void)r; (void)g; (void)b; (void)a;
}
API int getGameVersion(void) { return 77; }
static uint64_t g_globals[1 << 20];
API uint64_t *getGlobalPtr(int id) { return &g_globals[(unsigned)id & ((1 << 20) - 1)]; }
static uint8_t g_entMem[64][0x2000];
API uint8_t *getScriptHandleBaseAddress(int h) { return h >= 1 && h < 64 ? g_entMem[h] : NULL; }
static int fill(int *arr, int size, int from, int to) { int n = 0; for (int h = from; h <= to && n < size; h++) arr[n++] = h; return n; }
API int worldGetAllPeds(int *arr, int size) { return fill(arr, size, 1, 8); }
API int worldGetAllVehicles(int *arr, int size) { return fill(arr, size, 10, 14); }
API int worldGetAllObjects(int *arr, int size) { return fill(arr, size, 20, 22); }
API int worldGetAllPickups(int *arr, int size) { (void)arr; (void)size; return 0; }

__attribute__((constructor)) static void init_clock(void) { t0 = now_s(); }
