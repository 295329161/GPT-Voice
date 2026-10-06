#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    const char *name, *description, *parameter;
    bool required;
    char *(*execute)(const char *argument);
} terminal_tool_t;
const terminal_tool_t *terminal_tools(size_t *count);
char *terminal_tool_execute(const char *name, const char *json_arguments);
