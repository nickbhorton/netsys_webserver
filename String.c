#include "String.h"
#include <stdlib.h>
#include <string.h>

#define INIT_MALLOC 64

String String_new()
{
    String s = {.data = NULL, .len = 0, .cap = 0};
    return s;
}

void String_free(String* s)
{
    if (s->data != NULL) {
        free(s->data);
        s->data = NULL;
    }
    s->cap = 0;
    s->len = 0;
}

void String_push_back(String* s, char c)
{
    if (s->data == NULL) {
        s->data = malloc(INIT_MALLOC);
        s->cap = INIT_MALLOC;
        s->len = 0;
    } else {
        if (s->cap < s->len + 1) {
            s->cap = s->cap * 2 + 1;
            s->data = realloc(s->data, s->cap);
        }
    }
    s->len++;
    String_set(s, s->len - 1, c);
}

char String_get(const String* s, size_t index)
{
    if (index < s->len) {
        return s->data[index];
    }
    fprintf(stderr, "String_get out of bound\n");
    return 0;
}

void String_set(String* s, size_t index, char c)
{
    if (index < s->len) {
        s->data[index] = c;
        return;
    }
    fprintf(stderr, "String_set out of bound\n");
}

void String_push_cstr(String* s, const char* cstr)
{
    size_t len = strlen(cstr);
    for (size_t i = 0; i < len; i++) {
        String_push_back(s, cstr[i]);
    }
}
