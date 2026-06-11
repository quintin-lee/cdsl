/**
 * @file cdsl/visual.h
 * @brief Graphviz DOT visualization for rules and rulesets.
 *
 * @defgroup cdsl_visual Visualization
 * @{
 */
#ifndef CDSL_VISUAL_H
#define CDSL_VISUAL_H

#include "cdsl/util/portability.h"

#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/ruleset.h"

CDSL_NODISCARD
char* cdsl_rule_to_dot(const cdsl_rule_t* rule);
int cdsl_rule_to_dot_file(const cdsl_rule_t* rule, const char* filepath);
CDSL_NODISCARD
char* cdsl_ruleset_to_dot(const cdsl_ruleset_t* set);
int cdsl_ruleset_to_dot_file(const cdsl_ruleset_t* set, const char* filepath);

#endif
/** @} */
