// Test host: starts .NET the way the iPhone build does (Mono, everything interpreted, hybrid thread suspend, 16 MB
// mod thread stacks, the same heap cap and error hook), then runs ScriptHookV .NET with real mods against the fake
// game in libfakegame.dylib for a while. Any crash shows up with a full backtrace (the job runs it under lldb).
//
// usage: host <runtime dir> <our dotnet dir> <documents dir> <libfakegame.dylib> <seconds>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  void *(*invoke)(void *method, void *obj, void **args, void **exc);
  char *(*to_utf8)(void *str);
  void (*free_)(void *);
  void *(*enter_unsafe)(void **);
  void (*exit_unsafe)(void *, void **);
  void *init, *tick, *key;
} net_api;

static const char *g_rt, *g_dotnet, *g_docs, *g_fake;
static void (*fx_log)(const char *fmt, ...);
static void *(*p_enter_safe)(void **);

static void on_unhandled(void *exc, void *ud) {
  (void)exc; (void)ud;
  fx_log("[host] a .NET thread hit an error nobody caught; that thread is stopped (same as on the iPhone)");
  void *sd = NULL; p_enter_safe(&sd);
  for (;;) sleep(3600);
}

static void add_dir(char **tpa, size_t *len, size_t *cap, const char *dir, int *count) {
  DIR *d = opendir(dir); if (!d) return;
  for (struct dirent *e; (e = readdir(d));) {
    size_t nl = strlen(e->d_name);
    if (nl < 5 || strcmp(e->d_name + nl - 4, ".dll")) continue;
    if (strstr(*tpa, e->d_name)) continue;  // first one wins (ours come first)
    size_t need = strlen(dir) + nl + 3;
    while (*len + need >= *cap) { *cap *= 2; *tpa = realloc(*tpa, *cap); }
    *len += snprintf(*tpa + *len, *cap - *len, "%s%s/%s", *count ? ":" : "", dir, e->d_name);
    (*count)++;
  }
  closedir(d);
}

