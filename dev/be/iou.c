#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdio.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <linux/filter.h>  // for BPF struct
#include <malloc.h>  // for memory tracing

#include <iel/backends/iou.h>
#include <iel/arg.h>

static inline
unsigned long long micros()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#define SEC_TO_US(sec) ((sec)*1000000)
#define NS_TO_US(ns)    ((ns)/1000)
    return SEC_TO_US((unsigned long long)ts.tv_sec) + NS_TO_US((unsigned long long)ts.tv_nsec);
}

struct iel_evloop_info {
    unsigned char stop;
    struct iel_vtable_st vt;
    struct ielb_iou_ctx_st *uring;
};

static inline
void *tmalloc(size_t sz) {
    void *p = malloc(sz);
    fprintf(stderr, "malloc(%zu) = %p\n", sz, p);
    return p;
}
static inline
void tfree(void *p) {
    fprintf(stderr, "free(%p)\n", p);
    free(p);
}
static inline
void *tcalloc(size_t x, size_t y) {
    void *p = calloc(x, y);
    fprintf(stderr, "calloc(%zu, %zu) = %p\n", x, y, p);
    return p;
}

struct cbt_onread_comp {
    struct iel_cb_base base;
    struct iel_evloop_info *loop;
    unsigned char *buf;
    unsigned int len;
};
struct cbt_onwrite_comp {
    struct iel_cb_base base;
    int sz;
    struct cbt_onread_comp *par;
};
void onwrite_comp(void *_self, int res) {
    struct cbt_onwrite_comp *cbp = (struct cbt_onwrite_comp *)_self;
    if (res != cbp->sz) {
        if (res < 0)
            fprintf(stderr, "write error: %s\n", strerror(-res));
        else
            fprintf(stderr, "write incomplete: wrote %d/%d bytes\n", res, cbp->sz);
    } else {
        IEL_RESOLVE_CALL(cbp->par->loop->vt, iou, fr)(cbp->par->loop->uring, STDIN_FILENO, cbp->par->buf, cbp->par->len, IEL_ARG_NULL, &cbp->par->base);
    }
    free(cbp);
}

void onread_comp(void *_self, int res) {
    struct cbt_onread_comp *cbp = (struct cbt_onread_comp *)_self;
    if (res > 0) {
        /* Read successful. Write to stdout. */
        struct cbt_onwrite_comp *wcb = (struct cbt_onwrite_comp *)malloc(sizeof(struct cbt_onwrite_comp));
        if (wcb) {
            wcb->base.cb = &onwrite_comp;
            wcb->sz = res;
            wcb->par = cbp;
            IEL_RESOLVE_CALL(cbp->loop->vt, iou, fw)(cbp->loop->uring, STDOUT_FILENO, cbp->buf, res, IEL_ARG_NULL, &wcb->base);
            return;
        }
        else
            fprintf(stderr, "could not malloc\n");
    }
    else if (res < 0)
        /* Error reading file */
        fprintf(stderr, "read error: %s\n", strerror(-res));
    free(cbp->buf);
    cbp->loop->stop = 1;
    free(cbp);
}

void ontimer_comp(void *_self, int res) {
    if (res < 0)
        fprintf(stderr, "timeout res<0, strerror: %s\n", strerror(-res));
    fprintf(stderr, "3s time out, res=%d\n", res);
    free(_self);
}

void taskcb(void *_self, int res) {
    fprintf(stderr, "soon: %d\n", res);
    free(_self);
}

static inline
int amain(struct iel_evloop_info *loop) {
    struct cbt_onread_comp *cbp = (struct cbt_onread_comp *)malloc(sizeof(struct cbt_onread_comp));
    iel_cbp timer_cb;
    unsigned char *buf;

    if (!cbp)
        goto err;
    cbp->base.cb=&onread_comp;
    cbp->loop=loop;
    cbp->len=4096;
    buf = (unsigned char *)calloc(cbp->len, sizeof(unsigned char));
    if (!buf)
        goto err_freecbp;
    cbp->buf = buf;
    IEL_RESOLVE_CALL(cbp->loop->vt, iou, fr)(loop->uring, STDIN_FILENO, cbp->buf, cbp->len, IEL_ARG_NULL, &cbp->base);

    timer_cb = (iel_cbp)malloc(sizeof(struct iel_cb_base));
    if (!timer_cb)
        goto err_postfr;
    timer_cb->cb = &ontimer_comp;
    for (size_t i = 0; i < 8; ++i) {
        iel_cbp task_cb = (iel_cbp)malloc(sizeof(struct iel_cb_base));
        if (!task_cb)
            break;
        task_cb->cb = &taskcb;
        IEL_RESOLVE_CALL(cbp->loop->vt, iou, esoon)(loop->uring, IEL_ARG_NULL, task_cb);
    }
    IEL_RESOLVE_CALL(cbp->loop->vt, iou, etime)(loop->uring, 3000000, IEL_ARG(IEL_FLAG_ETIME_MICROS), timer_cb);
    return 0;
err_postfr:;
    return 1;
err_freecbp:;
    free(cbp);
err:;
    return 1;
}

