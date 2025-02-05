#include "common.h"
#include "debug_macros.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static bool is_whitespace(char c) { return c == ' '; }

WsRequest WsRequest_create(const char* from)
{
    WsRequest rv = {.method = 0, .version = 0};
    // TODO: is this needed in c99?
    memset(rv.uri, 0, WS_PATH_BUFFER_SIZE);

    size_t end_index = 0;
    char prev = 0;
    char curr = 0;
    for (size_t i = 0; i < WS_BUFFER_SIZE; i++) {
        curr = from[i];
        if (prev == '\r' && curr == '\n') {
            // put the end index at char befor "\r\n"
            end_index = i - 2;
            break;
        }
        prev = curr;
    }
    if (end_index == 0) {
        rv.method = REQ_ERROR_BUFFER_OVERFLOW;
        return rv;
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

String response_header(const WsRequest* req) {
    String ret = String_new();
    String_push_cstr(&ret, "HTTP/1.1 200 OK\r\n");
    String_push_cstr(&ret, "Content-Type: text/plain\r\n");
    String_push_cstr(&ret, "Content-Length: 2\r\n");
    String_push_cstr(&ret, "\r\n");
    String_push_cstr(&ret, "Hi");
    return ret;
}

int bind_socket(const char* addr, const char* port, Address* address)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET; // ipv4
    hints.ai_socktype = SOCK_STREAM;
    if (addr == NULL) {
        hints.ai_flags = AI_PASSIVE;
    }
    const char* port_ext = port;
    if (port == NULL) {
        port_ext = "0";
    }

    struct addrinfo* address_info;
    int rv;
    rv = getaddrinfo(addr, port_ext, &hints, &address_info);
    if (rv != 0) {
        NP_DEBUG_ERR("getaddrinfo() error: %s\n", gai_strerror(rv));
        return -1;
    }

    int fd = -1;

    // linked list traversal vibe from beej.us
    struct addrinfo* ptr;
    for (ptr = address_info; ptr != NULL; ptr = ptr->ai_next) {
        if ((fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) <
            0) {
            int en = errno;
            NP_DEBUG_ERR("socket() error: %s\n", strerror(en));
            continue; // next loop
        }
        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            int en = errno;
            NP_DEBUG_ERR("setsockopt() %s\n", strerror(en));
            close(fd);
            continue;
        }
        if ((bind(fd, ptr->ai_addr, ptr->ai_addrlen)) < 0) {
            int en = errno;
            NP_DEBUG_ERR("bind() error: %s\n", strerror(en));
            continue; // next loop
        }
        break;
    }

    if (ptr == NULL) {
        NP_DEBUG_ERR("failed to find and bind a socket\n");
        close(fd);
        freeaddrinfo(address_info);
        return -1;
    }

    // put success address into bound_sock
    memcpy(&address->addr, ptr->ai_addr, ptr->ai_addrlen);
    address->addrlen = ptr->ai_addrlen;

    freeaddrinfo(address_info);
    return fd;
}

struct sockaddr* Address_sockaddr(Address* a)
{
    return (struct sockaddr*)&a->addr;
}
