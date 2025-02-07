#include "debug_macros.h"

#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TIME_BUFFER_SIZE 256

void fprinttime(FILE* stream)
{
    struct timeval rn;
    gettimeofday(&rn, NULL);
    fprintf(stream, "%ld: ", rn.tv_usec);
}