int main(void) {
    struct iel_evloop_info loop = { .stop=0 };
    int ret = 0;
    int lres;
    unsigned sq_len;
#define L_BPF_RET_I(x) BPF_STMT(BPF_RET | BPF_K, x)
#define L_BPF_COND_I(cond, k, x) BPF_JUMP(BPF_JMP | BPF_J##cond | BPF_K, k, x, 0)
#define L_BPF_NCOND_I(cond, k, x) BPF_JUMP(BPF_JMP | BPF_J##cond | BPF_K, k, 0, x)
#define L_BPF_LD(off) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, off)
    struct sock_filter filter[] = {
        L_BPF_RET_I(SECCOMP_RET_ALLOW),
        L_BPF_LD(offsetof(struct seccomp_data, nr)),
        L_BPF_NCOND_I(EQ, SYS_io_uring_setup, 1),
            L_BPF_RET_I(SECCOMP_RET_ERRNO | EPERM),
        L_BPF_RET_I(SECCOMP_RET_ALLOW),
    };
#undef L_BPF_RET_I
#undef L_BPF_COND_I
#undef L_BPF_NCOND_I
#undef L_BPF_LD
    struct sock_fprog prog = {
        .filter=filter,
        .len=(sizeof(filter)/sizeof(0[filter])),
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        perror("prctl");
        return 1;
    }
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog)) {
        perror("seccomp");
        return 1;
    }
    lres = ielb_iou_vtsetup(&loop.vt);
    if (lres == IEL_VTSETUP_RET_ERROR) {
        perror("ielb_iou_vtsetup");
        return 1;
    } else if (lres == IEL_VTSETUP_RET_UNAVAIL) {
        fputs("NO PROVIDERS AVAILABLE!\n", stderr);
    }
    IEL_RESOLVE_CALL(loop.vt, iou, xinit)(IEL_ARG_NULL);
    loop.uring = (struct ielb_iou_ctx_st *)malloc(IEL_RESOLVE_CALL(loop.vt, iou, lsize)());

    fprintf(stderr, "event loop struct size(vtable excluded): %zu\n", IEL_RESOLVE_CALL(loop.vt, iou, lsize)());
    {
        unsigned long long m0 = micros();
        unsigned long long m1 = micros();
        fprintf(stderr, "micros() overhead on its own: %llu\n", m1 - m0);
    }
    {
        iel_fnptr_lnew p_lnew = loop.vt.p_lnew;
        unsigned long long m0 = micros();
        /* Setup io_uring for use */
        lres = p_lnew(loop.uring, IEL_ARG_NULL);
        unsigned long long m1 = micros();
        fprintf(stderr, "micros() spent on initialisation: %llu;lres=%d\n", m1 - m0, lres);
    }
    if (lres) {
        fprintf(stderr, "Unable to setup uring!\n");
        return 1;
    }

    if (amain(&loop)) {
        fprintf(stderr, "Unable to setup application!\n");
        ret = 1;
        goto out;
    }
    /*
    * A while loop that reads from stdin and writes to stdout.
    * Breaks on EOF.
    */
    fprintf(stderr, "starting event loop\n");
    while (!loop.stop) {
        fprintf(stderr, "run1 event loop\n");
        IEL_RESOLVE_CALL(loop.vt, iou, lrun1)(loop.uring, IEL_ARG_NULL);
    }
    sq_len = (unsigned) IEL_RESOLVE_CALL(loop.vt, iou, xcntl)(loop.uring, IELB_IOU_XCNTL_SQLEN, IEL_ARG_NULL, IEL_ARG_NULL).ull;
    if (sq_len)
        fprintf(stderr, "WARNING: stopping event loop with %u pending submissions in queue\n", sq_len);
out:;
    fprintf(stderr, "main() out\n");
    IEL_RESOLVE_CALL(loop.vt, iou, ldel)(loop.uring);
    free(loop.uring);
    IEL_RESOLVE_CALL(loop.vt, iou, xtdwn)(IEL_ARG_NULL);
    return ret;
}
