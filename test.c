#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

#include "server.h"

#define FILE_COUNT 2

void test_happy(void)
{
    const char methods[9][8] = {
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
    const int method_nums[9] = {
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
    const char version[3][16] = {
        "HTTP/1.0",
        "HTTP/1.1",
        "HTTP/2.0",
    };
    const int version_nums[3] = {
        REQ_VERSION_1_0,
        REQ_VERSION_1_1,
        REQ_VERSION_2_0,
    };
    const char uri_in[FILE_COUNT][64] = {
        " / ",
        " /index.html ",
    };
    const char uri_out[FILE_COUNT][64] = {"/\0", "/index.html\0"};

    char req_buffer[2048];
    memset(req_buffer, 0, 2048);
    for (size_t i = 0; i < 9; i++) {
        for (size_t j = 0; j < 3; j++) {
            for (size_t k = 0; k < FILE_COUNT; k++) {
                memcpy(req_buffer, methods[i], strlen(methods[i]));
                memcpy(
                    req_buffer + strlen(methods[i]),
                    uri_in[k],
                    strlen(uri_in[k])
                );
                memcpy(
                    req_buffer + strlen(methods[i]) + strlen(uri_in[k]),
                    version[j],
                    strlen(version[j])
                );
                memcpy(
                    req_buffer + strlen(methods[i]) + strlen(uri_in[k]) +
                        strlen(version[j]),
                    "\r\n",
                    2
                );
                WsRequest wreq = WsRequest_create(req_buffer);
                CU_ASSERT(wreq.method == method_nums[i]);
                CU_ASSERT(wreq.version == version_nums[j]);
                CU_ASSERT_FALSE(
                    strncmp(wreq.uri, uri_out[k], strlen(uri_out[k]))
                );
                memset(req_buffer, 0, 2048);
            }
        }
    }
}

void test_extra_whitespace(void)
{
    const char methods[9][16] = {
        "  GET ",
        "  HEAD ",
        "  OPTIONS ",
        " TRACE ",
        "   PUT ",
        " DELETE ",
        "POST  ",
        " PATCH ",
        "  CONNECT"
    };
    const int method_nums[9] = {
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
    const char version[3][16] = {
        "   HTTP/1.0 ",
        "HTTP/1.1   ",
        "   HTTP/2.0",
    };
    const int version_nums[3] = {
        REQ_VERSION_1_0,
        REQ_VERSION_1_1,
        REQ_VERSION_2_0,
    };
    const char uri_in[FILE_COUNT][64] = {
        " /    ",
        "    /index.html    ",
    };
    const char uri_out[FILE_COUNT][64] = {"/\0", "/index.html\0"};

    char req_buffer[2048];
    memset(req_buffer, 0, 2048);
    for (size_t i = 0; i < 9; i++) {
        for (size_t j = 0; j < 3; j++) {
            for (size_t k = 0; k < FILE_COUNT; k++) {
                memcpy(req_buffer, methods[i], strlen(methods[i]));
                memcpy(
                    req_buffer + strlen(methods[i]),
                    uri_in[k],
                    strlen(uri_in[k])
                );
                memcpy(
                    req_buffer + strlen(methods[i]) + strlen(uri_in[k]),
                    version[j],
                    strlen(version[j])
                );
                const char* sentinal = "      \r\n     ";
                memcpy(
                    req_buffer + strlen(methods[i]) + strlen(uri_in[k]) +
                        strlen(version[j]),
                    sentinal,
                    strlen(sentinal)
                );
                WsRequest wreq = WsRequest_create(req_buffer);
                CU_ASSERT(wreq.method == method_nums[i]);
                CU_ASSERT(wreq.version == version_nums[j]);
                CU_ASSERT_FALSE(
                    strncmp(wreq.uri, uri_out[k], strlen(uri_out[k]))
                );
                memset(req_buffer, 0, 2048);
            }
        }
    }
}

void request_method_parse_error(void)
{
    WsRequest wreq = WsRequest_create(" GT / HTTP/1.1\r\n");
    CU_ASSERT(wreq.method == REQ_ERROR_METHOD_PARSE);
    CU_ASSERT(wreq.version == 0);
}

void request_version_parse_error(void)
{
    WsRequest wreq = WsRequest_create(" GET / HTP/1.1\r\n");
    CU_ASSERT(wreq.method == REQ_ERROR_VERSION_PARSE);
    CU_ASSERT(wreq.version == 0);
}

void request_uri_parse_error(void)
{
    WsRequest wreq = WsRequest_create("GET /HTTP/1.1\r\n");
    CU_ASSERT(wreq.method == REQ_ERROR_URI_PARSE);
    CU_ASSERT(wreq.version == 0);
}

void request_uri_size_error(void)
{
    char uri[WS_PATH_BUFFER_SIZE + 64];
    memcpy(uri, "GET /", strlen("GET /"));
    memset(uri + 5, 'a', WS_PATH_BUFFER_SIZE + 16);
    memcpy(
        uri + 4 + WS_PATH_BUFFER_SIZE + 16,
        " HTTP/1.1\r\n",
        strlen(" HTTP/1.1\r\n")
    );
    WsRequest wreq = WsRequest_create(uri);
    CU_ASSERT(wreq.method == REQ_ERROR_URI_SIZE);
    CU_ASSERT(wreq.version == 0);
}

void request_buffer_overflow_error(void)
{
    char uri[WS_BUFFER_SIZE + 2];
    memcpy(uri, "GET /", strlen("GET /"));
    memset(uri + 5, 'a', WS_BUFFER_SIZE - 5);
    WsRequest wreq = WsRequest_create(uri);
    CU_ASSERT(wreq.method == REQ_ERROR_BUFFER_OVERFLOW);
    CU_ASSERT(wreq.version == 0);
}

int main()
{
    CU_initialize_registry();
    CU_pSuite suite = CU_add_suite("WsRequestTestSuite", 0, 0);
    CU_add_test(suite, "happy case", test_happy);
    CU_add_test(suite, "extra whitespace", test_extra_whitespace);
    CU_add_test(
        suite,
        "method parsing error handling",
        request_method_parse_error
    );
    CU_add_test(
        suite,
        "version parsing error handling",
        request_version_parse_error
    );
    CU_add_test(suite, "uri parsing error handling", request_uri_parse_error);
    CU_add_test(suite, "uri size error handling", request_uri_size_error);
    CU_add_test(
        suite,
        "uri buffer overflow error handling",
        request_buffer_overflow_error
    );
    CU_basic_run_tests();
    CU_cleanup_registry();
    return 0;
}
