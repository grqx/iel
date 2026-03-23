#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <linux/filter.h>  // for BPF struct
#include <malloc.h>  // for memory tracing

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iel/backends/iou.h>
#include <iel/arg.h>
#include <iel/tagptr.h>
#include <iel/init.h>

static inline
unsigned long long micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#define SEC_TO_US(sec) ((sec)*1000000)
#define NS_TO_US(ns)    ((ns)/1000)
    return SEC_TO_US((unsigned long long)ts.tv_sec) + NS_TO_US((unsigned long long)ts.tv_nsec);
}

struct evloop {
    struct iel_vtable_st vt;
    struct ielb_iou_ctx_st *ctx;
    unsigned char stop;
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
    IEL_CB_BASE base;
    struct evloop *loop;
    unsigned char *buf;
    unsigned int len;
};
struct cbt_onwrite_comp {
    IEL_CB_BASE base;
    int sz;
    struct cbt_onread_comp *par;
};
static
void onwrite_comp(void *_self, int res) {
    struct cbt_onwrite_comp *cbp = (struct cbt_onwrite_comp *)_self;
    if (res != cbp->sz) {
        if (res < 0)
            fprintf(stderr, "write error: %s\n", strerror(-res));
        else
            fprintf(stderr, "write incomplete: wrote %d/%d bytes\n", res, cbp->sz);
    } else {
        IEL_RESOLVE_CALL(cbp->par->loop->vt, iou, fr, (cbp->par->loop->ctx, STDIN_FILENO, cbp->par->buf, cbp->par->len, IEL_ARG_NULL, &cbp->par->base));
    }
    free(cbp);
}

