#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

#include "server.h"

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
    char req_buffer[2048];
    memset(req_buffer, 0, 2048);
    for (size_t i = 0; i < 9; i++) {
        {
            memcpy(req_buffer, methods[i], strlen(methods[i]));
            memcpy(req_buffer + strlen(methods[i]), " / ", 3);
            memcpy(req_buffer + strlen(methods[i]) + 3, "HTTP/1.1\r\n", 10);
            WsRequest wreq = WsRequest_create(req_buffer);
            CU_ASSERT(wreq.method == method_nums[i]);
            CU_ASSERT(wreq.version == REQ_VERSION_1_1);
            CU_ASSERT_FALSE(strncmp(wreq.uri, "/\0", 2));
            memset(req_buffer, 0, 2048);
        }
    }
}

int main()
{
    CU_initialize_registry();
    CU_pSuite suite = CU_add_suite("WsRequestTestSuite", 0, 0);
    CU_add_test(suite, "happy case", test_happy);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return 0;
}
