#ifndef IEL_PRIV_PP_H_
#define IEL_PRIV_PP_H_

#define IEL_PP_EXTRACT0(a,...) a
#define IEL_PP_EXTRACT1(a,b,...) b
#define IEL_PP_EXTRACT2(a,b,c,...) c
#define IEL_PP_EXTRACT3(a,b,c,d,...) d
#define IEL_PP_EXTRACT4(a,b,c,d,e,...) e
#define IEL_PP_EXTRACT5(a,b,c,d,e,f,...) f

#define IEL_PP_EXPAND(x) x
#define IEL_PP_GETARG(n, tup) IEL_PP_EXPAND(IEL_PP_EXTRACT ## n tup)

#define IEL_PP_CONCAT3_0(a, b, c) a ## b ## c
#define IEL_PP_CONCAT3(a, b, c) IEL_PP_CONCAT3_0(a,b,c)

#endif /* ifndef IEL_PRIV_PP_H_ */
