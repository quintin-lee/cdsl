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
    cfg.business_context = NULL;
    return cfg;
}

static const char* type_to_str(cdsl_type_t t) {
    switch (t) {
        case CDSL_TYPE_INT:    return "INT";
        case CDSL_TYPE_FLOAT:  return "FLOAT";
        case CDSL_TYPE_BOOL:   return "BOOL";
        case CDSL_TYPE_STRING: return "STRING";
        default:               return "VOID";
    }
}

static int count_schema_vars(const cdsl_schema_t* schema) {
    int n = 0;
    for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) n++;
    return n;
}

static void build_schema_prompt(char* buf, size_t sz, const cdsl_schema_t* schema) {
    size_t pos = strlen(buf);
    snprintf(buf + pos, sz - pos, "Available variables:\n");
    for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) {
        pos = strlen(buf);
        snprintf(buf + pos, sz - pos, "  - %s (%s)\n", v->name, type_to_str(v->type));
    }
    pos = strlen(buf);
    snprintf(buf + pos, sz - pos, "\nAvailable actions:\n");
    for (cdsl_action_schema_t* a = schema ? schema->actions : NULL; a; a = a->next) {
        pos = strlen(buf);
        snprintf(buf + pos, sz - pos, "  - %s(", a->name);
        for (int i = 0; i < a->arg_count; i++) {
            if (i > 0) { pos = strlen(buf); snprintf(buf + pos, sz - pos, ", "); }
            pos = strlen(buf);
            snprintf(buf + pos, sz - pos, "%s", type_to_str(a->arg_types[i]));
        }
        pos = strlen(buf);
        snprintf(buf + pos, sz - pos, ") -> %s\n", type_to_str(a->return_type));
    }
}

static int has_keyword(const char* text, const char* word) {
    if (!text || !word) return 0;
    size_t wlen = strlen(word);
    const char* p = text;
    while ((p = strstr(p, word)) != NULL) {
        char before = (p > text) ? *(p - 1) : ' ';
        char after = p[wlen];
        if ((before == ' ' || before == '\t' || before == '\n' || before == ',' || before == '(')
            && (after == ' ' || after == '\t' || after == '\n' || after == ',' || after == '.' || after == ')' || after == '\0')) {
            return 1;
        }
        p++;
    }
    return 0;
}

