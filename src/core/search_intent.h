#pragma once
#include <stdbool.h>
#include <stddef.h>
bool search_intent(const char *text);
void search_query(const char *text, const char *today, char *out, size_t capacity);
bool time_intent(const char *text);
