#ifndef CDSL_ERROR_H
#define CDSL_ERROR_H

typedef enum {
    CDSL_ERR_SYNTAX,
    CDSL_ERR_TYPE,
    CDSL_ERR_SEMANTIC,
    CDSL_ERR_RUNTIME
} cdsl_error_kind_t;

typedef struct cdsl_error {
    int line;
    int column;
    char* message;
    char* hint;
    cdsl_error_kind_t kind;
} cdsl_error_t;

cdsl_error_t* cdsl_error_create(cdsl_error_kind_t kind, int line, int column,
                                  const char* message, const char* hint);
void cdsl_error_free(cdsl_error_t* err);
void cdsl_error_print(const cdsl_error_t* err);

typedef struct cdsl_error_list {
    cdsl_error_t** errors;
    int count;
    int capacity;
} cdsl_error_list_t;

cdsl_error_list_t* cdsl_error_list_create(void);
void cdsl_error_list_free(cdsl_error_list_t* list);
void cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err);
int cdsl_error_list_has_errors(const cdsl_error_list_t* list);
void cdsl_error_list_print(const cdsl_error_list_t* list);

#endif
