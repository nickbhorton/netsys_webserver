#include "common.h"
#include "String.h"
#include "debug_macros.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char* HTTP_1_1 = "HTTP/1.1 ";
static const char* HTTP_1_0 = "HTTP/1.0 ";

static const char* HTTP_200 = "200 Ok\r\n";
static const char* HTTP_400 = "400 Bad Request\r\n";
static const char* HTTP_403 = "403 Forbidden\r\n";
static const char* HTTP_404 = "404 Not Found\r\n";
static const char* HTTP_405 = "405 Method Not Allowed\r\n";
static const char* HTTP_414 = "414 URI Too Long\r\n";
static const char* HTTP_500 = "500 Internal Sever Error\r\n";
static const char* HTTP_505 = "505 HTTP Versoin Not Supported\r\n";

#define CONTENT_TYPE_COUNT 11
static char content_type_trans[CONTENT_TYPE_COUNT][2][64] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"htm", "text/html"},
    {"jpg", "image/jpg"},
    {"png", "image/png"},
    {"webp", "image/webp"},
    {"gif", "image/gif"},
    {"txt", "text/plain"},
    {"jpeg", "image/jpg"},
    {"ico", "image/x-icon"},
};

#define HTTP_METHOD_COUNT 9
const char http_methods[HTTP_METHOD_COUNT][8] = {
    "GET",
    "HEAD",
    "OPTIONS",
    "TRACE",
    "PUT",
    "DELETE",
    "POST",
    "PATCH",
    "CONNECT"
};
const int http_methods_index[HTTP_METHOD_COUNT] = {
    REQ_METHOD_GET,
    REQ_METHOD_HEAD,
    REQ_METHOD_OPTIONS,
    REQ_METHOD_TRACE,
    REQ_METHOD_PUT,
    REQ_METHOD_DELETE,
    REQ_METHOD_POST,
    REQ_METHOD_PATCH,
    REQ_METHOD_CONNECT,
};

#define HTTP_VERSION_COUNT 3
const char http_version[HTTP_VERSION_COUNT][16] = {
    "HTTP/1.0",
    "HTTP/1.1",
    "HTTP/2.0",
};
const int http_version_index[HTTP_VERSION_COUNT] = {
    REQ_VERSION_1_0,
    REQ_VERSION_1_1,
    REQ_VERSION_2_0,
};

static bool is_whitespace(char c) { return c == ' '; }

