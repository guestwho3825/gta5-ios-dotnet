// ScriptHook 2: runs ScriptHookV .NET (and the .NET mods it loads) on the iOS port.
//
// Threads, as on PC (where ScriptHookV .NET's loader does the same):
//  - the .NET thread runs ScriptHookV .NET: its init, then one Tick per game frame. Mods' own script threads
//    (ScriptHookV .NET makes one per script) take turns inside that tick.
//  - the game's script thread runs a small native script that, each frame, lets the .NET side tick and waits for it.
//    While it waits it performs the game calls (natives) the .NET side asks for, through a one-slot mailbox. So every
//    native still runs on the game's script thread, inside a script, exactly like natives from C plugins: no thread
//    context swapping, nothing touching the game from another thread.
//  - .NET code never runs on the ScriptHook's fiber stacks (Mono can't scan those).
//
// ScriptHookV .NET calls ScriptHookV through 7 functions with Windows C++ names; shvdn_exports.S gives this
// library those exact names, so its code is unchanged there.
#include <mach/mach.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void fx_log(const char *fmt, ...);
void nativeInit(uint64_t hash);
void nativePush64(uint64_t v);
uint64_t *nativeCall(void);
void scriptWait(uint32_t ms);
void scriptRegister(void *module, void (*fn)(void));
typedef void (*KeyboardHandler)(uint32_t, uint16_t, uint8_t, int, int, int, int);
void keyboardHandlerRegister(KeyboardHandler h);

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static inline void cpu_relax(void) { __asm__ volatile("yield"); }

// ---------- mailbox: game calls from .NET threads, performed on the game's script thread ----------
#define WAIT_HASH 0x4EDE34FBADD967A6ULL  // SYSTEM::WAIT: ScriptHookV .NET waits by itself; never pause the server
enum { BOX_EMPTY, BOX_REQUEST, BOX_DONE };
static struct {
  _Atomic int state;
  int kind;                       // 1: a native call, 2: an entity list
  uint64_t hash, args[32], ret[4];
  int n;
  int which, size, count, *arr;   // entity list: 0 peds, 1 vehicles, 2 objects, 3 pickups
  int hasPos, nModels; float pos[3], radius; const int *models;  // optional filters, applied on the game side
} g_box;
// numbers for the status line and the freeze report (stall.c)
_Atomic uint64_t g_netServed, g_netBeats, g_netTicksDone;
_Atomic double g_netTickStart;  // when the running .NET tick began (ms), 0 if none
_Atomic uint32_t g_netTid;      // the .NET thread's id
_Atomic uint64_t g_boxHash;     // last request on the mailbox (0x2 + which for a list)
_Atomic uint64_t g_serveNow;     // what the game thread is doing for .NET right now (0: nothing)
uint64_t g_recent[32];          // the last 32 requests, for the last-moments file
_Atomic unsigned g_recentN;
int worldGetAllPeds(int *arr, int size);
int worldGetAllVehicles(int *arr, int size);
int worldGetAllObjects(int *arr, int size);
int worldGetAllPickups(int *arr, int size);
static pthread_mutex_t g_boxLock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_serving;  // the game thread is inside its wait loop and will answer

static __thread uint64_t t_hash, t_args[32], t_ret[4];

