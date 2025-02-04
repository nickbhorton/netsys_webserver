#ifndef NBH_SERVER_HEADER
#define NBH_SERVER_HEADER

#include <stddef.h>
#include <stdint.h>

#define WS_PATH_BUFFER_SIZE 1024

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

// Request Versions
#define REQ_VERSION_1_0 1
#define REQ_VERSION_1_1 2

typedef struct {
    uint8_t method;
    uint8_t version;
    char uri[WS_PATH_BUFFER_SIZE];
} WsRequest;

/* creates a WsRequest from some char*. Requires "\r\n" to be included at the
 * end of the char* or else will segfault.
 * if WsRequest cannot be created then method will be set to the correct error
 * and WsRequest_valid will return that error
 */
WsRequest WsRequest_create(const char* from);

/* checks if a WsRequest is valid.
 * returns:
 *   0  if WsRequest is valid
 *   <0 on error
 */
int WsRequest_valid(const WsRequest* request);

#endif