WsRequest WsRequest_create(const char from[WS_BUFFER_SIZE])
{
    WsRequest rv = {.method = 0, .version = 0};
    memset(rv.uri, 0, WS_URI_BUFFER_SIZE);

    unsigned int request_line_len = WS_BUFFER_SIZE;
    for (size_t i = 1; i < WS_BUFFER_SIZE; i++) {
        if (from[i - 1] == '\r' && from[i] == '\n') {
            request_line_len = i - 2;
            break;
        }
    }
    if (request_line_len == WS_BUFFER_SIZE) {
        rv.method = REQ_ERROR_URI_SIZE;
        return rv;
    }

    unsigned int i = 0;
    for (; i < request_line_len; i++) {
        for (size_t j = 0; j < HTTP_METHOD_COUNT; j++) {
            unsigned int method_len = strlen(http_methods[j]);
            if (strncmp(from + i, http_methods[j], method_len) == 0 &&
                is_whitespace(from[i + method_len])) {
                rv.method = http_methods_index[j];
                i += method_len + 1;
                goto http_method_done;
            }
        }
    }

http_method_done:
    if (rv.method == 0) {
        rv.method = REQ_ERROR_METHOD_PARSE;
        return rv;
    }

    size_t uri_start_index = 0;
    for (; i < request_line_len; i++) {
        if (from[i] == '/') {
            uri_start_index = i;
            break;
        }
    }
    if (i == request_line_len || uri_start_index == 0) {
        rv.method = REQ_ERROR_URI_PARSE;
        return rv;
    }

    size_t uri_end_index = 0;
    for (; i < request_line_len; i++) {
        if (is_whitespace(from[i])) {
            // put uri_end_index at char befor whitespace
            uri_end_index = i - 1;
            break;
        }
    }
    if (i == request_line_len || uri_end_index == 0 ||
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

    for (; i < request_line_len; i++) {
        if (strncmp(from + i, "HTTP/1.0", 8) == 0 &&
            (is_whitespace(from[i + 8]) || i + 7 == request_line_len)) {
            rv.version = REQ_VERSION_1_0;
            break;
        } else if (strncmp(from + i, "HTTP/1.1", 8) == 0 &&
                   (is_whitespace(from[i + 8]) || i + 7 == request_line_len)) {
            rv.version = REQ_VERSION_1_1;
            break;
        } else if (strncmp(from + i, "HTTP/2.0", 8) == 0 &&
                   (is_whitespace(from[i + 8]) || i + 7 == request_line_len)) {
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

const char* get_content_type(const char* path)
{
    size_t dot_index = WS_PATH_BUFFER_SIZE;
    size_t path_len = strnlen(path, WS_PATH_BUFFER_SIZE);
    for (int i = ((int)path_len) - 1; i > 0; i--) {
        if (path[i] == '.') {
            dot_index = i;
            break;
        }
    }
    if (dot_index == WS_PATH_BUFFER_SIZE) {
        return "";
    }

    for (size_t i = 0; i < CONTENT_TYPE_COUNT; i++) {
        if (strncmp(
                path + dot_index + 1,
                content_type_trans[i][0],
                WS_PATH_BUFFER_SIZE - (dot_index + 1)
            ) == 0) {
            return content_type_trans[i][1];
        }
    }
    return "";
}

int uri_to_path(char uri[WS_URI_BUFFER_SIZE])
{
    unsigned int root_len = strlen(MNT_DIR);
    if (strncmp("/", uri, WS_URI_BUFFER_SIZE) == 0 ||
        strncmp("/inside/", uri, WS_URI_BUFFER_SIZE) == 0) {
        const char* default_path = "/index.html";
        memcpy(uri, MNT_DIR, root_len);
        memcpy(uri + root_len, default_path, strlen(default_path));
        return 0;
    }
    unsigned int uri_len = strnlen(uri, WS_PATH_BUFFER_SIZE);
    memmove(uri + root_len, uri, uri_len);
    memcpy(uri, MNT_DIR, root_len);
    return 0;
}

FileInfo FileInfo_create(const char* uri)
{
    struct stat st;
    FileInfo result = {};
    int rv = stat(uri, &st);
    if (rv < 0) {
        int en = errno;
        result.result = en;
        return result;
    }
    result.result = 0;
    result.length = st.st_size;
    return result;
}

bool connection_keep_alive(char* request_buffer)
{
    const char* keep_alive_header = "Connection: keep-alive\r\n";
    for (size_t i = 0; i < WS_BUFFER_SIZE - strlen(keep_alive_header) - 2;
         i++) {
        if (strncmp(request_buffer + i, "\r\n", 2) == 0) {
            i += 2;
            int rv = strncmp(
                keep_alive_header,
                request_buffer + i,
                strlen(keep_alive_header)
            );
            if (rv == 0) {
                return true;
            }
        }
    }
    return false;
}

bool connection_close(char* request_buffer)
{
    const char* keep_alive_header = "Connection: close\r\n";
    for (size_t i = 0; i < WS_BUFFER_SIZE - strlen(keep_alive_header) - 2;
         i++) {
        if (strncmp(request_buffer + i, "\r\n", 2) == 0) {
            i += 2;
            int rv = strncmp(
                keep_alive_header,
                request_buffer + i,
                strlen(keep_alive_header)
            );
            if (rv == 0) {
                return true;
            }
        }
    }
    return false;
}

static void
response_file_error(int en, const char* http_version_str, String* response)
{
    switch (en) {
    case EACCES:
        String_push_cstr(response, http_version_str);
        String_push_cstr(response, HTTP_403);
        break;
    case ENOENT:
    default:
        String_push_cstr(response, http_version_str);
        String_push_cstr(response, HTTP_404);
    }
}

String get_response(WsRequest* req, bool close)
{
    String ret = String_new();
    const char* http_version_str = HTTP_1_1;
    if (req->version == REQ_VERSION_1_0) {
        http_version_str = HTTP_1_0;
    } else if (req->version == REQ_VERSION_1_1) {
        http_version_str = HTTP_1_1;
    } else {
        // Version not supported
        String_push_cstr(&ret, http_version_str);
        String_push_cstr(&ret, HTTP_505);
        return ret;
    }

    // some error happend with request parsing
    if (req->method >= REQ_ERROR) {
        switch (req->method) {
        case REQ_ERROR_URI_SIZE:
            String_push_cstr(&ret, http_version_str);
            String_push_cstr(&ret, HTTP_414);
            return ret;

        case REQ_ERROR_URI_PARSE:
        case REQ_ERROR_METHOD_PARSE:
        case REQ_ERROR_VERSION_PARSE:
        default:
            String_push_cstr(&ret, http_version_str);
            String_push_cstr(&ret, HTTP_400);
            return ret;
        }
    }
    // only support Get
    if (req->method != REQ_METHOD_GET) {
        String_push_cstr(&ret, http_version_str);
        String_push_cstr(&ret, HTTP_405);
        return ret;
    }

    int rv = uri_to_path(req->uri);
    if (rv < 0) {
        String_push_cstr(&ret, http_version_str);
        String_push_cstr(&ret, HTTP_500);
        return ret;
    }

    const char* content_type = get_content_type(req->uri);
    if (strlen(content_type) == 0) {
        String_push_cstr(&ret, http_version_str);
        String_push_cstr(&ret, HTTP_400);
        return ret;
    }

    // get file size
    FileInfo fi = FileInfo_create(req->uri);
    if (fi.result) {
        response_file_error(fi.result, http_version_str, &ret);
        return ret;
    }

    // check if file can be opened
    FILE* fptr = fopen(req->uri, "r");
    if (fptr == NULL) {
        int en = errno;
        response_file_error(en, http_version_str, &ret);
        return ret;
    }

    String_push_cstr(&ret, http_version_str);
    String_push_cstr(&ret, HTTP_200);

    String_push_cstr(&ret, "Content-Type: ");
    String_push_cstr(&ret, content_type);
    String_push_cstr(&ret, "\r\n");

    String_push_cstr(&ret, "Content-Length: ");
    char buffer[128];
    memset(buffer, 0, 128);
    snprintf(buffer, 128, "%zu", fi.length);
    String_push_cstr(&ret, buffer);
    String_push_cstr(&ret, "\r\n");
    if (close) {
        String_push_cstr(&ret, "Connection: close\r\n");
    } else {
        String_push_cstr(&ret, "Connection: keep-alive\r\n");
    }
    String_push_cstr(&ret, "\r\n");
    int c;
    while ((c = fgetc(fptr)) != EOF) {
        String_push_back(&ret, (char)c);
    }
    fclose(fptr);
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