// ---------- touch pad for mod menus (NativeUI, LemonUI...) ----------
// Those menus move with the game's controls (phone/frontend up, down, accept, back...), which touch can't press.
// The on-screen pad sets "virtual" controls here; when a .NET mod asks the game whether one of those controls is
// pressed, the answer comes from the pad (the game itself is unaffected). A mod asking about up/down tells us a menu
// is open, so the pad is shown only then.
enum { VB_UP, VB_DOWN, VB_LEFT, VB_RIGHT, VB_OK, VB_BACK, VB_N };
static int vb_of(int ctl) {
  switch (ctl) {
    case 172: case 188: case 27: return VB_UP;      // INPUT_CELLPHONE_UP, INPUT_FRONTEND_UP, INPUT_PHONE
    case 173: case 187: return VB_DOWN;
    case 174: case 189: return VB_LEFT;
    case 175: case 190: return VB_RIGHT;
    case 176: case 201: case 191: case 18: return VB_OK;   // CELLPHONE_SELECT, FRONTEND_ACCEPT, FRONTEND_RDOWN, ENTER
    case 177: case 202: case 194: return VB_BACK;           // CELLPHONE_CANCEL, FRONTEND_CANCEL, FRONTEND_RRIGHT
  }
  return -1;
}
static _Atomic int g_vbHeld[VB_N], g_vbPend[VB_N], g_vbRelPend[VB_N];
static volatile int g_vbJust[VB_N], g_vbRel[VB_N];   // for the current .NET tick
static _Atomic double g_menuSeen;
void net_vpad(int b, int down) {
  if (b < 0 || b >= VB_N) return;
  if (down) { atomic_store(&g_vbHeld[b], 1); atomic_store(&g_vbPend[b], 1); }
  else { atomic_store(&g_vbHeld[b], 0); atomic_store(&g_vbRelPend[b], 1); }
}
static double vb_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int net_menu_active(void) { return vb_now() - atomic_load(&g_menuSeen) < 800; }
static void vb_new_tick(void) {
  for (int b = 0; b < VB_N; b++) { g_vbJust[b] = atomic_exchange(&g_vbPend[b], 0); g_vbRel[b] = atomic_exchange(&g_vbRelPend[b], 0); }
}
// 1 pressed, 2 just pressed, 3 just released
static int ctl_kind(uint64_t h) {
  switch (h) {
    case 0xF3A21BCD95725A4AULL: case 0xE2587F8CBBD87B1DULL: return 1;
    case 0x580417101DDB492FULL: case 0x91AEF906BCA88877ULL: return 2;
    case 0x50F940259D3841E6ULL: case 0x305C8DCD79DA8B0FULL: return 3;
  }
  return 0;
}
static __thread int t_n;

void shv_nativeInit(uint64_t hash) { t_hash = hash; t_n = 0; }
void shv_nativePush64(uint64_t v) { if (t_n < 32) t_args[t_n++] = v; }
uint64_t *shv_nativeCall(void) {
  memset(t_ret, 0, sizeof t_ret);
  if (t_hash == WAIT_HASH) return t_ret;
  int ck = ctl_kind(t_hash);
  if (ck && t_n >= 2) {
    int ctl = (int)t_args[1], b = vb_of(ctl);
    if (b >= 0) {
      if (ctl == 172 || ctl == 173 || ctl == 187 || ctl == 188) atomic_store(&g_menuSeen, vb_now());
      int v = ck == 1 ? atomic_load(&g_vbHeld[b]) : ck == 2 ? g_vbJust[b] : g_vbRel[b];
      if (v) { t_ret[0] = 1; return t_ret; }
    }
  }
  pthread_mutex_lock(&g_boxLock);
  g_box.kind = 1; g_box.hash = t_hash; g_box.n = t_n;
  memcpy(g_box.args, t_args, sizeof(uint64_t) * (size_t)t_n);
  atomic_store_explicit(&g_box.state, BOX_REQUEST, memory_order_release);
  for (unsigned spins = 0; atomic_load_explicit(&g_box.state, memory_order_acquire) != BOX_DONE; spins++) {
    if (spins < 4000) cpu_relax(); else if (spins < 4100) sched_yield(); else usleep(50);
  }
  memcpy(t_ret, g_box.ret, sizeof t_ret);
  atomic_store_explicit(&g_box.state, BOX_EMPTY, memory_order_release);
  pthread_mutex_unlock(&g_boxLock);
  return t_ret;
}
// Entity lists for ScriptHookV .NET (World.GetAllPeds etc.), made on the game thread like the natives: script IDs
// are created there, with the game's own allocator
// With a position (3 floats) and/or model list, only matching entities are returned; the checks run on the game
// thread too, so a whole GetNearbyPeds is one trip instead of one per entity.
__attribute__((visibility("default"))) int shv_worldGetFiltered(int which, int *arr, int size, const float *pos,
                                                               float radius, const int *models, int nModels) {
  if (!arr || size <= 0) return 0;
  pthread_mutex_lock(&g_boxLock);
  g_box.kind = 2; g_box.which = which; g_box.arr = arr; g_box.size = size; g_box.count = 0;
  g_box.hasPos = pos != NULL; if (pos) { g_box.pos[0] = pos[0]; g_box.pos[1] = pos[1]; g_box.pos[2] = pos[2]; }
  g_box.radius = radius; g_box.models = models; g_box.nModels = models ? nModels : 0;
  atomic_store_explicit(&g_box.state, BOX_REQUEST, memory_order_release);
  for (unsigned spins = 0; atomic_load_explicit(&g_box.state, memory_order_acquire) != BOX_DONE; spins++) {
    if (spins < 4000) cpu_relax(); else if (spins < 4100) sched_yield(); else usleep(50);
  }
  int n = g_box.count;
  atomic_store_explicit(&g_box.state, BOX_EMPTY, memory_order_release);
  pthread_mutex_unlock(&g_boxLock);
  return n;
}
__attribute__((visibility("default"))) int shv_worldGetAll(int which, int *arr, int size) {
  return shv_worldGetFiltered(which, arr, size, NULL, 0, NULL, 0);
}

