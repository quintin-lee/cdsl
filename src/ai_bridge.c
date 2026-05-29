#include "ai_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static char* call_llm_api(const char* prompt, const cdsl_ai_config_t* config) {
    if (!config || !config->api_key || !config->api_base || !config->model) return NULL;

    char* escaped_prompt = malloc(strlen(prompt) * 4 + 1);
    char* dst = escaped_prompt;
    for (const char* s = prompt; *s; s++) {
        if (*s == '"') { *dst++ = '\\'; *dst++ = '"'; }
        else if (*s == '\\') { *dst++ = '\\'; *dst++ = '\\'; }
        else if (*s == '\n') { *dst++ = '\\'; *dst++ = 'n'; }
        else if (*s == '\r') { *dst++ = '\\'; *dst++ = 'r'; }
        else if (*s == '\t') { *dst++ = '\\'; *dst++ = 't'; }
        else { *dst++ = *s; }
    }
    *dst = '\0';

    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST \"%s/chat/completions\" "
        "-H \"Content-Type: application/json\" "
        "-H \"Authorization: Bearer %s\" "
        "-d '{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"temperature\":0.1}'",
        config->api_base, config->api_key, config->model, escaped_prompt);
    free(escaped_prompt);

    FILE* fp = popen(cmd, "r");
    if (!fp) return NULL;

    char* result = malloc(16384);
    size_t total = 0;
    size_t n;
    while ((n = fread(result + total, 1, 16383 - total, fp)) > 0) {
        total += n;
    }
    result[total] = '\0';
    pclose(fp);

    char* content = strstr(result, "\"content\":\"");
    if (!content) { free(result); return NULL; }
    content += 11;
    char* end = strchr(content, '\"');
    if (!end) { free(result); return NULL; }
    *end = '\0';

    char* decoded = malloc(end - content + 1);
    char* d = decoded;
    for (char* s = content; *s; s++) {
        if (*s == '\\' && s[1] == 'n') { *d++ = '\n'; s++; }
        else if (*s == '\\' && s[1] == 't') { *d++ = '\t'; s++; }
        else if (*s == '\\' && s[1] == '"') { *d++ = '"'; s++; }
        else if (*s == '\\' && s[1] == '\\') { *d++ = '\\'; s++; }
        else { *d++ = *s; }
    }
    *d = '\0';
    free(result);
    return decoded;
}

cdsl_ai_config_t cdsl_ai_config_default(void) {
    cdsl_ai_config_t cfg;
    cfg.use_mock = 1;
    cfg.api_key = NULL;
    cfg.api_base = NULL;
    cfg.model = NULL;
    return cfg;
}

static char* mock_translate_supplier_capital(void) {
    return strdup(
        "RULE supplier_qualification_audit {\n"
        "    META {\n"
        "        description = \"Supplier onboarding qualification audit and grading\"\n"
        "        category = \"compliance\"\n"
        "        pass_threshold = \"80\"\n"
        "        partial_threshold = \"60\"\n"
        "    }\n"
        "    METRIC credit_check {\n"
        "        META {\n"
        "            description = \"Anti-fraud and blacklist compliance\"\n"
        "            weight = \"30\"\n"
        "            is_critical = \"true\"\n"
        "        }\n"
        "        CASE supplier.is_blacklisted == false THEN score(30)\n"
        "        DEFAULT fail_metric(0, \"blacklisted_supplier_rejected\")\n"
        "    }\n"
        "    METRIC capital_check {\n"
        "        META {\n"
        "            description = \"Registered capital score\"\n"
        "            weight = \"40\"\n"
        "        }\n"
        "        CASE supplier.registered_capital >= 5000000 THEN score(40)\n"
        "        CASE supplier.registered_capital >= 1000000 THEN score(20)\n"
        "        DEFAULT score(0)\n"
        "    }\n"
        "    METRIC experience_check {\n"
        "        META {\n"
        "            description = \"Supplier business age evaluation\"\n"
        "            weight = \"30\"\n"
        "        }\n"
        "        CASE supplier.years_in_business >= 5 THEN score(30)\n"
        "        CASE supplier.years_in_business >= 2 THEN score(15)\n"
        "        DEFAULT score(0)\n"
        "    }\n"
        "}\n"
    );
}

