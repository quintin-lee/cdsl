#ifndef CDSL_JSON_H
#define CDSL_JSON_H

typedef struct cdsl_json_value {
    char* key;
    enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_OBJECT, JSON_ARRAY } type;
    union {
        int bool_val;
        double number_val;
        char* string_val;
        struct { struct cdsl_json_value* items; int count; } object;
        struct { struct cdsl_json_value* items; int count; } array;
    } value;
    struct cdsl_json_value* next;
} cdsl_json_value_t;

cdsl_json_value_t* cdsl_json_parse(const char* json);
void cdsl_json_free(cdsl_json_value_t* val);

#endif