static
void onread_comp(void *_self, int res) {
    struct cbt_onread_comp *cbp = (struct cbt_onread_comp *)_self;
    if (res > 0) {
        /* Read successful. Write to stdout. */
        struct cbt_onwrite_comp *wcb = (struct cbt_onwrite_comp *)malloc(sizeof(struct cbt_onwrite_comp));
        if (wcb) {
            wcb->base = &onwrite_comp;
            wcb->sz = res;
            wcb->par = cbp;
            IEL_RESOLVE_CALL(cbp->loop->vt, iou, fw, (cbp->loop->ctx, STDOUT_FILENO, cbp->buf, res, IEL_ARG_NULL, &wcb->base));
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

static
void ontimer_comp(void *_self, int res) {
    if (res < 0)
        fprintf(stderr, "timeout res<0, strerror: %s\n", strerror(-res));
    fprintf(stderr, "3s time out, res=%d\n", res);
    free(_self);
}

struct soon_comp {
    IEL_CB_BASE base;
    uintmax_t rc;
};
static
void taskcb(void *_self, int res) {
    struct iel_tp_untag_st ut = iel_tp_untag(_self, IEL_CB_ALIGN);
    struct soon_comp *out = (struct soon_comp *)ut.ptr;
    fprintf(stderr, "soon: %d, tag = %#" PRIxMAX "\n", res, ut.tag);
    if (!--out->rc)
        free(out);
}

struct tcpecho {
    IEL_CB_BASE base_read;  /* sa/sw */
    struct evloop *loop;
    IEL_CB_BASE base_write;  /* sr */
    iel_pf_sockfd sock;
    unsigned char *buf; /* 4096 */
    char rstate;
};

static
void tcpecho_read(void *_self, int res) {
    struct tcpecho *pcb = (struct tcpecho *)_self;
    if (res <= 0) {
        free(pcb->buf);
        close(pcb->sock);
        free(pcb);
        return;
    }
    if (pcb->rstate == 'A') {
        pcb->rstate = 'W';
        close(pcb->sock);
        pcb->sock = res;
    }
    IEL_RESOLVE_CALL(pcb->loop->vt, iou, sr, (pcb->loop->ctx, pcb->sock, pcb->buf, 4096, IEL_ARG_NULL, &pcb->base_write));
}

static
void tcpecho_write(void *_self, int res) {
    struct tcpecho *pcb = (struct tcpecho *)(((char *)_self) - offsetof(struct tcpecho, base_write));
    if (res <= 0) {
        free(pcb->buf);
        close(pcb->sock);
        free(pcb);
        return;
    }
    IEL_RESOLVE_CALL(pcb->loop->vt, iou, sw, (pcb->loop->ctx, pcb->sock, pcb->buf, res, IEL_ARG_NULL, &pcb->base_read));
}

struct connect_comp {
    IEL_CB_BASE base_write;
    struct evloop *loop;
    IEL_CB_BASE base_read;
    unsigned char *buf;  /* 4100 */
    IEL_CB_BASE base_vfy;
    struct sockaddr_in sin;
    iel_pf_sockfd sock;
};

static
void onconnect_dowrite(void *_self, int res) {
    struct connect_comp *pcb = (struct connect_comp *)_self;
    if (res < 0) {
        fprintf(stderr, "connect(): %s\n", strerror(-res));
        close(pcb->sock);
        free(pcb->buf);
        pcb->loop->stop = 1;
        free(pcb);
        return;
    }
    IEL_RESOLVE_CALL(pcb->loop->vt, iou, sw, (pcb->loop->ctx, pcb->sock, pcb->buf, 4100, IEL_ARG_NULL, &pcb->base_read));
}

static
void onconnect_doread(void *_self, int res) {
    struct connect_comp *pcb = (struct connect_comp *)((char *)_self - offsetof(struct connect_comp, base_read));
    if (res <= 0) {
        fprintf(stderr, "write(): %s\n", strerror(-res));
        close(pcb->sock);
        free(pcb->buf);
        pcb->loop->stop = 1;
        free(pcb);
        return;
    }
    IEL_RESOLVE_CALL(pcb->loop->vt, iou, sr, (pcb->loop->ctx, pcb->sock, pcb->buf, 4100, IEL_ARG_NULL, &pcb->base_vfy));
    // TODO: should partial reads/writes be handled by the library?
}

static
void onconnect_vfy(void *_self, int res) {
    struct connect_comp *pcb = (struct connect_comp *)((char *)_self - offsetof(struct connect_comp, base_vfy));
    if (res <= 0) {
        fprintf(stderr, "read(): %s\n", strerror(-res));
        goto free_rsrc;
    }
    for (size_t i = 0; i < (unsigned)res; ++i)
        if (pcb->buf[i] != (i & 0xff)) {
            fprintf(
                stderr, "TCP echo result mismatch at byte %zu, expected %hhx, got %hhx\n",
                i, (unsigned char)(i & 0xff), pcb->buf[i]);
            goto free_rsrc;
        }
    fprintf(stderr, "TCP echo result correct, got %d bytes\n", res);
free_rsrc:;
    close(pcb->sock);
    free(pcb->buf);
    pcb->loop->stop = 1;
    free(pcb);
}

static inline
int amain_c(struct evloop *loop) {
    struct connect_comp *cbp;
    iel_pf_sockfd sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
        goto err;
    cbp = (struct connect_comp *)malloc(sizeof(struct connect_comp));
    if (!cbp)
        goto err_closefd;
    cbp->buf = (unsigned char *)malloc(4100);
    if (!cbp->buf)
        goto err_freecbp;
    cbp->base_write = &onconnect_dowrite;
    cbp->base_read = &onconnect_doread;
    cbp->base_vfy = &onconnect_vfy;
    cbp->loop = loop;
    cbp->sock = sock;
    // zero init not required for sin
    cbp->sin.sin_family = AF_INET;
    cbp->sin.sin_addr.s_addr = htonl(0x7f000001);
    cbp->sin.sin_port = htons(6789);
    IEL_RESOLVE_CALL(loop->vt, iou, sc, (loop->ctx, sock, &cbp->sin.sin_family, sizeof(cbp->sin), IEL_ARG_NULL, &cbp->base_write));
    for (size_t i = 0; i < 4100; ++i)
        cbp->buf[i] = i & 0xff;
    return 0;
err_freecbp:;
    free(cbp);
err_closefd:;
    close(sock);
err:;
    return 1;
}

static inline
int amain_s(struct evloop *loop) {
    struct tcpecho *tcpecho;
    struct iel_cb_raw_st *timer_cb;
    unsigned char *buf;
    uintmax_t tagmax = iel_tp_max(IEL_CB_ALIGN);
    iel_pf_sockfd tcpecho_ssock;
    struct sockaddr_in tcpecho_sa;

    {
        struct cbt_onread_comp *cbp = (struct cbt_onread_comp *)malloc(sizeof(struct cbt_onread_comp));
        if (!cbp)
            goto err;
        cbp->base = &onread_comp;
        cbp->loop = loop;
        cbp->len = 4096;
        buf = (unsigned char *)calloc(cbp->len, sizeof(unsigned char));
        if (!buf)
            goto err_freecbp;
        cbp->buf = buf;
        IEL_RESOLVE_CALL(loop->vt, iou, fr, (loop->ctx, STDIN_FILENO, cbp->buf, cbp->len, IEL_ARG_NULL, &cbp->base));
        goto sched_read_out;
err_freecbp:;
        free(cbp);
err:;
        return 1;
    }
sched_read_out:;

    timer_cb = (struct iel_cb_raw_st *)malloc(sizeof(struct iel_cb_raw_st));
    if (!timer_cb)
        goto err_out;
    timer_cb->base = &ontimer_comp;
    for (size_t i = 0;;) {
        struct soon_comp *task_cb = (struct soon_comp *)malloc(sizeof(*task_cb));
        uintmax_t tag = 0;
        if (!task_cb)
            break;
        fprintf(stderr, "soon-alloc: %p\n", (void *)task_cb);
        task_cb->base = &taskcb;
        do {  // its guaranteed that tagmax > 0
            void *sub_ctx = iel_tp_tag((void *)task_cb, IEL_CB_ALIGN, tag);
            IEL_RESOLVE_CALL(loop->vt, iou, esoon, (loop->ctx, IEL_ARG_NULL, sub_ctx));
            ++tag;
            if (++i >= 66) {
                task_cb->rc = tag;
                goto endfillsoon;
            }
        } while (tag < tagmax);
        task_cb->rc = tag;
    }
endfillsoon:;
    IEL_RESOLVE_CALL(loop->vt, iou, etime, (loop->ctx, 3000000, IEL_ARG(IEL_FLAG_ETIME_MICROS), timer_cb));
    tcpecho_ssock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpecho_ssock < 0)
        goto err_out;
    // zero init not required for sin
    memset(&tcpecho_sa.sin_addr, 0, sizeof(struct in_addr));
    tcpecho_sa.sin_port = htons(6789);
    tcpecho_sa.sin_family = AF_INET;
    if (bind(tcpecho_ssock, (struct sockaddr const *)&tcpecho_sa, sizeof tcpecho_sa) < 0)
        goto err_closefd;
    if (listen(tcpecho_ssock, 5) < 0)
        goto err_closefd;
    tcpecho = (struct tcpecho *)malloc(sizeof(struct tcpecho));
    if (!tcpecho)
        goto err_closefd;
    tcpecho->buf = (unsigned char *)malloc(4096);
    if (!tcpecho->buf)
        goto err_freetcpecho;
    tcpecho->base_read = &tcpecho_read;
    tcpecho->base_write = &tcpecho_write;
    tcpecho->loop = loop;
    tcpecho->rstate = 'A';
    tcpecho->sock = tcpecho_ssock;
    IEL_RESOLVE_CALL(loop->vt, iou, sa, (loop->ctx, tcpecho_ssock, NULL, NULL, IEL_ARG_NULL, &tcpecho->base_read));
    return 0;
err_freetcpecho:;
    free(tcpecho);
err_closefd:;
    close(tcpecho_ssock);
err_out:;
    return 1;
}

int main(int argc, char **argv) {
    struct evloop loop = { .stop=0 };
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

    if (argc > 2) {
        fprintf(stderr, "too many args!\n");
        return 1;
    }
    if (argc == 2) {
        if (!argv[1][0]) {
            fprintf(stderr, "argument empty\n");
            return 1;
        }
        else if (argv[1][0] != 'c' && argv[1][0] != 's') {
            fprintf(stderr, "invalid argument, expected 'c' or 's'\n");
            return 1;
        }
        else if (argv[1][1]) {
            fprintf(stderr, "argument more than one char\n");
            return 1;
        }
    }
    iel_init();
    lres = ielb_iou_vtsetup(&loop.vt);
    if (lres == IEL_VTSETUP_RET_ERROR) {
        perror("ielb_iou_vtsetup");
        return 1;
    } else if (lres == IEL_VTSETUP_RET_UNAVAIL) {
        fputs("NO PROVIDERS AVAILABLE!\n", stderr);
        return 1;
    }
    IEL_RESOLVE_CALL(loop.vt, iou, xinit, (IEL_ARG_NULL));
    loop.ctx = (struct ielb_iou_ctx_st *)malloc(IEL_RESOLVE_CALL(loop.vt, iou, lsize, ()));

    fprintf(stderr, "event loop struct size(vtable excluded): %zu\n", IEL_RESOLVE_CALL(loop.vt, iou, lsize, ()));
    {
        unsigned long long m0 = micros();
        unsigned long long m1 = micros();
        fprintf(stderr, "micros() overhead on its own: %llu\n", m1 - m0);
    }
    {
        iel_fn_lnew *p_lnew = loop.vt.p_lnew;
        unsigned long long m0 = micros();
        lres = p_lnew(loop.ctx, IEL_ARG_NULL);
        unsigned long long m1 = micros();
        fprintf(stderr, "micros() spent on initialisation: %llu;lres=%d\n", m1 - m0, lres);
    }
    if (lres) {
        fprintf(stderr, "Unable to setup event loop!\n");
        return 1;
    }

    {
        unsigned long long m0, m1;
        if (argc == 1 || (argc == 2 && argv[1][0] == 's')) {
            m0 = micros();
            ret = amain_s(&loop);
            m1 = micros();
        } else {
            m0 = micros();
            ret = amain_c(&loop);
            m1 = micros();
        }
        if (ret) {
            fprintf(stderr, "Unable to setup application!\n");
            goto out;
        }
        fprintf(stderr, "amain() took %llu micros\n", m1-m0);
    }
    /*
    * A while loop that reads from stdin and writes to stdout.
    * Breaks on EOF.
    */
    fprintf(stderr, "starting event loop\n");
    while (!loop.stop) {
        fprintf(stderr, "run1 event loop\n");
        IEL_RESOLVE_CALL(loop.vt, iou, lrun1, (loop.ctx, IEL_ARG_NULL));
    }
    sq_len = (unsigned) IEL_RESOLVE_CALL(loop.vt, iou, xcntl, (loop.ctx, IELB_IOU_XCNTL_SQLEN, IEL_ARG_NULL, IEL_ARG_NULL)).ull;
    if (sq_len)
        fprintf(stderr, "WARNING: stopping event loop with %u pending submissions in queue\n", sq_len);
out:;
    fprintf(stderr, "main() out\n");
    IEL_RESOLVE_CALL(loop.vt, iou, ldel, (loop.ctx));
    free(loop.ctx);
    IEL_RESOLVE_CALL(loop.vt, iou, xtdwn, (IEL_ARG_NULL));
    return ret;
}
