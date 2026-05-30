/**
 * @file cdsl/codegen.h
 * @brief C code generation from DSL rules.
 *
 * @defgroup cdsl_codegen Code Generation
 * @{
 */
#ifndef CDSL_CODEGEN_H
#define CDSL_CODEGEN_H

#include "cdsl/ast.h"
#include "cdsl/schema.h"

char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema);
int cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath);

#endif
/** @} */
