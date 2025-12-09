#ifndef IEL_ARG_H_
#define IEL_ARG_H_

union iel_arg_un {
    void *ptr;
    unsigned long long ull;
};

#define IEL_ARG_NULL ((union iel_arg_un) { .ull = 0ULL })
#define IEL_ARG_NULLPTR ((union iel_arg_un) { .ptr = NULL })

#endif /* ifndef IEL_ARG_H_ */
