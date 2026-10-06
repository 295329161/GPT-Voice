#pragma once
#include "cJSON.h"
cJSON *search_rpc_reply(const char *body, int id);
char *search_evidence(cJSON *result);
// Caller frees both returned briefing and screen-only sources.
char *search_briefing(const char *evidence, const char *required_date, char **sources);