static void *boot(void *arg) {
  (void)arg;
  char lib[2048]; snprintf(lib, sizeof lib, "%s/libmonosgen-2.0.dylib", g_rt);
  void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
  if (!h) { fx_log("[host] runtime not loaded: %s", dlerror()); exit(2); }
#define S(n) void *n = dlsym(h, #n); if (!n) { fx_log("[host] missing %s", #n); exit(2); }
  S(monovm_initialize) S(mono_jit_set_aot_mode) S(mono_jit_init_version) S(mono_domain_assembly_open) S(mono_assembly_get_image)
  S(mono_class_from_name) S(mono_class_get_method_from_name) S(mono_runtime_invoke) S(mono_string_to_utf8) S(mono_free)
  S(mono_threads_enter_gc_unsafe_region) S(mono_threads_exit_gc_unsafe_region) S(mono_threads_enter_gc_safe_region)
  S(mono_install_unhandled_exception_hook) S(mono_threads_set_default_stacksize)
#undef S
  p_enter_safe = mono_threads_enter_gc_safe_region;
  ((void (*)(void (*)(void *, void *), void *))mono_install_unhandled_exception_hook)(on_unhandled, NULL);

  size_t cap = 1 << 16, len = 0; char *tpa = malloc(cap); tpa[0] = 0; int count = 0;
  add_dir(&tpa, &len, &cap, g_dotnet, &count);
  add_dir(&tpa, &len, &cap, g_rt, &count);
  const char *keys[] = {"TRUSTED_PLATFORM_ASSEMBLIES", "APP_PATHS", "NATIVE_DLL_SEARCH_DIRECTORIES",
                        "System.Globalization.Invariant", "System.GC.Concurrent"};
  const char *vals[] = {tpa, g_dotnet, g_rt, "true", "false"};
  fx_log("[host] starting .NET (%d libraries, interpreter only, like the iPhone)", count);
  ((void (*)(int))mono_jit_set_aot_mode)(8);  // MONO_AOT_MODE_INTERP_ONLY
  ((void (*)(size_t))mono_threads_set_default_stacksize)(16u << 20);
  int rc = ((int (*)(int, const char **, const char **))monovm_initialize)(5, keys, vals);
  if (rc) { fx_log("[host] runtime setup failed %d", rc); exit(2); }
  void *domain = ((void *(*)(const char *, const char *))mono_jit_init_version)("harness", "mobile");
  if (!domain) { fx_log("[host] runtime didn't start"); exit(2); }
  fx_log("[host] .NET started");

  char api[2048]; snprintf(api, sizeof api, "%s", g_dotnet);
  setenv("PH_DOCS", g_docs, 1); setenv("PH_APP", g_docs, 1); setenv("PH_SHVDN_API", api, 1); setenv("PH_D3D11", g_fake, 1);
  char bp[2048]; snprintf(bp, sizeof bp, "%s/PostHook.DotNet.dll", g_dotnet);
  void *sd = NULL, *ck = ((void *(*)(void **))mono_threads_enter_gc_unsafe_region)(&sd);
  void *asmb = ((void *(*)(void *, const char *))mono_domain_assembly_open)(domain, bp);
  void *img = asmb ? ((void *(*)(void *))mono_assembly_get_image)(asmb) : NULL;
  void *c = img ? ((void *(*)(void *, const char *, const char *))mono_class_from_name)(img, "PostHook", "Bridge") : NULL;
  void *(*gm)(void *, const char *, int) = (void *(*)(void *, const char *, int))mono_class_get_method_from_name;
  net_api a = {(void *(*)(void *, void *, void **, void **))mono_runtime_invoke, (char *(*)(void *))mono_string_to_utf8,
               (void (*)(void *))mono_free, (void *(*)(void **))mono_threads_enter_gc_unsafe_region,
               (void (*)(void *, void **))mono_threads_exit_gc_unsafe_region,
               c ? gm(c, "Init", 3) : NULL, c ? gm(c, "Tick", 0) : NULL, c ? gm(c, "Key", 5) : NULL};
  ((void (*)(void *, void **))mono_threads_exit_gc_unsafe_region)(ck, &sd);
  if (!a.init || !a.tick || !a.key) { fx_log("[host] bridge not found (%s)", bp); exit(2); }

  void (*run)(const net_api *) = (void (*)(const net_api *))dlsym(RTLD_DEFAULT, "net_shvdn_run");
  fx_log("[host] loading ScriptHookV .NET");
  run(&a);  // never returns
  return NULL;
}

static void *game(void *arg) { (void)arg; ((void (*)(void))dlsym(RTLD_DEFAULT, "fake_game_run"))(); return NULL; }

int main(int argc, char **argv) {
  if (argc < 6) { fprintf(stderr, "usage: host <runtime> <dotnet> <docs> <libfakegame.dylib> <seconds>\n"); return 2; }
  setvbuf(stdout, NULL, _IONBF, 0);
  g_rt = argv[1]; g_dotnet = argv[2]; g_docs = argv[3]; g_fake = argv[4];
  int secs = atoi(argv[5]);
  setenv("MONO_THREADS_SUSPEND", "hybrid", 1);
  setenv("MONO_GC_PARAMS", "max-heap-size=1536m", 1);
  if (!dlopen(g_fake, RTLD_NOW | RTLD_GLOBAL)) { fprintf(stderr, "fake game: %s\n", dlerror()); return 2; }
  fx_log = (void (*)(const char *, ...))dlsym(RTLD_DEFAULT, "fx_log");
  ((void (*)(void))dlsym(RTLD_DEFAULT, "net_register"))();  // the .NET server script, like the ScriptHook does
  pthread_t gt; pthread_create(&gt, NULL, game, NULL);
  pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8 << 20);
  pthread_t bt; pthread_create(&bt, &at, boot, NULL);
  sleep((unsigned)secs);
  ((void (*)(void))dlsym(RTLD_DEFAULT, "fake_print_natives"))();
  fx_log("[host] SURVIVED %d s with the mods running", secs);
  _exit(0);
}
