#include "debug_macros.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#define TIME_BUFFER_SIZE 256

void fprinttime(FILE* stream)
{
    static char buffer[TIME_BUFFER_SIZE];
    static bool buffer_zeroed = false;
    if (!buffer_zeroed) {
        memset(buffer, 0, TIME_BUFFER_SIZE);
        buffer_zeroed = true;
    }

    time_t rn;
    time(&rn);
    struct tm* rn_tm = localtime(&rn);
    int rv = strftime(buffer, TIME_BUFFER_SIZE, "%m%d%Y-%H:%M:%S", rn_tm);
    if (rv < 0) {
        fprintf(stderr, "strftime returned an error\n");
        return;
    }
    fprintf(stream, "%s: ", buffer);
    // clear buffer
    memset(buffer, 0, rv);
}
