#include "common.h"

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

static int text_hash(const char* text, size_t size)
{
    int ret = 0;
    for (size_t i = 0; i < size - 1; i++) {
        ret += (int)text[i] * (int)text[i + 1];
    }
    return ret;
}

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
static const char http_methods[HTTP_METHOD_COUNT][8] =
    {"GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "POST", "PATCH", "CONNECT"};
#define RMH_GET 10695
#define RMH_HEAD 13873
#define RMH_OPTIONS 37575
#define RMH_TRACE 21196
#define RMH_PUT 13940
#define RMH_DELETE 26772
#define RMH_POST 19849
#define RMH_PATCH 21112
#define RMH_CONNECT 33172

#define HTTP_VERSION_COUNT 3
const char http_versions[HTTP_VERSION_COUNT][16] = {
    "HTTP/1.0",
    "HTTP/1.1",
    "HTTP/2.0",
};
#define RVH_1_0 30349
#define RVH_1_1 30395
#define RVH_2_0 30442

void compute_hashes()
{
    for (size_t m = 0; m < HTTP_METHOD_COUNT; m++) {
        printf("%s -> %i\n", http_methods[m], text_hash(http_methods[m], strlen(http_methods[m])));
    }
    for (size_t m = 0; m < HTTP_VERSION_COUNT; m++) {
        printf("%s -> %i\n", http_versions[m], text_hash(http_versions[m], strlen(http_versions[m])));
    }
}

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

static int get_http_method_from_hash(int method_hash)
{
    switch (method_hash) {
    case RMH_GET:
        return REQ_METHOD_GET;
    case RMH_HEAD:
        return REQ_METHOD_HEAD;
    case RMH_OPTIONS:
        return REQ_METHOD_OPTIONS;
    case RMH_TRACE:
        return REQ_METHOD_TRACE;
    case RMH_PUT:
        return REQ_METHOD_PUT;
    case RMH_DELETE:
        return REQ_METHOD_DELETE;
    case RMH_POST:
        return REQ_METHOD_POST;
    case RMH_PATCH:
        return REQ_METHOD_PATCH;
    case RMH_CONNECT:
        return REQ_METHOD_CONNECT;
    }
    return REQ_ERROR_METHOD_PARSE;
}

static int get_http_version_from_hash(int version_hash)
{
    switch (version_hash) {
    case RVH_1_0:
        return REQ_VERSION_1_0;
    case RVH_1_1:
        return REQ_VERSION_1_1;
    case RVH_2_0:
        return REQ_VERSION_2_0;
    }
    return REQ_ERROR_VERSION_PARSE;
}

StringView parse_word(const char* src, size_t max)
{
    size_t i = 0;
    StringView ret = {};
    while (i < max && is_whitespace(src[i])) {
        i++;
    }
    ret.ptr = src + i;
    while (i < max && !is_whitespace(src[i])) {
        ret.size++;
        i++;
    }
    return ret;
}

HttpRequestLine HttpRequestLine_create(const char from[WS_BUFFER_SIZE])
{
    HttpRequestLine rv = {};
    const char* from_cpy = from;

    // make sure request is not too long
    size_t request_line_len = http_nlen(from_cpy, WS_BUFFER_SIZE);
    if (request_line_len == WS_BUFFER_SIZE) {
        rv.method = REQ_ERROR_URI_SIZE;
        return rv;
    }
    // parsing http method
    StringView method_sv = parse_word(from_cpy, WS_BUFFER_SIZE);
    rv.method = get_http_method_from_hash(text_hash(method_sv.ptr, method_sv.size));
    if (method_sv.size == 0 || method_sv.size == WS_BUFFER_SIZE || rv.method == REQ_ERROR_METHOD_PARSE) {
        return rv;
    }
    from_cpy = from + (method_sv.ptr - from) + method_sv.size;
    // parsing http uri
    StringView uri_sv = parse_word(from_cpy, WS_BUFFER_SIZE - (from_cpy - from));
    if (uri_sv.size == 0 || uri_sv.size == WS_BUFFER_SIZE) {
        rv.method = REQ_ERROR_URI_PARSE;
        return rv;
    }
    memcpy(rv.uri, uri_sv.ptr, uri_sv.size);
    from_cpy = from + (uri_sv.ptr - from) + uri_sv.size;
    // parsing http versoin
    StringView version_sv = parse_word(from_cpy, WS_BUFFER_SIZE - (from_cpy - from));
    rv.version = get_http_version_from_hash(text_hash(version_sv.ptr, version_sv.size));
    if (version_sv.size == 0 || version_sv.size == WS_BUFFER_SIZE || rv.version == REQ_ERROR_VERSION_PARSE) {
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

// i got carried away with callgrind
static int connection_hash(const char* src, size_t size)
{
    int hash = 0;
    int pos = 1;
    for (size_t i = 0; i < size; i++) {
        switch (src[i]) {
        case ' ':
            break;
        case 'c':
        case 'C':
        case 'o':
        case 'n':
        case 'e':
        case 't':
        case 'i':
        case 'k':
        case 'K':
        case 'a':
        case 'A':
        case 'l':
        case 'v':
        case 's':
        case 'p':
        case ':':
        case '-':
            hash += tolower(src[i]) * pos;
            pos++;
            break;
        default:
            return -1;
        }
    }
    return hash;
}

int headers_connection_parse(const char* from, size_t max_len)
{
    size_t header_len = http_nlen(from, max_len);
    int chash = connection_hash(from, header_len);
    // printf("header_len=%zu, hash=%i, header=%s|\n", header_len, chash, from);
    switch (chash) {
    case 23059:
        return REQ_CONNECTION_KEEP_ALIVE;
    case 14066:
        return REQ_CONNECTION_CLOSE;
    }
    return 0;
}

static const char* connection_close_cstr = "Connection: close\r\n";
static const char* connection_keepalive_str = "Connection: keep-alive\r\n";
static const char* connection_keepalive_timeout_str = "Keep-Alive: timeout=1, max=500\r\n";
HttpResponse HttpResponse_create(HttpRequest* req, char* header_buffer, size_t header_buffer_size)
{
    HttpResponse ret;
    char* head_ptr = header_buffer;

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
        strcpy(head_ptr, HTTP_1_1);
        head_ptr += strlen(HTTP_1_1);

        ret.code = 505;
        strcpy(head_ptr, HTTP_505);
        head_ptr += strlen(HTTP_505);

        if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
            strcpy(head_ptr, connection_close_cstr);
            head_ptr += strlen(connection_close_cstr);
        } else {
            strcpy(head_ptr, connection_keepalive_str);
            head_ptr += strlen(connection_keepalive_str);
            strcpy(head_ptr, connection_keepalive_timeout_str);
            head_ptr += strlen(connection_keepalive_timeout_str);
        }

        strcpy(head_ptr, "\r\n");
        head_ptr += strlen("\r\n");

        ret.header_size = head_ptr - header_buffer;
        return ret;
    }

    // some error happend with request parsing
    if (req->line.method >= REQ_ERROR) {
        switch (req->line.method) {
        case REQ_ERROR_URI_SIZE:
            strcpy(head_ptr, http_version_str);
            head_ptr += strlen(http_version_str);

            ret.code = 414;
            strcpy(head_ptr, HTTP_414);
            head_ptr += strlen(HTTP_414);

            if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
                strcpy(head_ptr, connection_close_cstr);
                head_ptr += strlen(connection_close_cstr);
            } else {
                strcpy(head_ptr, connection_keepalive_str);
                head_ptr += strlen(connection_keepalive_str);
                strcpy(head_ptr, connection_keepalive_timeout_str);
                head_ptr += strlen(connection_keepalive_timeout_str);
            }

            strcpy(head_ptr, "\r\n");
            head_ptr += strlen("\r\n");

            ret.header_size = head_ptr - header_buffer;
            return ret;

        case REQ_ERROR_URI_PARSE:
        case REQ_ERROR_METHOD_PARSE:
        case REQ_ERROR_VERSION_PARSE:
        default:
            strcpy(head_ptr, http_version_str);
            head_ptr += strlen(http_version_str);

            ret.code = 400;
            strcpy(head_ptr, HTTP_400);
            head_ptr += strlen(HTTP_400);

            if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
                strcpy(head_ptr, connection_close_cstr);
                head_ptr += strlen(connection_close_cstr);
            } else {
                strcpy(head_ptr, connection_keepalive_str);
                head_ptr += strlen(connection_keepalive_str);
                strcpy(head_ptr, connection_keepalive_timeout_str);
                head_ptr += strlen(connection_keepalive_timeout_str);
            }

            strcpy(head_ptr, "\r\n");
            head_ptr += strlen("\r\n");

            ret.header_size = head_ptr - header_buffer;
            return ret;
        }
    }

    // only support GET and HEAD
    if (req->line.method != REQ_METHOD_GET && req->line.method != REQ_METHOD_HEAD) {
        strcpy(head_ptr, http_version_str);
        head_ptr += strlen(http_version_str);

        ret.code = 405;
        strcpy(head_ptr, HTTP_405);
        head_ptr += strlen(HTTP_405);

        if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
            strcpy(head_ptr, connection_close_cstr);
            head_ptr += strlen(connection_close_cstr);
        } else {
            strcpy(head_ptr, connection_keepalive_str);
            head_ptr += strlen(connection_keepalive_str);
            strcpy(head_ptr, connection_keepalive_timeout_str);
            head_ptr += strlen(connection_keepalive_timeout_str);
        }

        strcpy(head_ptr, "\r\n");
        head_ptr += strlen("\r\n");

        ret.header_size = head_ptr - header_buffer;
        return ret;
    }

    // getting the path for the file requested
    int rv = uri_to_path(req->line.uri);
    if (rv < 0) {
        strcpy(head_ptr, http_version_str);
        head_ptr += strlen(http_version_str);

        ret.code = 500;
        strcpy(head_ptr, HTTP_500);
        head_ptr += strlen(HTTP_500);

        if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
            strcpy(head_ptr, connection_close_cstr);
            head_ptr += strlen(connection_close_cstr);
        } else {
            strcpy(head_ptr, connection_keepalive_str);
            head_ptr += strlen(connection_keepalive_str);
            strcpy(head_ptr, connection_keepalive_timeout_str);
            head_ptr += strlen(connection_keepalive_timeout_str);
        }

        strcpy(head_ptr, "\r\n");
        head_ptr += strlen("\r\n");

        ret.header_size = head_ptr - header_buffer;
        return ret;
    }

    // get file info
    ret.finfo = FileInfo_create(req->line.uri);
    if (ret.finfo.result) {
        switch (ret.finfo.result) {
        case EACCES:
            strcpy(head_ptr, http_version_str);
            head_ptr += strlen(http_version_str);

            ret.code = 403;
            strcpy(head_ptr, HTTP_403);
            head_ptr += strlen(HTTP_403);

            if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
                strcpy(head_ptr, connection_close_cstr);
                head_ptr += strlen(connection_close_cstr);
            } else {
                strcpy(head_ptr, connection_keepalive_str);
                head_ptr += strlen(connection_keepalive_str);
                strcpy(head_ptr, connection_keepalive_timeout_str);
                head_ptr += strlen(connection_keepalive_timeout_str);
            }

            strcpy(head_ptr, "\r\n");
            head_ptr += strlen("\r\n");

            ret.header_size = head_ptr - header_buffer;
            break;
        case ENOENT:
        default:
            strcpy(head_ptr, http_version_str);
            head_ptr += strlen(http_version_str);

            ret.code = 404;
            strcpy(head_ptr, HTTP_404);
            head_ptr += strlen(HTTP_404);

            if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
                strcpy(head_ptr, connection_close_cstr);
                head_ptr += strlen(connection_close_cstr);
            } else {
                strcpy(head_ptr, connection_keepalive_str);
                head_ptr += strlen(connection_keepalive_str);
                strcpy(head_ptr, connection_keepalive_timeout_str);
                head_ptr += strlen(connection_keepalive_timeout_str);
            }

            strcpy(head_ptr, "\r\n");
            head_ptr += strlen("\r\n");

            ret.header_size = head_ptr - header_buffer;
        }
        return ret;
    }

    // getting the content type of file path
    const char* content_type = get_content_type(req->line.uri);
    if (strlen(content_type) == 0) {
        strcpy(head_ptr, http_version_str);
        head_ptr += strlen(http_version_str);

        ret.code = 400;
        strcpy(head_ptr, HTTP_400);
        head_ptr += strlen(HTTP_400);

        if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
            strcpy(head_ptr, connection_close_cstr);
            head_ptr += strlen(connection_close_cstr);
        } else {
            strcpy(head_ptr, connection_keepalive_str);
            head_ptr += strlen(connection_keepalive_str);
            strcpy(head_ptr, connection_keepalive_timeout_str);
            head_ptr += strlen(connection_keepalive_timeout_str);
        }

        strcpy(head_ptr, "\r\n");
        head_ptr += strlen("\r\n");

        ret.header_size = head_ptr - header_buffer;
        return ret;
    }

    //
    // success
    //

    strcpy(head_ptr, http_version_str);
    head_ptr += strlen(http_version_str);

    ret.code = 200;
    strcpy(head_ptr, HTTP_200);
    head_ptr += strlen(HTTP_200);

    if (req->headers.connection == REQ_CONNECTION_CLOSE || req->headers.connection == 0) {
        strcpy(head_ptr, connection_close_cstr);
        head_ptr += strlen(connection_close_cstr);
    } else {
        strcpy(head_ptr, connection_keepalive_str);
        head_ptr += strlen(connection_keepalive_str);
        strcpy(head_ptr, connection_keepalive_timeout_str);
        head_ptr += strlen(connection_keepalive_timeout_str);
    }

    // content type
    strcpy(head_ptr, "Content-Type: ");
    head_ptr += strlen("Content-Type: ");
    strcpy(head_ptr, content_type);
    head_ptr += strlen(content_type);
    strcpy(head_ptr, "\r\n");
    head_ptr += strlen("\r\n");

    // content length
    strcpy(head_ptr, "Content-Length: ");
    head_ptr += strlen("Content-Length: ");

    static char buffer[128];
    int snprintf_bytes = snprintf(buffer, 128, "%zu", ret.finfo.length);

    strncpy(head_ptr, buffer, snprintf_bytes);
    head_ptr += snprintf_bytes;
    strcpy(head_ptr, "\r\n");
    head_ptr += strlen("\r\n");

    strcpy(head_ptr, "\r\n");
    head_ptr += strlen("\r\n");

    ret.header_size = head_ptr - header_buffer;
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
        DebugErr("getaddrinfo() error: %s\n", gai_strerror(rv));
        return -1;
    }

    int fd = -1;

    // linked list traversal vibe from beej.us
    struct addrinfo* ptr;
    for (ptr = address_info; ptr != NULL; ptr = ptr->ai_next) {
        if ((fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0) {
            int en = errno;
            DebugErr("socket() error: %s\n", strerror(en));
            continue; // next loop
        }
        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            int en = errno;
            DebugErr("setsockopt() %s\n", strerror(en));
            close(fd);
            continue;
        }
        if ((bind(fd, ptr->ai_addr, ptr->ai_addrlen)) < 0) {
            int en = errno;
            DebugErr("bind() error: %s\n", strerror(en));
            continue; // next loop
        }
        break;
    }

    if (ptr == NULL) {
        DebugErr("failed to find and bind a socket\n");
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