#define GET_ENTITY_MODEL 0x9F47B058362C84B5ULL
#define GET_ENTITY_COORDS 0x3FEF770D40960D5AULL
static int keep_entity(int h) {
  if (g_box.nModels > 0) {
    nativeInit(GET_ENTITY_MODEL); nativePush64((uint32_t)h);
    uint64_t *r = nativeCall(); int model = r ? (int)r[0] : 0, ok = 0;
    for (int i = 0; i < g_box.nModels; i++) if (g_box.models[i] == model) { ok = 1; break; }
    if (!ok) return 0;
  }
  if (g_box.hasPos) {
    nativeInit(GET_ENTITY_COORDS); nativePush64((uint32_t)h); nativePush64(1);
    uint64_t *r = nativeCall(); if (!r) return 0;
    float x = *(float *)&r[0], y = *(float *)&r[1], z = *(float *)&r[2];
    float dx = x - g_box.pos[0], dy = y - g_box.pos[1], dz = z - g_box.pos[2];
    if (dx * dx + dy * dy + dz * dz > g_box.radius * g_box.radius) return 0;
  }
  return 1;
}

// game thread: answer one request if there is one
static int serve_one(void) {
  if (atomic_load_explicit(&g_box.state, memory_order_acquire) != BOX_REQUEST) return 0;
  atomic_store_explicit(&g_boxHash, g_box.kind == 2 ? 0x20 + (uint64_t)g_box.which : g_box.hash, memory_order_relaxed);
  { uint64_t hsh = g_box.kind == 2 ? 0x20 + (uint64_t)g_box.which : g_box.hash;
    unsigned i = atomic_fetch_add_explicit(&g_recentN, 1, memory_order_relaxed); g_recent[i % 32] = hsh; }
  atomic_store(&g_serveNow, g_box.kind == 2 ? 0x20 + (uint64_t)g_box.which : g_box.hash);
  if (g_box.kind == 2) {
    int n = 0;
    switch (g_box.which) {
      case 0: n = worldGetAllPeds(g_box.arr, g_box.size); break;
      case 1: n = worldGetAllVehicles(g_box.arr, g_box.size); break;
      case 2: n = worldGetAllObjects(g_box.arr, g_box.size); break;
      case 3: n = worldGetAllPickups(g_box.arr, g_box.size); break;
    }
    if (g_box.hasPos || g_box.nModels > 0) {
      int k = 0;
      for (int i = 0; i < n; i++) if (keep_entity(g_box.arr[i])) g_box.arr[k++] = g_box.arr[i];
      n = k;
    }
    g_box.count = n;
    atomic_store(&g_serveNow, 0);
    atomic_fetch_add_explicit(&g_netServed, 1, memory_order_relaxed);
    atomic_store_explicit(&g_box.state, BOX_DONE, memory_order_release);
    return 1;
  }
  nativeInit(g_box.hash);
  for (int i = 0; i < g_box.n; i++) nativePush64(g_box.args[i]);
  uint64_t *r = nativeCall();
  if (r) memcpy(g_box.ret, r, sizeof g_box.ret); else memset(g_box.ret, 0, sizeof g_box.ret);
  atomic_store(&g_serveNow, 0);
  atomic_fetch_add_explicit(&g_netServed, 1, memory_order_relaxed);
  atomic_store_explicit(&g_box.state, BOX_DONE, memory_order_release);
  return 1;
}

// the other ScriptHookV functions ScriptHookV .NET imports, by their Windows names (see shvdn_exports.S)
int createTexture(const char *file);
void drawTexture(int id, int index, int level, int time, float sizeX, float sizeY, float centerX, float centerY,
                 float posX, float posY, float rotation, float scaleFactor, float r, float g, float b, float a);
