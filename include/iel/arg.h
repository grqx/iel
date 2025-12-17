#ifndef IEL_ARG_H_
#define IEL_ARG_H_

typedef void (*iel_arg_fnptr)(void);

union iel_arg_un {
    unsigned long long ull;
    void *ptr;
    iel_arg_fnptr fnptr;
};
#define IEL_ARG(x) _Generic(x, \
        unsigned long long: (union iel_arg_un) { .ull = (unsigned long long)x }, \
        void *: (union iel_arg_un) { .ptr = (void *)x }, \
        iel_arg_fnptr: (union iel_arg_un) { .fnptr = (iel_arg_fnptr)x } \
    )

#define IEL_ARG_NULL ((union iel_arg_un) { .ull = 0ULL })
#define IEL_ARG_NULLPTR ((union iel_arg_un) { .ptr = NULL })
#define IEL_ARG_NULLFNPTR ((union iel_arg_un) { .fnptr = NULL })

#endif /* ifndef IEL_ARG_H_ */
