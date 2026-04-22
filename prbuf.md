Submit registered buffer:
`RIO` uses `RIO_BUFFERID` which can be cast to `void *`, then 1x `(RIO_BUF) {void *bufid; ulong os,len;};`
`IoRing` uses u32 buff index can be cast to u64/umax, then `(IORING_REGISTERED_BUFFER) {u32 bufid,os;}; u32 len;`
`io_uring` uses `[u32 iovcnt]? (struct iovec) {void *ptr = (char *)r[bufid].iov_base + os; u32 len; }; u16 bufid;`

Register buffer:
`RIO` says "A pointer to the beginning of the memory buffer to register"; char *buf; u32 sz;
`IoRing` says "A VOID pointer representing the address of the data buffer"; {void *buf; u32 sz;}
`io_uring` says "<1GiB `MAP_ANONYMOUS`"; `[u count] (struct iovec) {void *buf; size_t sz;};` `IORING_REGISTER_BUFFERS`; non-registered buffer-select support

Register handle:
`RIO`: RQ per `SOCKET`(`uintptr_t`) -> `RIO_RQ` (`void *` -> `uintptr_t`)
`IoRing`: [u32 count]HANDLE(void *) -> u32; async operation (can convert to sync if 0 in-flight)
`io_uring`: [u count]s32 -> u32(s32); `IORING_REGISTER_FILES`, sync setup, unregister later (auto on ring teardown), 0 in-flight for <5.13
proposed api:
```c
struct iel_pf_reg_fd_st {
    iel_pf_fd raw;
    iel_pf_fd_r reg;
};
struct iel_pf_reg_sockfd_st {
    iel_pf_sockfd raw;
    iel_pf_sockfd_r reg;
};
int xreg(
    void * restrict ctx,
    unsigned char opcode,
    const void * restrict in,
    void * restrict out,
    size_t nr_args,
    union iel_arg_un flags);
```

argument types:
`IEL_XREG_FILES`:
    `in`: real type `struct iel_pf_reg_fd_st const *`, nullable; if null, `in = out`.
    `out`: real type `struct iel_pf_reg_fd_st *`, non-null.
    `nr_args`: if `in` is not null, it should have `nr_args * sizeof(struct iel_pf_reg_fd_st)` bytes readable;
        `out` should have `nr_args * sizeof(struct iel_pf_reg_fd_st)` bytes readable and writable.
`IEL_XREG_SOCKETS`:
    Same as `IEL_XREG_FILES`, except that the element type is `struct iel_pf_reg_sockfd_st`.
return:
-1 + `iel_errno`: all failed, and the backend does not support `IEL_FEAT_NOREG_HANDLE` (`xfeat(ctx, IEL_ARG_NULL) & IEL_FEAT_NOREG_HANDLE`)
0 + `iel_errno`: all failed, but the backend supports `IEL_FEAT_NOREG_HANDLE` (`!(xfeat(ctx, IEL_ARG_NULL) & IEL_FEAT_NOREG_HANDLE)`)
`nr_args`: all succeeded

Required?:
`RIO`: register buffer; raw fd
`IoRing`: {raw/async-registered} {fd/buffer}
`io_uring`: {raw/sync-registered} {fd/buffer}

man
`io_uring_setup`
`io_uring_register`
`io_uring_enter`
