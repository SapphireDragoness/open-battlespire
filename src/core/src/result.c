#include "bs/core/result.h"

const char *bs_result_str(bs_result r)
{
    switch (r) {
    case BS_OK:              return "ok";
    case BS_ERR_ARGS:        return "invalid argument";
    case BS_ERR_NOMEM:       return "out of memory";
    case BS_ERR_IO:          return "I/O error";
    case BS_ERR_NOT_FOUND:   return "not found";
    case BS_ERR_FORMAT:      return "malformed data";
    case BS_ERR_UNSUPPORTED: return "unsupported";
    }
    return "unknown error";
}
