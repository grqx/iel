#include <iel/tagptr.h>
#include <iel/backends.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>
#include <inttypes.h>

int main(void) {
    iel_tp_init();
    void *p = malloc(sizeof(struct iel_cb_raw_st));
    if (!p) return 1;
    ((struct iel_cb_raw_st *)p)->base = NULL;  // make the compiler happy
    uintmax_t tag = 0;
    uintmax_t maxtag = iel_tp_max(IEL_CB_ALIGN);
    while (tag != maxtag) {
        void *pt = iel_tp_tag(p, IEL_CB_ALIGN, tag);
        struct iel_tp_untag_st ut = iel_tp_untag(pt , alignof(max_align_t));
        if (ut.ptr != p || ut.tag != tag) {
            fprintf(
                stderr,
                "err: al %zu, expected {%p, %" PRIuMAX "}, "
                "got {%p, %" PRIuMAX "}\n",
                alignof(max_align_t),
                p, tag, ut.ptr, ut.tag);
            abort();
        }
        ++tag;
    }
    printf("RealMaxTag is %" PRIuMAX " for alignment %zu(IEL_CB_ALIGN)\n", tag - 1, IEL_CB_ALIGN);
    free(p);
}
