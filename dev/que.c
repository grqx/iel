#include <stdio.h>  // fprintf, stderr
#include <stdlib.h>
#include <stdint.h>

#include <iel_priv/quep.h>

static inline
void *tmalloc(size_t sz) {
    fprintf(stderr, "malloc(%zu) ", sz);
    void *p = malloc(sz);
    fprintf(stderr, "= %p\n", p);
    return p;
}
static inline
void tfree(void *p) {
    fprintf(stderr, "free(%p) ", p);
    free(p);
    fprintf(stderr, "= void\n");
}
static inline
void *tcalloc(size_t x, size_t y) {
    fprintf(stderr, "calloc(%zu, %zu) ", x, y);
    void *p = calloc(x, y);
    fprintf(stderr, "= %p\n", p);
    return p;
}
static inline
void *trealloc(void *p, size_t x) {
    fprintf(stderr, "realloc(%p, %zu) ", p, x);
    void *p1 = realloc(p, x);
    fprintf(stderr, "= %p\n", p1);
    return p1;
}
static inline
unsigned long long tloadull(void *p) {
    fprintf(stderr, "loadhq(%p) ", p);
    unsigned long long res = *(unsigned long long *)p;
    fprintf(stderr, "= %#018llx\n", res);
    return res;
}
static inline
void tstoreull(void *p, unsigned long long ull) {
    fprintf(stderr, "storehq(%p, %#018llx) ", p, ull);
    *(unsigned long long *)p = ull;
    fprintf(stderr, "= void\n");
}
#define malloc tmalloc
#define free tfree
#define calloc tcalloc
#define realloc trealloc

static inline
void pque(struct iel_que_st *que) {
    printf("(struct iel_que_st) { .map=%p, .mapcap=%zu, .chunk_s=%zu, .os_s=%hu, .chunk_e=%zu, .os_e=%hu }\nmap: [", que->map, que->mapcap, que->chunk_s, que->os_s, que->chunk_e, que->os_e);
    for (iel_que_sz i = 0; i < que->mapcap; ++i) {
        printf("%p; ", (void *)(((void ***)que->map)[i]));
    }
    puts("]");
}

int main(void) {
    struct iel_que_st que;
    int res;
    void **wval;
    res = iel_quep_init(&que, 0);
    if (res < 0) return 1;
    pque(&que);
    iel_quep_trim(&que);
    puts("trimmed0");
    pque(&que);
    for (int j = 0; j < 3; ++j) {
        printf("sz: %zu\n", iel_quep_size(&que));
        for (size_t i = 0; i < 33; ++i) {
            wval = iel_quep_rsv1(&que);
            if (!wval) return 2;
            *wval = (void *)(uintptr_t)i;
        }
        pque(&que);
        printf("sz: %zu\n", iel_quep_size(&que));
        for (size_t i = 0; i < 32; ++i) {
            void *vout;
            res = iel_quep_pop1(&que, &vout);
            if (res < 0) return 3;
            printf("pop: %p; sz: %zu\n", vout, iel_quep_size(&que));
        }
        pque(&que);
        iel_quep_trim(&que);
        puts("trimmed1");
        pque(&que);
    }
    while (1) {
        void *vout;
        res = iel_quep_pop1(&que, &vout);
        if (res < 0) break;
        printf("pop: %p; sz: %zu\n", vout, iel_quep_size(&que));
    }

    wval = iel_quep_rsv1(&que);
    if (!wval) return 4;
    *wval = (void *)(uintptr_t)42;

    for (size_t i = 0; i < 1024; ++i) {
        wval = iel_quep_rsv1(&que);
        if (!wval) return 6;
        *wval = (void *)(uintptr_t) i;
    }
    {
        void *p[1030];
        size_t out = iel_quep_pop_to(&que, p, 1030);
        printf("pop out: %zu\n", out);
        for (size_t i = 0; i < out; ++i)
            printf("%p ", p[i]);
        putchar('\n');
        iel_quep_trim(&que);
    }
    for (size_t i = 256; i; --i) {
        wval = iel_quep_rsv1(&que);
        if (!wval) return 7;
        *wval = (void *)(uintptr_t) i;
    }
    {
        void *p[256];
        size_t out = iel_quep_pop_to(&que, p, 256);
        printf("pop out: %zu\n", out);
        for (size_t i = 0; i < out; ++i)
            printf("%p ", p[i]);
        putchar('\n');
    }
    pque(&que);
    iel_quep_trim(&que);
    puts("out");
    pque(&que);
    iel_quep_del(&que);
    return 0;
}