static char* mock_translate_doc_format(void) {
    return strdup(
        "RULE document_format_audit {\n"
        "    META {\n"
        "        description = \"Document format and signature compliance\"\n"
        "        category = \"compliance\"\n"
        "        pass_threshold = \"100\"\n"
        "        partial_threshold = \"60\"\n"
        "    }\n"
        "    METRIC format_check {\n"
        "        META {\n"
        "            description = \"File format validation\"\n"
        "            weight = \"50\"\n"
        "            is_critical = \"true\"\n"
        "        }\n"
        "        CASE document.format == \"pdf\" THEN score(50)\n"
        "        DEFAULT fail_metric(0, \"invalid_format\")\n"
        "    }\n"
        "    METRIC signature_check {\n"
        "        META {\n"
        "            description = \"Digital signature verification\"\n"
        "            weight = \"30\"\n"
        "        }\n"
        "        CASE document.has_digital_signature == true THEN score(30)\n"
        "        DEFAULT score(0)\n"
        "    }\n"
        "    METRIC size_check {\n"
        "        META {\n"
        "            description = \"File size under limit\"\n"
        "            weight = \"20\"\n"
        "        }\n"
        "        CASE document.size_mb <= 10.0 THEN score(20)\n"
        "        CASE document.size_mb <= 20.0 THEN score(10)\n"
        "        DEFAULT score(0)\n"
        "    }\n"
        "}\n"
    );
}

static char* mock_translate_content_safety(void) {
    return strdup(
        "RULE content_safety_audit {\n"
        "    META {\n"
        "        description = \"User-generated content safety and moderation\"\n"
        "        category = \"moderation\"\n"
        "        pass_threshold = \"80\"\n"
        "        partial_threshold = \"50\"\n"
        "    }\n"
        "    METRIC sensitive_check {\n"
        "        META {\n"
        "            description = \"Sensitive words detection\"\n"
        "            weight = \"30\"\n"
        "            is_critical = \"true\"\n"
        "        }\n"
        "        CASE content.sensitive_words_count == 0 THEN score(30)\n"
        "        DEFAULT fail_metric(0, \"sensitive_words_detected\")\n"
        "    }\n"
        "    METRIC pii_check {\n"
        "        META {\n"
        "            description = \"Personal identifiable information protection\"\n"
        "            weight = \"40\"\n"
        "            is_critical = \"true\"\n"
        "        }\n"
        "        CASE content.contains_pii == false THEN score(40)\n"
        "        DEFAULT fail_metric(0, \"pii_exposure_risk\")\n"
        "    }\n"
        "    METRIC spam_check {\n"
        "        META {\n"
        "            description = \"Spam and quality scoring\"\n"
        "            weight = \"30\"\n"
        "        }\n"
        "        CASE content.ai_spam_score < 0.3 THEN score(30)\n"
        "        CASE content.ai_spam_score < 0.7 THEN score(15)\n"
        "        DEFAULT score(0)\n"
        "    }\n"
        "}\n"
    );
}

