#include "common.h"
#include "String.h"
#include "debug_macros.h"

#include <arpa/inet.h>
#include <ctype.h>
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

// errors wont have files attached so double \r\n
static const char* HTTP_400 = "400 Bad Request\r\n";
static const char* HTTP_403 = "403 Forbidden\r\n";
static const char* HTTP_404 = "404 Not Found\r\n";
static const char* HTTP_405 = "405 Method Not Allowed\r\n";
static const char* HTTP_414 = "414 URI Too Long\r\n";
static const char* HTTP_500 = "500 Internal Sever Error\r\n";
static const char* HTTP_505 = "505 HTTP Versoin Not Supported\r\n";

#define CONTENT_TYPE_COUNT 16
static char content_type_trans[CONTENT_TYPE_COUNT][2][64] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"jpg", "image/jpg"},
    {"png", "image/png"},
    {"gif", "image/gif"},
    {"txt", "text/plain"},
    {"htm", "text/html"},
    {"ico", "image/x-icon"},
    // not required
    {"pdf", "application/pdf"},
    {"json", "application/json"},
    {"bin", "application/octect-stream"},
    {"bmp", "image/bmp"},
    {"csv", "image/csv"},
    {"webp", "image/webp"},
    {"jpeg", "image/jpg"},
};

#define HTTP_METHOD_COUNT 9
const char http_methods[HTTP_METHOD_COUNT][8] =
    {"GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "POST", "PATCH", "CONNECT"};
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
const char http_versions[HTTP_VERSION_COUNT][16] = {
    "HTTP/1.0",
    "HTTP/1.1",
    "HTTP/2.0",
};
const int http_versions_index[HTTP_VERSION_COUNT] = {
    REQ_VERSION_1_0,
    REQ_VERSION_1_1,
    REQ_VERSION_2_0,
};

static bool is_whitespace(char c) { return c == ' ' || c == '\r' || c == '\t'; }

size_t http_nlen(const char* src, size_t max)
{
    for (size_t i = 1; i < max; i++) {
        if (src[i - 1] == '\r' && src[i] == '\n') {
            return i - 1;
        } else if (src[i] == '\0') {
            return max;
        }
    }
    return max;
}

HttpRequestLine HttpRequestLine_create(const char from[WS_BUFFER_SIZE])
{
    // initalize everything to 0
    HttpRequestLine rv = {};

    size_t request_line_len = http_nlen(from, WS_BUFFER_SIZE);
    if (request_line_len == WS_BUFFER_SIZE) {
        rv.method = REQ_ERROR_URI_SIZE;
        return rv;
    }

    unsigned int i = 0;
    for (; i < request_line_len; i++) {
        for (size_t j = 0; j < HTTP_METHOD_COUNT; j++) {
            unsigned int method_len = strlen(http_methods[j]);
            if (strncmp(from + i, http_methods[j], method_len) == 0 && is_whitespace(from[i + method_len])) {
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
    size_t uri_end_index = 0;
    for (; i < request_line_len; i++) {
        if (!is_whitespace(from[i]) && uri_start_index == 0) {
            uri_start_index = i;
        } else if (is_whitespace(from[i]) && uri_start_index != 0) {
            uri_end_index = i;
            break;
        }
    }
    if (uri_end_index == 0 || uri_start_index == 0) {
        rv.method = REQ_ERROR_URI_PARSE;
        return rv;
    }

    memcpy(rv.uri, from + uri_start_index, uri_end_index - uri_start_index);

    for (; i < request_line_len; i++) {
        for (size_t j = 0; j < HTTP_VERSION_COUNT; j++) {
            unsigned int version_len = strlen(http_versions[j]);
            if (strncmp(from + i, http_versions[j], version_len) == 0 && is_whitespace(from[i + version_len])) {
                rv.version = http_methods_index[j];
                i += version_len + 1;
                goto http_version_done;
            }
        }
    }

http_version_done:
    if (rv.version == 0) {
        rv.method = REQ_ERROR_VERSION_PARSE;
        return rv;
    }
    return rv;
}

HttpRequest HttpRequest_create(const char from[WS_BUFFER_SIZE])
{
    HttpRequest req = {};
    req.line = HttpRequestLine_create(from);
    if (req.line.method > REQ_ERROR) {
        return req;
    }

    size_t i = 0;
    int rv = 0;
    while (1) {
        size_t header_len = http_nlen(from + i, WS_BUFFER_SIZE - i);
        if (header_len + i >= WS_BUFFER_SIZE || header_len == 0) {
            break;
        }
        // skip to next header
        i += header_len + 2;
        if ((rv = headers_connection_parse(from + i, WS_BUFFER_SIZE - i)) > 0) {
            req.headers.connection = rv;
        }
    }
    return req;
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
        if (strncmp(path + dot_index + 1, content_type_trans[i][0], WS_PATH_BUFFER_SIZE - (dot_index + 1)) == 0) {
            return content_type_trans[i][1];
        }
    }
    return "";
}

int uri_to_path(char uri[WS_URI_BUFFER_SIZE])
{
    unsigned int root_len = strlen(ROOT_DIR);
    if (strncmp("/", uri, WS_URI_BUFFER_SIZE) == 0 || strncmp("/inside/", uri, WS_URI_BUFFER_SIZE) == 0) {
        const char* default_path = "/index.html";
        memcpy(uri, ROOT_DIR, root_len);
        memcpy(uri + root_len, default_path, strlen(default_path));
        return 0;
    }
    unsigned int uri_len = strnlen(uri, WS_PATH_BUFFER_SIZE);
    memmove(uri + root_len, uri, uri_len);
    memcpy(uri, ROOT_DIR, root_len);
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

int headers_connection_parse(const char* from, size_t max_len)
{
    size_t start = 0;
    while (is_whitespace(from[start])) {
        start++;
        if (from[start] == '\0') {
            return 0;
        }
    }
    size_t header_len = http_nlen(from + start, max_len);
    if (header_len == 0) {
        return 0;
    }
    static const char* connection_str = "connection";
    size_t i;
    for (i = 0; i < header_len; i++) {
        if (from[start + i] == ':') {
            i++;
            break;
        } else if (tolower(from[start + i]) != connection_str[i]) {
            return 0;
        }
    }
    if (i >= header_len) {
        return 0;
    }

    // eat whitespace
    for (; i < header_len; i++) {
        if (!is_whitespace(from[start + i])) {
            break;
        }
    }
    if (i >= header_len) {
        return 0;
    }
    static const char* keep_alive_str = "keep-alive";
    static const char* close_str = "close";
    size_t j;
    for (j = 0; j < strlen(keep_alive_str); j++) {
        if (tolower(from[start + i + j]) != keep_alive_str[j]) {
            break;
        }
    }
    if (j == strlen(keep_alive_str)) {
        return REQ_CONNECTION_KEEP_ALIVE;
    }
    for (j = 0; j < strlen(close_str); j++) {
        if (tolower(from[start + i + j]) != close_str[j]) {
            break;
        }
    }
    if (j == strlen(close_str)) {
        return REQ_CONNECTION_CLOSE;
    }
    return 0;
}

static void add_response_code(HttpResponse* response, uint32_t code)
{
    switch (code) {
    case 200:
        String_push_cstr(&response->header, HTTP_200);
        response->code = 200;
        break;
    case 400:
        String_push_cstr(&response->header, HTTP_400);
        response->code = 400;
        break;
    case 403:
        String_push_cstr(&response->header, HTTP_403);
        response->code = 403;
        break;
    case 404:
        String_push_cstr(&response->header, HTTP_404);
        response->code = 404;
        break;
    case 405:
        String_push_cstr(&response->header, HTTP_405);
        response->code = 405;
        break;
    case 414:
        String_push_cstr(&response->header, HTTP_414);
        response->code = 414;
        break;
    case 500:
        String_push_cstr(&response->header, HTTP_500);
        response->code = 500;
        break;
    case 505:
        String_push_cstr(&response->header, HTTP_505);
        response->code = 505;
        break;
    default:
        String_push_cstr(&response->header, HTTP_500);
        response->code = 500;
        break;
    }
}

/*
static void response_file_error(HttpResponse* response, int en)
{
    switch (en) {
    case EACCES:
        add_response_code(response, 403);
        break;
    case ENOENT:
    default:
        add_response_code(response, 404);
    }
}
*/

static void add_connection_header(String* str, int connection_header)
{
    // keep alive or close
    if (connection_header == REQ_CONNECTION_CLOSE || connection_header == 0) {
        String_push_cstr(str, "Connection: close\r\n");
    } else {
        String_push_cstr(str, "Connection: keep-alive\r\n");
        String_push_cstr(str, "Keep-Alive: timeout=1, max=200\r\n");
    }
}

HttpResponse HttpResponse_create(HttpRequest* req)
{
    HttpResponse ret;
    ret.header = String_new();

    const char* http_version_str;
    switch (req->line.version) {
    case REQ_VERSION_1_0:
        http_version_str = HTTP_1_0;
        break;
    case REQ_VERSION_1_1:
        http_version_str = HTTP_1_1;
        break;
    case REQ_VERSION_2_0:
    default:
        // Version not supported error
        String_push_cstr(&ret.header, HTTP_1_1);
        add_response_code(&ret, 505);
        add_connection_header(&ret.header, req->headers.connection);
        String_push_cstr(&ret.header, "\r\n");
        return ret;
    }

    // some error happend with request parsing
    if (req->line.method >= REQ_ERROR) {
        switch (req->line.method) {
        case REQ_ERROR_URI_SIZE:
            String_push_cstr(&ret.header, http_version_str);
            add_response_code(&ret, 414);
            add_connection_header(&ret.header, req->headers.connection);
            String_push_cstr(&ret.header, "\r\n");
            return ret;

        case REQ_ERROR_URI_PARSE:
        case REQ_ERROR_METHOD_PARSE:
        case REQ_ERROR_VERSION_PARSE:
        default:
            String_push_cstr(&ret.header, http_version_str);
            add_response_code(&ret, 400);
            add_connection_header(&ret.header, req->headers.connection);
            String_push_cstr(&ret.header, "\r\n");
            return ret;
        }
    }

    // only support GET and HEAD
    if (req->line.method != REQ_METHOD_GET && req->line.method != REQ_METHOD_HEAD) {
        String_push_cstr(&ret.header, http_version_str);
        add_response_code(&ret, 405);
        add_connection_header(&ret.header, req->headers.connection);
        String_push_cstr(&ret.header, "\r\n");
        return ret;
    }

    // getting the path for the file requested
    int rv = uri_to_path(req->line.uri);
    if (rv < 0) {
        String_push_cstr(&ret.header, http_version_str);
        add_response_code(&ret, 500);
        add_connection_header(&ret.header, req->headers.connection);
        String_push_cstr(&ret.header, "\r\n");
        return ret;
    }

    // get file info
    ret.finfo = FileInfo_create(req->line.uri);
    if (ret.finfo.result) {
        switch (ret.finfo.result) {
        case EACCES:
            String_push_cstr(&ret.header, http_version_str);
            add_response_code(&ret, 403);
            add_connection_header(&ret.header, req->headers.connection);
            String_push_cstr(&ret.header, "\r\n");
            break;
        case ENOENT:
        default:
            String_push_cstr(&ret.header, http_version_str);
            add_response_code(&ret, 404);
            add_connection_header(&ret.header, req->headers.connection);
            String_push_cstr(&ret.header, "\r\n");
        }
        return ret;
    }

    // getting the content type of file path
    const char* content_type = get_content_type(req->line.uri);
    if (strlen(content_type) == 0) {
        String_push_cstr(&ret.header, http_version_str);
        add_response_code(&ret, 400);
        add_connection_header(&ret.header, req->headers.connection);
        String_push_cstr(&ret.header, "\r\n");
        return ret;
    }

    //
    // success
    //

    String_push_cstr(&ret.header, http_version_str);
    add_response_code(&ret, 200);
    add_connection_header(&ret.header, req->headers.connection);

    // content type
    String_push_cstr(&ret.header, "Content-Type: ");
    String_push_cstr(&ret.header, content_type);
    String_push_cstr(&ret.header, "\r\n");

    // content length
    String_push_cstr(&ret.header, "Content-Length: ");
    char buffer[128];
    memset(buffer, 0, 128);
    snprintf(buffer, 128, "%zu", ret.finfo.length);
    String_push_cstr(&ret.header, buffer);
    String_push_cstr(&ret.header, "\r\n");

    // server
    String_push_cstr(&ret.header, "Server: nbh\r\n");
    String_push_cstr(&ret.header, "\r\n");
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
        if (rv == -8) {
            fprintf(stderr, "port must be a number\n");
            return -1;
        }
        NP_DEBUG_ERR("getaddrinfo() error: %s\n", gai_strerror(rv));
        return -1;
    }

    int fd = -1;

    // linked list traversal vibe from beej.us
    struct addrinfo* ptr;
    for (ptr = address_info; ptr != NULL; ptr = ptr->ai_next) {
        if ((fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0) {
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

struct sockaddr* Address_sockaddr(Address* a) { return (struct sockaddr*)&a->addr; }
