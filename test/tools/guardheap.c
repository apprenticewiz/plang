// guardheap.c -- a guard-page allocator for plang-generated programs.
//
// plang_new is std::calloc (runtime/plang_sys.cpp).  calloc is resolved
// dynamically even though the runtime is linked statically, so interposing it
// here reaches every heap object a generated program allocates.
//
// Each allocation is placed so its LAST byte abuts a PROT_NONE page.  A write
// one byte past the end of a schema body therefore faults at the instruction
// that makes it, instead of quietly landing in whatever malloc happened to own
// next.  That is the whole point: every schema test's oracle is a printed
// value, and a corrupted NEIGHBOURING field does not change it.
//
//   layout:  [ header page ][ pad ][ user bytes ][ guard page: PROT_NONE ]
//                                  ^ returned                ^ first fault
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GH_MAGIC 0x67756172645f6870ULL   /* "guard_hp" */

struct gh_head { uint64_t magic; size_t user; size_t total; };

static size_t gh_page(void) {
    static size_t p; if (!p) p = (size_t)sysconf(_SC_PAGESIZE); return p;
}
static void *(*real_calloc)(size_t, size_t);
static void  (*real_free)(void *);
static void *(*real_malloc)(size_t);
static void *(*real_realloc)(void *, size_t);

// dlsym itself can allocate, so calls that arrive before the real functions are
// resolved are served from a small static arena rather than recursing.
static char gh_boot[1 << 16];
static size_t gh_boot_used;
static int gh_in_init;

static void gh_init(void) {
    if (real_calloc) return;
    gh_in_init = 1;
    real_calloc  = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
    real_free    = (void  (*)(void *))        dlsym(RTLD_NEXT, "free");
    real_malloc  = (void *(*)(size_t))        dlsym(RTLD_NEXT, "malloc");
    real_realloc = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
    gh_in_init = 0;
}
static int gh_is_boot(void *p) {
    return (char *)p >= gh_boot && (char *)p < gh_boot + sizeof gh_boot;
}
/* Is this page mapped?  msync reports ENOMEM for a range that is not, which is
   the only way to ask without risking the fault we are trying to avoid. */
static int gh_mapped(void *page) {
    return msync(page, gh_page(), MS_ASYNC) == 0;
}

/* The header for a pointer we allocated, or NULL for one we did not.
 *
 * This read h->magic unconditionally, and that IS the fault it was meant to
 * screen out: for a pointer this allocator never handed out -- one from the
 * bootstrap arena's neighbours, a static buffer, or anything the real malloc
 * placed at the start of a fresh mapping -- the page BEFORE it need not be
 * mapped at all, and the magic check dereferenced it to find out.  So free()
 * of a foreign pointer could take the program down, and the memory-safety gate
 * reported SIGSEGV on a program that had done nothing wrong.
 *
 * A false alarm here is worse than no gate: it was used to decide whether real
 * defects were real.
 *
 * The user pointer is right-aligned in the body so its last byte touches the
 * guard page, and the body is a whole number of pages, so p always lies in the
 * FIRST body page and the header is always exactly one page below it. */
static struct gh_head *gh_head_of(void *p) {
    size_t pg = gh_page();
    uintptr_t page = (uintptr_t)p & ~(uintptr_t)(pg - 1);
    void *hp = (void *)(page - pg);
    if (!gh_mapped(hp)) return NULL;
    struct gh_head *h = (struct gh_head *)hp;
    return h->magic == GH_MAGIC ? h : NULL;
}

static void *gh_alloc(size_t n) {
    size_t pg = gh_page();
    if (n == 0) n = 1;
    size_t body  = (n + pg - 1) & ~(pg - 1);
    size_t total = pg + body + pg;                 /* header + body + guard */
    char *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base + pg + body, pg, PROT_NONE) != 0) { munmap(base, total); return NULL; }
    struct gh_head *h = (struct gh_head *)base;
    h->magic = GH_MAGIC; h->user = n; h->total = total;
    /* Right-align the user bytes so the last one touches the guard page. */
    return base + pg + (body - n);
}

void *calloc(size_t nmemb, size_t size) {
    if (gh_in_init || !real_calloc) {
        gh_init();
        if (gh_in_init) {                       /* still bootstrapping */
            size_t n = nmemb * size;
            n = (n + 15) & ~(size_t)15;
            if (gh_boot_used + n > sizeof gh_boot) return NULL;
            void *p = gh_boot + gh_boot_used; gh_boot_used += n;
            memset(p, 0, n); return p;
        }
    }
    size_t n = nmemb * size;
    if (nmemb && n / nmemb != size) return NULL;   /* overflow */
    void *p = gh_alloc(n);
    return p;                                      /* mmap memory is zeroed */
}

void free(void *p) {
    if (!p) return;
    if (gh_is_boot(p)) return;
    gh_init();
    struct gh_head *h = gh_head_of(p);
    if (h) { munmap((void *)h, h->total); return; }
    real_free(p);
}

void *realloc(void *p, size_t n) {
    gh_init();
    if (!p) return real_malloc ? real_malloc(n) : NULL;
    struct gh_head *h = gh_head_of(p);
    if (!h) return real_realloc(p, n);
    void *q = gh_alloc(n);
    if (!q) return NULL;
    memcpy(q, p, h->user < n ? h->user : n);
    munmap((void *)h, h->total);
    return q;
}