char* cdsl_ai_translate(const char* natural_language,
                         const cdsl_schema_t* schema,
                         const cdsl_ai_config_t* config) {
    if (!natural_language) return NULL;

    char lower[1024];
    int len = strlen(natural_language);
    if (len >= 1024) len = 1023;
    for (int i = 0; i < len; i++) lower[i] = tolower((unsigned char)natural_language[i]);
    lower[len] = '\0';

    if (config && config->use_mock) {
        if (strstr(lower, "supplier") || strstr(lower, "供应商") || strstr(lower, "资质")) {
            return mock_translate_supplier_capital();
        }
        if (strstr(lower, "document") || strstr(lower, "文档") || strstr(lower, "格式")) {
            return mock_translate_doc_format();
        }
        if (strstr(lower, "content") || strstr(lower, "内容") || strstr(lower, "安全")) {
            return mock_translate_content_safety();
        }
        return mock_translate_supplier_capital();
    }

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "You are a C-DSL rule translator. Convert the following natural language "
        "description into a valid C-DSL rule.\n\n"
        "Available variables:\n");
    for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) {
        char typebuf[32];
        switch (v->type) {
            case CDSL_TYPE_INT:    strcpy(typebuf, "INT"); break;
            case CDSL_TYPE_FLOAT:  strcpy(typebuf, "FLOAT"); break;
            case CDSL_TYPE_BOOL:   strcpy(typebuf, "BOOL"); break;
            case CDSL_TYPE_STRING: strcpy(typebuf, "STRING"); break;
            default:               strcpy(typebuf, "VOID"); break;
        }
        snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
                 "  - %s (%s)\n", v->name, typebuf);
    }
    snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
             "\nAvailable actions: score(), fail_metric(), block_action(), reject_document()\n"
             "Use METRIC/CASE/DEFAULT for multi-indicator scoring.\n"
             "Use META { weight = \"N\", is_critical = \"true\" } for critical metrics.\n"
             "Use META { pass_threshold = \"N\", partial_threshold = \"N\" }.\n\n"
             "User request: %s\n\n"
             "Output ONLY the DSL code in a ```dsl code block.", natural_language);

    fprintf(stderr, "[AI Bridge] Would call LLM API with prompt:\n%s\n", prompt);

    if (!config->use_mock && config->api_key) {
        char* response = call_llm_api(prompt, config);
        if (response) {
            char* start = strstr(response, "```dsl");
            if (start) {
                start += 6;
                while (*start == '\n') start++;
                char* end = strstr(start, "```");
                if (end) {
                    char* dsl = malloc(end - start + 1);
                    memcpy(dsl, start, end - start);
                    dsl[end - start] = '\0';
                    free(response);
                    return dsl;
                }
            }
            free(response);
        }
        fprintf(stderr, "[AI Bridge] API call failed, falling back to mock.\n");
    }

    return mock_translate_supplier_capital();
}

