#ifndef CDSL_AI_BRIDGE_H
#define CDSL_AI_BRIDGE_H

#include "abstract.h"

typedef struct {
    int approved;
    int risk_score;
    char* reason;
    char* suggestions;
} cdsl_ai_review_t;

typedef struct {
    int use_mock;
    char* api_key;
    char* api_base;
    char* model;
} cdsl_ai_config_t;

cdsl_ai_config_t cdsl_ai_config_default(void);
char* cdsl_ai_translate(const char* natural_language,
                         const cdsl_schema_t* schema,
                         const cdsl_ai_config_t* config);
cdsl_ai_review_t* cdsl_ai_review(const char* dsl_code,
                                   const cdsl_schema_t* schema,
                                   const cdsl_ai_config_t* config);
void cdsl_ai_review_free(cdsl_ai_review_t* review);

#endif
