#include "bs/render/gl.h"

#define BS_GL_DEFINE(ret, name, params) PFN_##name bs_##name;
BS_GL_FUNCTIONS(BS_GL_DEFINE)
#undef BS_GL_DEFINE

bool bs_gl_load(bs_gl_loader loader, const char **missing)
{
    *missing = NULL;
    bs_gl_proc p;
/* Function and object pointers can't be converted directly in ISO C; go through the proc type. */
#define BS_GL_LOAD(ret, name, params)                  \
    p = loader(#name);                                 \
    if (!p) {                                          \
        *missing = #name;                              \
        return false;                                  \
    }                                                  \
    bs_##name = (PFN_##name)p;
    BS_GL_FUNCTIONS(BS_GL_LOAD)
#undef BS_GL_LOAD
    return true;
}
