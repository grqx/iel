#ifndef IEL_PRIV_QUEP_H_
#define IEL_PRIV_QUEP_H_

#include <stdalign.h>

#define IEL_QUE_TPL (/*ChunkEntsBit=*/7, /*MinChunks=*/16, /*type=*/void *, /*pfx=*/iel_quep_, /*sfx=*/, /*api=*/, /*align=*/alignof(void *),)
#include <iel_priv/que.tpl.h>

#endif /* ifndef IEL_PRIV_QUEP_H_ */
