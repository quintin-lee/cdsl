/**
 * @file cdsl.h
 * @brief Umbrella header for the C-DSL Rule Engine.
 *
 * Include this single header to access the entire public API.
 * Individual module headers can also be included separately.
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

/* Core modules */
#include "cdsl/schema.h"
#include "cdsl/ast.h"
#include "cdsl/vm.h"
#include "cdsl/context.h"
#include "cdsl/report.h"
#include "cdsl/cache.h"
#include "cdsl/ruleset.h"
#include "cdsl/codegen.h"
#include "cdsl/visual.h"
#include "cdsl/ai.h"

#endif
/** @} */
