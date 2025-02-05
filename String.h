#ifndef NBH_STRING
#define NBH_STRING

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} String;

String String_new();
void String_free(String* s);

// bounds checked access
char String_get(const String* s, size_t index);
void String_set(String* s, size_t index, char c);

void String_push_cstr(String* s, const char* cstr);

#endif