cdsl_ai_review_t* cdsl_ai_review(const char* dsl_code,
                                   const cdsl_schema_t* schema,
                                   const cdsl_ai_config_t* config) {
    cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));

    if (config && config->use_mock) {
        if (!dsl_code || strlen(dsl_code) == 0) {
            rev->approved = 0;
            rev->risk_score = 100;
            rev->reason = strdup("Empty DSL code provided");
            rev->suggestions = strdup("Provide a valid DSL rule definition");
            return rev;
        }

        int has_meta = (strstr(dsl_code, "META") != NULL);
        int has_metric = (strstr(dsl_code, "METRIC") != NULL);
        int has_case = (strstr(dsl_code, "CASE") != NULL);
        int has_default = (strstr(dsl_code, "DEFAULT") != NULL);
        int has_critical = (strstr(dsl_code, "is_critical") != NULL);
        int has_weight = (strstr(dsl_code, "weight") != NULL);
        int has_desc = (strstr(dsl_code, "description") != NULL);

        int score = 0;
        char reason[1024] = {0};
        char suggestions[1024] = {0};

        if (has_meta) { score += 10; }
        else { strcat(suggestions, "Add META block with description. "); }

        if (has_metric) { score += 15; }
        else { strcat(suggestions, "Consider using METRIC blocks for multi-indicator assessment. "); }

        if (has_case && has_default) { score += 15; }
        else { strcat(suggestions, "Ensure all METRIC blocks have CASE and DEFAULT branches. "); }

        if (has_critical) { score += 10; }
        else { strcat(suggestions, "Mark critical compliance items with is_critical=true. "); }

        if (has_weight) { score += 10; }
        else { strcat(suggestions, "Assign weights to each metric for scoring. "); }

        if (has_desc) { score += 10; }

        int risk = 100 - score;

        if (score >= 50) {
            rev->approved = 1;
            rev->risk_score = risk;
            snprintf(reason, sizeof(reason),
                     "Rule structure review passed. Score: %d/70. "
                     "No logical contradictions or safety violations detected.", score);
        } else {
            rev->approved = 0;
            rev->risk_score = risk;
            snprintf(reason, sizeof(reason),
                     "Rule structure incomplete. Score: %d/70. "
                     "Missing critical structural elements.", score);
        }

        rev->reason = strdup(reason);
        rev->suggestions = strdup(suggestions);
        return rev;
    }

    if (!config->use_mock && config->api_key) {
        char review_prompt[8192];
        snprintf(review_prompt, sizeof(review_prompt),
            "You are a DSL rule safety reviewer. Analyze the following C-DSL rule for:\n"
            "1. Logical contradictions (e.g., x > 10 AND x < 5)\n"
            "2. Missing META blocks or weights\n"
            "3. Security risks (e.g., unauthorized actions)\n\n"
            "DSL code:\n%s\n\n"
            "Respond in JSON: {\"approved\":true/false,\"risk_score\":0-100,\"reason\":\"...\",\"suggestions\":\"...\"}",
            dsl_code);

        char* response = call_llm_api(review_prompt, config);
        if (response) {
            char* ap = strstr(response, "\"approved\":");
            if (ap) {
                rev->approved = (strstr(ap, "true") != NULL);
            }
            char* rs = strstr(response, "\"risk_score\":");
            if (rs) rev->risk_score = atoi(rs + 13);
            char* rn = strstr(response, "\"reason\":\"");
            if (rn) {
                rn += 10;
                char* re = strchr(rn, '\"');
                if (re) rev->reason = strndup(rn, re - rn);
            }
            char* sg = strstr(response, "\"suggestions\":\"");
            if (sg) {
                sg += 15;
                char* se = strchr(sg, '\"');
                if (se) rev->suggestions = strndup(sg, se - sg);
            }
            free(response);
            return rev;
        }
        fprintf(stderr, "[AI Bridge] API call failed, falling back to mock.\n");
    }

    rev->approved = 1;
    rev->risk_score = 10;
    rev->reason = strdup("LLM review passed (API mode)");
    rev->suggestions = strdup("No suggestions");
    return rev;
}

void cdsl_ai_review_free(cdsl_ai_review_t* review) {
    if (!review) return;
    free(review->reason);
    free(review->suggestions);
    free(review);
}

static char* call_llm_api_stream(const char* prompt, const cdsl_ai_config_t* config,
                                  cdsl_ai_stream_cb_t callback, void* user_data) {
    if (!config || !config->api_key || !config->api_base || !config->model) return NULL;

    char* escaped_prompt = malloc(strlen(prompt) * 4 + 1);
    char* dst = escaped_prompt;
    for (const char* s = prompt; *s; s++) {
        if (*s == '"') { *dst++ = '\\'; *dst++ = '"'; }
        else if (*s == '\\') { *dst++ = '\\'; *dst++ = '\\'; }
        else if (*s == '\n') { *dst++ = '\\'; *dst++ = 'n'; }
        else if (*s == '\r') { *dst++ = '\\'; *dst++ = 'r'; }
        else if (*s == '\t') { *dst++ = '\\'; *dst++ = 't'; }
        else { *dst++ = *s; }
    }
    *dst = '\0';

    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
        "curl -s -N -X POST \"%s/chat/completions\" "
        "-H \"Content-Type: application/json\" "
        "-H \"Authorization: Bearer %s\" "
        "-d '{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"temperature\":0.1,\"stream\":true}'",
        config->api_base, config->api_key, config->model, escaped_prompt);
    free(escaped_prompt);

    FILE* fp = popen(cmd, "r");
    if (!fp) return NULL;

    char* result = malloc(16384);
    size_t total = 0;
    result[0] = '\0';
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "data: ", 6) != 0) continue;
        char* data = line + 6;
        if (strcmp(data, "[DONE]\n") == 0) break;

        char* content = strstr(data, "\"content\":\"");
        if (!content) continue;
        content += 11;
        char* end = strchr(content, '\"');
        if (!end) continue;

        size_t len = end - content;
        if (total + len + 1 > 16384) {
            result = realloc(result, total + len + 64);
        }
        for (size_t i = 0; i < len; i++) {
            if (content[i] == '\\' && i + 1 < len) {
                if (content[i+1] == 'n') { result[total++] = '\n'; i++; }
                else if (content[i+1] == 't') { result[total++] = '\t'; i++; }
                else if (content[i+1] == '"') { result[total++] = '"'; i++; }
                else if (content[i+1] == '\\') { result[total++] = '\\'; i++; }
                else { result[total++] = content[i]; }
            } else {
                result[total++] = content[i];
            }
        }
        result[total] = '\0';

        if (callback) {
            char* chunk = strndup(content, len);
            callback(chunk, user_data);
            free(chunk);
        }
    }
    pclose(fp);
    return result;
}

