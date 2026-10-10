#ifndef BS_CORE_RESULT_H
#define BS_CORE_RESULT_H

/* Error codes returned by library functions. Library code never calls exit(). */
typedef enum bs_result {
    BS_OK = 0,
    BS_ERR_ARGS,      /* invalid argument */
    BS_ERR_NOMEM,     /* allocation failed */
    BS_ERR_IO,        /* read/write/open failed */
    BS_ERR_NOT_FOUND, /* file or record does not exist */
    BS_ERR_FORMAT,    /* data is malformed or truncated */
    BS_ERR_UNSUPPORTED
} bs_result;

const char *bs_result_str(bs_result r);

#endif