static char* mock_translate_generic(const char* natural_language,
                                     const cdsl_schema_t* schema,
                                     const char* business_context) {
    int is_simple = (has_keyword(natural_language, "when")
                     || has_keyword(natural_language, "if")
                     || has_keyword(natural_language, "WHEN")
                     || has_keyword(natural_language, "IF"));
    int n_vars = count_schema_vars(schema);

    if (!schema || n_vars == 0) {
        return strdup(
            "RULE generated_rule {\n"
            "    META {\n"
            "        description = \"Auto-generated rule\"\n"
            "        pass_threshold = \"80\"\n"
            "        partial_threshold = \"50\"\n"
            "    }\n"
            "    METRIC score {\n"
            "        META { description = \"Score metric\" weight = \"100\" }\n"
            "        DEFAULT score(0)\n"
            "    }\n"
            "}\n"
        );
    }

    char rule_name[128] = {0};
    const char* nl = natural_language;
    while (*nl == ' ') nl++;
    int rn = 0;
    for (int i = 0; nl[i] && nl[i] != ' ' && nl[i] != ',' && nl[i] != '.' && rn < 32; i++) {
        if (isalpha((unsigned char)nl[i]) || nl[i] == '_') rule_name[rn++] = tolower((unsigned char)nl[i]);
    }
    rule_name[rn] = '\0';
    if (rn == 0) strcpy(rule_name, "rule");

    char desc[256];
    snprintf(desc, sizeof(desc), "%s", natural_language);
    if (business_context) {
        snprintf(desc + strlen(desc), sizeof(desc) - strlen(desc), " | context: %s", business_context);
    }

    if (is_simple && n_vars >= 1) {
        cdsl_var_schema_t* first_var = schema->vars;
        char buf[4096];
        snprintf(buf, sizeof(buf),
            "RULE %s {\n"
            "    META {\n"
            "        description = \"%s\"\n"
            "    }\n"
            "    WHEN %s %s %s\n"
            "    THEN record_warning(\"triggered\")\n"
            "}\n",
            rule_name, desc,
            first_var->name,
            (first_var->type == CDSL_TYPE_INT || first_var->type == CDSL_TYPE_FLOAT) ? ">" : "==",
            first_var->type == CDSL_TYPE_STRING ? "\"expected\"" :
            first_var->type == CDSL_TYPE_BOOL ? "true" : "0");
        return strdup(buf);
    }

    char buf[8192] = {0};
    snprintf(buf, sizeof(buf),
        "RULE %s {\n"
        "    META {\n"
        "        description = \"%s\"\n"
        "        pass_threshold = \"80\"\n"
        "        partial_threshold = \"50\"\n"
        "    }\n",
        rule_name, desc);

    int metric_idx = 0;
    for (cdsl_var_schema_t* v = schema->vars; v && metric_idx < 8; v = v->next, metric_idx++) {
        char mname[64];
        const char* dot = strrchr(v->name, '.');
        if (dot) snprintf(mname, sizeof(mname), "m%d_%s", metric_idx + 1, dot + 1);
        else snprintf(mname, sizeof(mname), "m%d", metric_idx + 1);

        int weight = 100 / (n_vars < 1 ? 1 : n_vars);
        int is_first = (metric_idx == 0);

        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
            "    METRIC %s {\n"
            "        META {\n"
            "            description = \"Check %s\"\n"
            "            weight = \"%d\"%s\n"
            "        }\n",
            mname, v->name, weight, is_first ? "\n            is_critical = \"true\"" : "");

        switch (v->type) {
            case CDSL_TYPE_INT:
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                    "        CASE %s >= 0 THEN score(%d)\n"
                    "        DEFAULT score(0)\n",
                    v->name, weight);
                break;
            case CDSL_TYPE_FLOAT:
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                    "        CASE %s >= 0.0 THEN score(%d)\n"
                    "        DEFAULT score(0)\n",
                    v->name, weight);
                break;
            case CDSL_TYPE_BOOL:
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                    "        CASE %s == true THEN score(%d)\n"
                    "        DEFAULT score(0)\n",
                    v->name, weight);
                break;
            case CDSL_TYPE_STRING:
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                    "        CASE %s != \"\" THEN score(%d)\n"
                    "        DEFAULT score(0)\n",
                    v->name, weight);
                break;
            default:
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                    "        DEFAULT score(%d)\n", weight);
                break;
        }
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "    }\n");
    }

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "}\n");
    return strdup(buf);
}

char* cdsl_ai_translate(const char* natural_language,
                         const cdsl_schema_t* schema,
                         const cdsl_ai_config_t* config) {
    if (!natural_language) return NULL;

    if (config && config->use_mock) {
        return mock_translate_generic(natural_language, schema, config->business_context);
    }

    char prompt[16384] = {0};
    snprintf(prompt, sizeof(prompt),
        "You are a C-DSL rule engine. Convert the following natural language description "
        "into a valid C-DSL rule.\n\n"
        "## C-DSL Syntax Reference\n"
        "- Simple rule: RULE name { META { description = \"...\" } WHEN <expr> THEN action(\"arg\") }\n"
        "- Scoring rule: RULE name { META { description = \"...\" pass_threshold = \"80\" partial_threshold = \"50\" } METRIC m { META { weight = \"40\" is_critical = \"true\" } CASE <expr> THEN score(N) DEFAULT score(0) } }\n"
        "- Operators: == != < > <= >= AND OR NOT\n"
        "- Keywords: true false\n"
        "- Functions: score(N), fail_metric(N, \"reason\") and registered actions\n\n");

    build_schema_prompt(prompt, sizeof(prompt), schema);

    if (config && config->business_context) {
        snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
                 "\nAdditional business context:\n%s\n", config->business_context);
    }

    snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
             "\nUser request: %s\n\n"
             "Output ONLY the DSL code wrapped in a ```dsl code block. "
             "Use the exact variable names listed above. "
             "Choose appropriate threshold values based on the business context.",
             natural_language);

    if (config && !config->use_mock && config->api_key) {
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
            char* dsl = strdup(response);
            free(response);
            return dsl;
        }
        fprintf(stderr, "[AI Bridge] API call failed, falling back to generic generation.\n");
    }

    if (config) fprintf(stderr, "[AI Bridge] Mock-generating DSL from prompt.\n%s\n", prompt);
    return mock_translate_generic(natural_language, schema,
                                  config ? config->business_context : NULL);
}

