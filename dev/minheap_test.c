#define _POSIX_C_SOURCE 199309L
#include <time.h>

#define NS_HZ 1000000000LL  // 1e9
#define US_HZ 1000000LL  // 1e6
#define MS_HZ 1000LL  // 1e3

/* precondition: t.tv_nsec >= 0 && t.tv_sec >= 0 && \
 *     ULLONG_MAX / t.tv_sec < US_HZ
 */
static inline
unsigned long long ts2us(struct timespec t) {
    return (t.tv_sec * US_HZ) + (t.tv_nsec / 1000);
}
static inline
void us2ts(struct timespec *t, unsigned long long us) {
    t->tv_sec = us / US_HZ;
    t->tv_nsec = (us % US_HZ) * 1000;
}

#include <iel/minheap.h>

#include <stdio.h>
int main(void) {
    struct iel_mh_st mh;
    union iel_arg_un arg_42 = { .ull = 42 };
    union iel_arg_un arg_37 = { .ull = 37 };
    union iel_arg_un vout;
    iel_mh_elem kout;
    iel_mh_init(&mh, 4);

    {
        struct timespec ts;
        unsigned long long cur;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        cur = ts2us(ts);
        iel_mh_ins1(&mh, cur, arg_42);
        iel_mh_ins1(&mh, 0ULL, arg_42);
        iel_mh_ins1(&mh, cur + 30000, arg_37);
        iel_mh_ins1(&mh, cur - 20000, arg_37);
        fprintf(stderr, "ins entries: %u\n", mh.entries);

        iel_mh_pop(&mh, &kout, &vout);
        fprintf(stderr, "pop k=%llu; v=%llu\n", kout, vout.ull);
        iel_mh_pop(&mh, &kout, &vout);
        fprintf(stderr, "pop k=%llu; v=%llu\n", kout, vout.ull);
        iel_mh_pop(&mh, &kout, &vout);
        fprintf(stderr, "pop k=%llu; v=%llu\n", kout, vout.ull);
        iel_mh_pop(&mh, &kout, &vout);
        fprintf(stderr, "pop k=%llu; v=%llu\n", kout, vout.ull);
    }

    iel_mh_del(&mh);
    return 0;
}
