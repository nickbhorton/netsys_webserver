#include "server.h"

#include <stdbool.h>
#include <string.h>

static bool is_whitespace(char c) { return c == ' '; }

WsRequest WsRequest_create(const char* from)
{
    WsRequest rv = {.method = 0, .version = 0};
    // TODO: is this needed in c99?
    memset(rv.uri, 0, WS_PATH_BUFFER_SIZE);

    size_t end_index = 0;
    char prev = 0;
    char curr = 0;
    while (1) {
        curr = from[end_index];
        if (prev == '\r' && curr == '\n') {
            // put the end index at char befor "\r\n"
            end_index = end_index - 2;
            break;
        }
        end_index++;
        prev = curr;
    }

    size_t curr_index = 0;
    for (; curr_index < end_index; curr_index++) {
        if (strncmp(from + curr_index, "GET", 3) == 0 &&
            is_whitespace(from[curr_index + 3])) {
            rv.method = REQ_METHOD_GET;
            // goto whitespace
            curr_index += 4;
            break;
        } else if (strncmp(from + curr_index, "HEAD", 4) == 0 &&
                   is_whitespace(from[curr_index + 4])) {
            rv.method = REQ_METHOD_HEAD;
            curr_index += 5;
            break;
        } else if (strncmp(from + curr_index, "OPTIONS", 7) == 0 &&
                   is_whitespace(from[curr_index + 7])) {
            rv.method = REQ_METHOD_OPTIONS;
            curr_index += 8;
            break;
        } else if (strncmp(from + curr_index, "TRACE", 5) == 0 &&
                   is_whitespace(from[curr_index + 5])) {
            rv.method = REQ_METHOD_TRACE;
            curr_index += 6;
            break;
        } else if (strncmp(from + curr_index, "PUT", 3) == 0 &&
                   is_whitespace(from[curr_index + 3])) {
            rv.method = REQ_METHOD_PUT;
            curr_index += 4;
            break;
        } else if (strncmp(from + curr_index, "DELETE", 6) == 0 &&
                   is_whitespace(from[curr_index + 6])) {
            rv.method = REQ_METHOD_DELETE;
            curr_index += 7;
            break;
        } else if (strncmp(from + curr_index, "POST", 4) == 0 &&
                   is_whitespace(from[curr_index + 4])) {
            rv.method = REQ_METHOD_POST;
            curr_index += 5;
            break;
        } else if (strncmp(from + curr_index, "PATCH", 5) == 0 &&
                   is_whitespace(from[curr_index + 5])) {
            rv.method = REQ_METHOD_PATCH;
            curr_index += 6;
            break;
        } else if (strncmp(from + curr_index, "CONNECT", 7) == 0 &&
                   is_whitespace(from[curr_index + 7])) {
            rv.method = REQ_METHOD_CONNECT;
            curr_index += 8;
            break;
        }
    }
    if (curr_index == end_index || rv.method == 0) {
        rv.method = REQ_ERROR_METHOD_PARSE;
        return rv;
    }

    size_t uri_start_index = 0;
    for (; curr_index < end_index; curr_index++) {
        if (from[curr_index] == '/') {
            uri_start_index = curr_index;
            break;
        }
    }
    if (curr_index == end_index || uri_start_index == 0) {
        rv.method = REQ_ERROR_URI_PARSE;
        return rv;
    }

    size_t uri_end_index = 0;
    for (; curr_index < end_index; curr_index++) {
        if (is_whitespace(from[curr_index])) {
            // put uri_end_index at char befor whitespace
            uri_end_index = curr_index - 1;
            break;
        }
    }
    if (curr_index == end_index || uri_end_index == 0 ||
        uri_end_index < uri_start_index) {
        rv.method = REQ_ERROR_URI_PARSE;
        return rv;
    }

    size_t uri_size = (uri_end_index + 1) - uri_start_index;
    if (uri_size > WS_PATH_BUFFER_SIZE) {
        rv.method = REQ_ERROR_URI_SIZE;
        return rv;
    }
    memcpy(rv.uri, from + uri_start_index, uri_size);

    for (; curr_index < end_index; curr_index++) {
        if (strncmp(from + curr_index, "HTTP/1.0", 8) == 0 &&
            (is_whitespace(from[curr_index + 8]) || curr_index + 7 == end_index
            )) {
            rv.version = REQ_VERSION_1_0;
            break;
        } else if (strncmp(from + curr_index, "HTTP/1.1", 8) == 0 &&
                   (is_whitespace(from[curr_index + 8]) ||
                    curr_index + 7 == end_index)) {
            rv.version = REQ_VERSION_1_1;
            break;
        } else if (strncmp(from + curr_index, "HTTP/2.0", 8) == 0 &&
                   (is_whitespace(from[curr_index + 8]) ||
                    curr_index + 7 == end_index)) {
            rv.version = REQ_VERSION_2_0;
            break;
        }
    }
    if (rv.version == 0) {
        rv.method = REQ_ERROR_VERSION_PARSE;
        return rv;
    }

    return rv;
}
