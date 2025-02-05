#ifndef NP_DEBUG_MACROS
#define NP_DEBUG_MACROS

#include <stdio.h>

void fprinttime(FILE* stream);

#define NP_DEBUG 1

#define NP_DEBUG_ERR(...)                                                      \
    if (NP_DEBUG) {                                                            \
        fprinttime(stderr);                                                    \
        fprintf(stderr, __VA_ARGS__);                                          \
    }

#define NP_DEBUG_MSG(...)                                                    \
    if (NP_DEBUG) {                                                            \
        fprinttime(stdout);                                                    \
        fprintf(stdout, __VA_ARGS__);                                          \
    }

#endif
