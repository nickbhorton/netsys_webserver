#ifndef NBH_COMMON_HEADER
#define NBH_COMMON_HEADER

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "String.h"

#define WS_PATH_BUFFER_SIZE 1024
#define WS_BUFFER_SIZE 2048

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
#define REQ_ERROR_BUFFER_OVERFLOW 251
#define REQ_ERROR 250

// Request Versions
#define REQ_VERSION_1_0 1
#define REQ_VERSION_1_1 2
#define REQ_VERSION_2_0 3

typedef struct {
    uint8_t method;
    uint8_t version;
    char uri[WS_PATH_BUFFER_SIZE];
} WsRequest;

/* Creates a WsRequest from some char*.
 * If WsRequest cannot be created then method will be set to the
 * correct error.
 *
 * If no error 'uri' will have a '\0' at the end for use of str*() funcitons.
 */
WsRequest WsRequest_create(const char* from);

/* Translates the extention type to mime type.
 *
 * If error will return empty ""
 */
const char* get_content_type(const char* uri);

/* TODO: this should be in a configuration specifies by the server user.
 *
 * Hardcodes uri translations like / -> /index.html
 */
const char* map_specific_uris(const char* uri);

String get_response(const WsRequest* req);

typedef struct {
    bool valid;
    size_t length;
} FileInfo;

FileInfo FileInfo_create(const char* filename);

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addrlen;
} Address;

struct sockaddr* Address_sockaddr(Address* a);

// returns socket file descriptor and fills address with bound address.
int bind_socket(const char* addr, const char* port, Address* address_o);

#endif
