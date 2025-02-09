#ifndef NBH_COMMON_HEADER
#define NBH_COMMON_HEADER

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "String.h"

#define WS_BUFFER_SIZE 2048

#define WS_URI_BUFFER_SIZE 1024

#define ROOT_DIR "www"

// URI_BUFFER_SIZE - strlen(ROOT_DIR)
#define WS_PATH_BUFFER_SIZE 1021

#define WS_CHILD_TIMEOUT 1000

// Request Methods
#define REQ_METHOD_GET 1
#define REQ_METHOD_HEAD 2
#define REQ_METHOD_OPTIONS 3
#define REQ_METHOD_TRACE 4
#define REQ_METHOD_PUT 5
#define REQ_METHOD_DELETE 6
#define REQ_METHOD_POST 7
#define REQ_METHOD_PATCH 8
#define REQ_METHOD_CONNECT 9

// Request Errors
#define REQ_ERROR_METHOD_PARSE 255
#define REQ_ERROR_URI_PARSE 254
#define REQ_ERROR_URI_SIZE 253
#define REQ_ERROR_VERSION_PARSE 252

#define REQ_ERROR_HEADERS_PARSE 251

#define REQ_ERROR 250

// Request Versions
#define REQ_VERSION_1_0 1
#define REQ_VERSION_1_1 2
#define REQ_VERSION_2_0 3

typedef struct {
    uint8_t method;
    uint8_t version;
    char uri[WS_URI_BUFFER_SIZE];
} HttpRequestLine;

#define REQ_CONNECTION_KEEP_ALIVE 1
#define REQ_CONNECTION_CLOSE 2

typedef struct {
    int connection;
} HttpHeaders;

typedef struct {
    HttpRequestLine line;
    HttpHeaders headers;
} HttpRequest;

/* Creates a WsRequest from some char*.
 * If WsRequest cannot be created then method will be set to the
 * correct error.
 *
 * If no error 'uri' will have a '\0' at the end for use of str*() funcitons.
 */
HttpRequestLine HttpRequestLine_create(const char from[WS_BUFFER_SIZE]);

HttpRequest HttpRequest_create(const char from[WS_BUFFER_SIZE]);

/* Translates the extention type to mime type.
 *
 * If error will return empty ""
 */
const char* get_content_type(const char* path);

/* TODO: this should be in a configuration specifies by the server user.
 *
 * Hardcodes uri translations like / -> /index.html
 * Also adds www to the front of uri
 *
 * modifies uri in place.
 */
int uri_to_path(char uri[WS_URI_BUFFER_SIZE]);

String get_response(HttpRequest* req);

int headers_connection_parse(const char* from, size_t max_len);

typedef struct {
    int result;
    size_t length;
} FileInfo;

FileInfo FileInfo_create(const char* sanitized_uri);

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addrlen;
} Address;

struct sockaddr* Address_sockaddr(Address* a);

// returns socket file descriptor and fills address with bound address.
int bind_socket(const char* addr, const char* port, Address* address_o);

#endif
