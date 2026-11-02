// Windows paths for .NET mods. Mods written for PC open files like "scripts\\BetterChasesConfig.xml": on Windows the
// backslash is a folder separator, on iOS it's just a character, so those mods made oddly named files next to the
// scripts folder and never read the settings shipped with them. .NET does its file work through
// libSystem.Native.dylib; this points that library's file functions (open, stat, mkdir, rename, opendir...) at small
// wrappers that turn backslashes into slashes first. Nothing else in the game is touched.
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

void fx_log(const char *fmt, ...);

static _Atomic int g_logged;
static const char *fixp(const char *p, char *buf) {
  if (!p || !strchr(p, '\\')) return p;
  size_t n = 0; int prev = 0;
  for (const char *s = p; *s && n < PATH_MAX - 1; s++) {
    int sep = *s == '\\' || *s == '/';
    if (sep && prev) continue;  // "scripts\\x" -> "scripts/x"
    buf[n++] = sep ? '/' : *s; prev = sep;
  }
  buf[n] = 0;
  if (atomic_fetch_add(&g_logged, 1) < 5) fx_log("[net] Windows path from a mod: \"%s\" -> \"%s\"", p, buf);
  return buf;
}
#define B char b1[PATH_MAX]
static int w_open(const char *p, int fl, ...) {
  B; int m = 0;
  if (fl & O_CREAT) { va_list ap; va_start(ap, fl); m = va_arg(ap, int); va_end(ap); }
  return open(fixp(p, b1), fl, m);
}
static int w_stat(const char *p, struct stat *s) { B; return stat(fixp(p, b1), s); }
static int w_lstat(const char *p, struct stat *s) { B; return lstat(fixp(p, b1), s); }
static int w_access(const char *p, int m) { B; return access(fixp(p, b1), m); }
static int w_mkdir(const char *p, mode_t m) { B; return mkdir(fixp(p, b1), m); }
static int w_rmdir(const char *p) { B; return rmdir(fixp(p, b1)); }
static int w_unlink(const char *p) { B; return unlink(fixp(p, b1)); }
static int w_rename(const char *a, const char *b) { B; char b2[PATH_MAX]; return rename(fixp(a, b1), fixp(b, b2)); }
static int w_link(const char *a, const char *b) { B; char b2[PATH_MAX]; return link(fixp(a, b1), fixp(b, b2)); }
static int w_symlink(const char *a, const char *b) { B; char b2[PATH_MAX]; return symlink(fixp(a, b1), fixp(b, b2)); }
static DIR *w_opendir(const char *p) { B; return opendir(fixp(p, b1)); }
static char *w_realpath(const char *p, char *r) { B; return realpath(fixp(p, b1), r); }
static int w_chdir(const char *p) { B; return chdir(fixp(p, b1)); }
static int w_chmod(const char *p, mode_t m) { B; return chmod(fixp(p, b1), m); }
static int w_lchflags(const char *p, unsigned int f) { B; return lchflags(fixp(p, b1), f); }
static int w_mknod(const char *p, mode_t m, dev_t d) { B; return mknod(fixp(p, b1), m, d); }
static long w_pathconf(const char *p, int n) { B; return pathconf(fixp(p, b1), n); }
static ssize_t w_readlink(const char *p, char *o, size_t n) { B; return readlink(fixp(p, b1), o, n); }
static int w_statfs(const char *p, struct statfs *s) { B; return statfs(fixp(p, b1), s); }
static int w_utimensat(int fd, const char *p, const struct timespec t[2], int f) { B; return utimensat(fd, fixp(p, b1), t, f); }