int getGameVersion(void);
uint64_t *getGlobalPtr(int id);
int shv_createTexture(const char *f) { return createTexture(f); }
void shv_drawTexture(int id, int index, int level, int time, float sx, float sy, float cx, float cy, float px, float py,
                     float rot, float sf, float r, float g, float b, float a) {
  drawTexture(id, index, level, time, sx, sy, cx, cy, px, py, rot, sf, r, g, b, a);
}
int shv_getGameVersion(void) { return getGameVersion(); }
uint64_t *shv_getGlobalPtr(int id) { return getGlobalPtr(id); }

// Windows functions it imports (kernel32 / user32)
__attribute__((visibility("default"))) uint32_t GetCurrentThreadId(void) { return (uint32_t)pthread_mach_thread_np(pthread_self()); }
__attribute__((visibility("default"))) int IsDebuggerPresent(void) { return 0; }
__attribute__((visibility("default"))) int ToUnicode(uint32_t vk, uint32_t scan, const uint8_t *state, uint16_t *buf, int n, uint32_t f) {
  (void)vk; (void)scan; (void)state; (void)buf; (void)n; (void)f; return 0;
}
// "game thread context" hooks ScriptHookV .NET calls around tasks; natives already run on the game thread here
void *shv_get_tls(void) { return NULL; }
void shv_set_tls(void *ctx) { (void)ctx; }

void shv_breadcrumb(const char *s);
// ---------- per-frame handshake ----------
enum { PH_OFF, PH_INIT, PH_IDLE, PH_TICK };
static _Atomic int g_phase = PH_OFF;
static pthread_mutex_t g_phLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_phCond = PTHREAD_COND_INITIALIZER;
static _Atomic uint32_t g_gameTid;
static _Atomic int g_frameOverruns;

static void set_phase(int p) {
  pthread_mutex_lock(&g_phLock);
  atomic_store(&g_phase, p);
  pthread_cond_broadcast(&g_phCond);
  pthread_mutex_unlock(&g_phLock);
}

// The native script (runs on the game's script thread, in the ScriptHook's script loop)
static void net_script(void) {
  atomic_store(&g_gameTid, GetCurrentThreadId());
  for (;;) {
    atomic_fetch_add_explicit(&g_netBeats, 1, memory_order_relaxed);
    int ph = atomic_load(&g_phase);
    if (ph == PH_IDLE) { set_phase(PH_TICK); ph = PH_TICK; }
    if (ph == PH_INIT || ph == PH_TICK) {
      // serve the .NET side until it finishes this frame's tick (or init), within a budget so the game never freezes;
      // anything left continues next frame
      double end = now_ms() + (ph == PH_INIT ? 12.0 : 40.0);
      atomic_store(&g_serving, 1);
      unsigned idle = 0;
      while (now_ms() < end) {
        if (serve_one()) { idle = 0; continue; }
        ph = atomic_load(&g_phase);
        if (ph != PH_INIT && ph != PH_TICK) break;
        if (++idle < 2000) cpu_relax(); else sched_yield();
      }
      atomic_store(&g_serving, 0);
      if (atomic_load(&g_phase) == PH_TICK) atomic_fetch_add(&g_frameOverruns, 1);
    }
    scriptWait(0);
  }
}

// keys from the touch/controller UI, delivered to ScriptHookV .NET on its thread before each tick
static struct { uint32_t key; uint8_t down, alt; } g_keys[64];
static _Atomic unsigned g_kHead, g_kTail;
static void net_key(uint32_t key, uint16_t repeats, uint8_t scan, int ext, int alt, int wasDown, int isUp) {
  (void)repeats; (void)scan; (void)ext; (void)wasDown;
  unsigned t = atomic_load(&g_kTail);
  if (t - atomic_load(&g_kHead) >= 64) return;
  g_keys[t % 64].key = key; g_keys[t % 64].down = !isUp; g_keys[t % 64].alt = alt != 0;
  atomic_store(&g_kTail, t + 1);
}

// Registered with the ScriptHook alongside the C plugins (before the game starts its scripts).
void net_register(void) {
  scriptRegister(NULL, net_script);
  keyboardHandlerRegister(net_key);
}

// ---------- the .NET thread: ScriptHookV .NET init, then a tick per frame ----------
typedef struct {
  void *(*invoke)(void *method, void *obj, void **args, void **exc);
  char *(*to_utf8)(void *str);
  void (*free_)(void *);
  void *(*enter_unsafe)(void **);
  void (*exit_unsafe)(void *, void **);
  void *init, *tick, *key;
} net_api;