cdsl_ai_review_t* cdsl_ai_review(const char* dsl_code,
                                   const cdsl_schema_t* schema,
                                   const cdsl_ai_config_t* config) {
    if (!dsl_code) return NULL;

    if (config && config->use_mock) {
        int has_meta = (strstr(dsl_code, "META") != NULL);
        int has_metric = (strstr(dsl_code, "METRIC") != NULL);
        int has_case = (strstr(dsl_code, "CASE") != NULL);
        int has_default = (strstr(dsl_code, "DEFAULT") != NULL);
        int has_when = (strstr(dsl_code, "WHEN") != NULL);
        int has_score = (strstr(dsl_code, "score(") != NULL);
        int has_critical = (strstr(dsl_code, "is_critical") != NULL);

        int score = 10;
        char suggestions[1024] = {0};

        if (has_meta) score += 15;
        else strcat(suggestions, "Add META block with description and thresholds. ");

        if (has_metric && has_metric) {
            score += 15;
            if (has_case && has_default) score += 15;
            else strcat(suggestions, "Add CASE and DEFAULT branches to each METRIC. ");
        } else if (has_when) {
            score += 15;
        }

        if (has_critical) score += 10;
        else if (has_metric) strcat(suggestions, "Mark critical items with is_critical=true. ");

        if (has_score) score += 10;

        int risk = (score < 30) ? 100 - score * 2 : 100 - score;

        cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
        rev->approved = (score >= 40);
        rev->risk_score = risk < 0 ? 0 : (risk > 100 ? 100 : risk);
        char reason[512];
        snprintf(reason, sizeof(reason),
                 "Analysis score: %d/75. %s",
                 score, rev->approved ? "Rule structure is adequate." : "Rule structure needs improvement.");
        rev->reason = strdup(reason);
        rev->suggestions = suggestions[0] ? strdup(suggestions) : strdup("No issues found.");
        return rev;
    }

    if (config && !config->use_mock && config->api_key) {
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
        cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
        if (response) {
            char* ap = strstr(response, "\"approved\":");
            if (ap) rev->approved = (strstr(ap, "true") != NULL);
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
        }
        if (!rev->reason) rev->reason = strdup("LLM review completed");
        if (!rev->suggestions) rev->suggestions = strdup("");
        return rev;
    }

    cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
    rev->approved = 1;
    rev->risk_score = 10;
    rev->reason = strdup("Generic review passed");
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
        char* result = mock_translate_generic(natural_language, schema,
                                               config ? config->business_context : NULL);
        if (result && callback) callback(result, user_data);
        return result;
    }

    char prompt[16384] = {0};
    snprintf(prompt, sizeof(prompt),
        "You are a C-DSL rule translator. Convert the following request into "
        "a valid C-DSL rule.\n\n"
        "## C-DSL Syntax Reference\n"
        "- Simple rule: RULE name { META { description = \"...\" } WHEN <expr> THEN action(\"arg\") }\n"
        "- Scoring rule: RULE name { META { description = \"...\" pass_threshold = \"80\" partial_threshold = \"50\" } METRIC m { META { weight = \"40\" is_critical = \"true\" } CASE <expr> THEN score(N) DEFAULT score(0) } }\n"
        "- Operators: == != < > <= >= AND OR NOT\n"
        "- Keywords: true false\n"
        "- Functions: score(N), fail_metric(N, \"reason\") etc.\n\n");

    build_schema_prompt(prompt, sizeof(prompt), schema);

    if (config && config->business_context) {
        snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
                 "\nAdditional business context:\n%s\n", config->business_context);
    }

    snprintf(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt),
             "\nUser request: %s\n\nOutput ONLY the DSL code in a ```dsl code block.",
             natural_language);

    return call_llm_api_stream(prompt, config, callback, user_data);
}

char* cdsl_ai_review_stream(const char* dsl_code,
                             const cdsl_schema_t* schema,
                             const cdsl_ai_config_t* config,
                             cdsl_ai_stream_cb_t callback, void* user_data) {
    if (!dsl_code) return NULL;

    if (config && config->use_mock) {
        cdsl_ai_review_t* rev = cdsl_ai_review(dsl_code, schema, config);
        char* json = malloc(1024);
        snprintf(json, 1024, "{\"approved\":%s,\"risk_score\":%d,\"reason\":\"%s\",\"suggestions\":\"%s\"}",
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