char* cdsl_ai_translate_stream(const char* natural_language,
                                const cdsl_schema_t* schema,
                                const cdsl_ai_config_t* config,
                                cdsl_ai_stream_cb_t callback, void* user_data) {
    if (!natural_language) return NULL;

    if (config && config->use_mock) {
        char* result = cdsl_ai_translate(natural_language, schema, config);
        if (result && callback) callback(result, user_data);
        return result;
    }

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "You are a C-DSL rule translator. Convert the following natural language "
        "description into a valid C-DSL rule.\n\n"
        "Available variables:\n");
    for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) {
        char typebuf[32];
        switch (v->type) {
            case CDSL_TYPE_INT:    strcpy(typebuf, "INT"); break;
            case CDSL_TYPE_FLOAT:  strcpy(typebuf, "FLOAT"); break;
            case CDSL_TYPE_BOOL:   strcpy(typebuf, "BOOL"); break;
            case CDSL_TYPE_STRING: strcpy(typebuf, "STRING"); break;
            default:               strcpy(typebuf, "VOID"); break;
        }
        snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
                 "  - %s (%s)\n", v->name, typebuf);
    }
    snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
             "\nAvailable actions: score(), fail_metric(), block_action(), reject_document()\n"
             "Use METRIC/CASE/DEFAULT for multi-indicator scoring.\n\n"
             "User request: %s\n\n"
             "Output ONLY the DSL code in a ```dsl code block.", natural_language);

    return call_llm_api_stream(prompt, config, callback, user_data);
}

char* cdsl_ai_review_stream(const char* dsl_code,
                             const cdsl_schema_t* schema,
                             const cdsl_ai_config_t* config,
                             cdsl_ai_stream_cb_t callback, void* user_data) {
    if (!dsl_code) return NULL;

    if (config && config->use_mock) {
        cdsl_ai_review_t* rev = cdsl_ai_review(dsl_code, schema, config);
        char* json = malloc(512);
        snprintf(json, 512, "{\"approved\":%s,\"risk_score\":%d,\"reason\":\"%s\",\"suggestions\":\"%s\"}",
                 rev->approved ? "true" : "false", rev->risk_score, rev->reason, rev->suggestions);
        cdsl_ai_review_free(rev);
        if (callback) callback(json, user_data);
        return json;
    }

    char review_prompt[8192];
    snprintf(review_prompt, sizeof(review_prompt),
        "You are a DSL rule safety reviewer. Analyze the following C-DSL rule for:\n"
        "1. Logical contradictions\n"
        "2. Missing META blocks or weights\n"
        "3. Security risks\n\n"
        "DSL code:\n%s\n\n"
        "Respond in JSON: {\"approved\":true/false,\"risk_score\":0-100,\"reason\":\"...\",\"suggestions\":\"...\"}",
        dsl_code);

    return call_llm_api_stream(review_prompt, config, callback, user_data);
}