static void report(const char *what, void *res, const net_api *a) {
  if (!res) return;  // null = fine
  char *t = a->to_utf8(res);
  fx_log("[net] %s failed: %.600s", what, t ? t : "?");
  if (t) a->free_(t);
}

void net_shvdn_run(const net_api *a) {
  // wait until the game-side script is running (it records the game thread's id)
  for (int i = 0; i < 600 && !atomic_load(&g_gameTid); i++) usleep(50000);
  if (!atomic_load(&g_gameTid)) { fx_log("[net] game script never started; ScriptHookV .NET not loaded"); return; }

  atomic_store(&g_netTid, GetCurrentThreadId());
  set_phase(PH_INIT);
  double t0 = now_ms();
  atomic_store(&g_netTickStart, t0);
  long long getTls = (long long)(uintptr_t)shv_get_tls, setTls = (long long)(uintptr_t)shv_set_tls;
  int tid = (int)atomic_load(&g_gameTid);
  void *args[3] = {&getTls, &setTls, &tid};
  void *sd = NULL, *ck = a->enter_unsafe(&sd), *exc = NULL;
  void *res = a->invoke(a->init, NULL, args, &exc);
  report("ScriptHookV .NET start", res, a);
  if (exc) fx_log("[net] ScriptHookV .NET start threw (uncaught)");
  a->exit_unsafe(ck, &sd);
  atomic_store(&g_netTickStart, 0.0);
  fx_log("[net] ScriptHookV .NET started in %.0f ms (its log: Documents/ScriptHookVDotNet.log)", now_ms() - t0);
  set_phase(PH_IDLE);

  unsigned ticks = 0, errors = 0; double busy = 0, lastReport = now_ms();
  for (;;) {
    pthread_mutex_lock(&g_phLock);
    while (atomic_load(&g_phase) != PH_TICK) pthread_cond_wait(&g_phCond, &g_phLock);
    pthread_mutex_unlock(&g_phLock);

    double s = now_ms();
    atomic_store(&g_netTickStart, s);
    if (ticks < 3) shv_breadcrumb("(ScriptHookV .NET tick starting)");
    vb_new_tick();
    ck = a->enter_unsafe(&sd);
    while (atomic_load(&g_kHead) != atomic_load(&g_kTail)) {
      unsigned h = atomic_load(&g_kHead);
      int k = (int)g_keys[h % 64].key, dn = g_keys[h % 64].down, al = g_keys[h % 64].alt, z = 0;
      void *ka[5] = {&k, &dn, &z, &z, &al};
      exc = NULL; report("key event", a->invoke(a->key, NULL, ka, &exc), a);
      atomic_store(&g_kHead, h + 1);
    }
    exc = NULL;
    res = a->invoke(a->tick, NULL, NULL, &exc);
    if (res && errors++ < 20) report("tick", res, a);
    a->exit_unsafe(ck, &sd);
    atomic_store(&g_netTickStart, 0.0);
    atomic_fetch_add_explicit(&g_netTicksDone, 1, memory_order_relaxed);
    busy += now_ms() - s; ticks++;
    set_phase(PH_IDLE);

    if (now_ms() - lastReport > 30000) {
      fx_log("[net] last 30s: %u ticks, %.2f ms per tick, %d frames where scripts ran past the frame budget",
             ticks, ticks ? busy / ticks : 0.0, atomic_exchange(&g_frameOverruns, 0));
      ticks = 0; busy = 0; lastReport = now_ms();
    }
  }
}

// which mod script ScriptHookV .NET is running (set before each script's turn), for the last-moments file
char g_netCrumb[128];
__attribute__((visibility("default"))) void shv_breadcrumb(const char *s) {
  if (!s) { g_netCrumb[0] = 0; return; }
  size_t n = strlen(s); if (n > sizeof g_netCrumb - 1) n = sizeof g_netCrumb - 1;
  memcpy(g_netCrumb, s, n); g_netCrumb[n] = 0;
}

// for the freeze report
uint32_t net_game_tid(void) { return atomic_load(&g_gameTid); }
int net_phase(void) { return atomic_load(&g_phase); }
int net_box_state(void) { return atomic_load(&g_box.state); }