static const struct { const char *name; void *fn; } HOOKS[] = {
  {"_open", w_open}, {"_stat", w_stat}, {"_lstat", w_lstat}, {"_access", w_access}, {"_mkdir", w_mkdir}, {"_rmdir", w_rmdir},
  {"_unlink", w_unlink}, {"_rename", w_rename}, {"_link", w_link}, {"_symlink", w_symlink}, {"_opendir", w_opendir},
  {"_realpath$DARWIN_EXTSN", w_realpath}, {"_chdir", w_chdir}, {"_chmod", w_chmod}, {"_lchflags", w_lchflags},
  {"_mknod", w_mknod}, {"_pathconf", w_pathconf}, {"_readlink", w_readlink}, {"_statfs", w_statfs}, {"_utimensat", w_utimensat},
};

// rebind one image's imported symbols (its __got / __la_symbol_ptr entries), like fishhook
static int rebind_image(const struct mach_header_64 *mh, intptr_t slide) {
  const struct symtab_command *st = NULL; const struct dysymtab_command *dy = NULL;
  const struct segment_command_64 *linkedit = NULL;
  const uint8_t *p = (const uint8_t *)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; i++) {
    const struct load_command *lc = (const struct load_command *)p;
    if (lc->cmd == LC_SYMTAB) st = (const struct symtab_command *)lc;
    else if (lc->cmd == LC_DYSYMTAB) dy = (const struct dysymtab_command *)lc;
    else if (lc->cmd == LC_SEGMENT_64 && !strcmp(((const struct segment_command_64 *)lc)->segname, SEG_LINKEDIT))
      linkedit = (const struct segment_command_64 *)lc;
    p += lc->cmdsize;
  }
  if (!st || !dy || !linkedit || !dy->nindirectsyms) return 0;
  uintptr_t base = (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
  const struct nlist_64 *syms = (const struct nlist_64 *)(base + st->symoff);
  const char *strs = (const char *)(base + st->stroff);
  const uint32_t *ind = (const uint32_t *)(base + dy->indirectsymoff);
  int done = 0;
  p = (const uint8_t *)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; i++) {
    const struct load_command *lc = (const struct load_command *)p; p += lc->cmdsize;
    if (lc->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
    if (strcmp(sg->segname, "__DATA") && strcmp(sg->segname, "__DATA_CONST") && strcmp(sg->segname, "__AUTH_CONST")) continue;
    const struct section_64 *sec = (const struct section_64 *)(sg + 1);
    for (uint32_t j = 0; j < sg->nsects; j++, sec++) {
      uint32_t type = sec->flags & SECTION_TYPE;
      if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS) continue;
      void **ptrs = (void **)((uintptr_t)slide + sec->addr);
      size_t n = sec->size / sizeof(void *);
      for (size_t k = 0; k < n; k++) {
        uint32_t si = ind[sec->reserved1 + k];
        if (si == INDIRECT_SYMBOL_ABS || si == INDIRECT_SYMBOL_LOCAL || si == (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS)) continue;
        const char *name = strs + syms[si].n_un.n_strx;
        for (size_t h = 0; h < sizeof HOOKS / sizeof HOOKS[0]; h++) {
          if (strcmp(name, HOOKS[h].name)) continue;
          vm_address_t pg = (vm_address_t)&ptrs[k] & ~(vm_address_t)(vm_page_size - 1);
          if (vm_protect(mach_task_self(), pg, vm_page_size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS) {
            ptrs[k] = HOOKS[h].fn; done++;
          }
          break;
        }
      }
    }
  }
  return done;
}

// called after .NET starts (System.Native is loaded by then)
void pathfix_install(const char *frameworks) {
  char lib[PATH_MAX]; snprintf(lib, sizeof lib, "%s/libSystem.Native.dylib", frameworks);
  dlopen(lib, RTLD_NOW);
  int total = 0, found = 0;
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const char *n = _dyld_get_image_name(i);
    if (!n || !strstr(n, "libSystem.Native.dylib")) continue;
    found = 1;
    total += rebind_image((const struct mach_header_64 *)_dyld_get_image_header(i), _dyld_get_image_vmaddr_slide(i));
  }
  fx_log("[net] Windows-style paths for mods: %s (%d file functions)", found ? (total ? "on" : "no imports found") : "library not loaded", total);
}
