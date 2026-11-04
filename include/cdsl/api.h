/**
 * @file cdsl/api.h
 * @brief Unified API umbrella header for the C-DSL Rule Engine.
 *
 * Include this single header to access all public C-DSL APIs:
 * VM, schema, context, ruleset, AST, execution, report, visualization,
 * cache, codegen, and the simplified context-handle API (cdsl_ctx_t,
 * cdsl_set(), cdsl_get(), cdsl_ctx_* convenience functions).
 *
 * @defgroup cdsl_api Unified API
 * @{
 */
#ifndef CDSL_API_H
#define CDSL_API_H

#include "cdsl/vm.h"
#include "cdsl/schema.h"
#include "cdsl/context.h"
#include "cdsl/ruleset.h"
#include "cdsl/ast.h"
#include "cdsl/execution.h"
#include "cdsl/report.h"
#include "cdsl/visual.h"
#include "cdsl/cache.h"
#include "cdsl/codegen.h"

#endif
/** @} */
