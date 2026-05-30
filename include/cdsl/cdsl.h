/**
 * @file cdsl.h
 * @brief Umbrella header for the C-DSL Rule Engine.
 *
 * @defgroup cdsl C-DSL Rule Engine
 * @{
 */
#ifndef CDSL_CDSL_H
#define CDSL_CDSL_H

/* Infrastructure */
#include "cdsl/util/arena.h"
#include "cdsl/util/error.h"
#include "cdsl/util/hashmap.h"
#include "cdsl/util/json.h"

/* Core schema & AST */
#include "cdsl/schema.h"
#include "cdsl/ast.h"

/* Execution sub-modules */
#include "cdsl/context.h"
#include "cdsl/vm.h"
#include "cdsl/report.h"
#include "cdsl/cache.h"
#include "cdsl/ruleset.h"
#include "cdsl/codegen.h"
#include "cdsl/visual.h"

/* AI bridge */
#include "cdsl/ai.h"

#endif
/** @} */
