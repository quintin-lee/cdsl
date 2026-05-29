/**
 * @file ast.h
 * @brief Abstract Syntax Tree (AST) definitions for the C-DSL rule engine.
 *
 * This header defines all AST node types, enumeration constants, and
 * construction/destruction functions used by the parser and execution engine.
 *
 * @defgroup ast AST Layer
 * @{
 */

#ifndef CDSL_AST_H
#define CDSL_AST_H

#include <stddef.h>

/** @name Safety Limits */
/** @{ */
#define CDSL_MAX_EXPR_DEPTH 64	    /**< Maximum nesting depth of expressions */
#define CDSL_MAX_METRICS 32	    /**< Maximum number of metrics per rule */
#define CDSL_MAX_CASES 16	    /**< Maximum number of CASE branches per metric */
#define CDSL_MAX_RULES 128	    /**< Maximum number of rules in a ruleset */
#define CDSL_MAX_INPUT_LENGTH 65536 /**< Maximum DSL input string length in bytes */
/** @} */

/**
 * @brief Supported data types in the DSL type system.
 *
 * Used for schema registration, type checking, and runtime value representation.
 */
typedef enum {
	CDSL_TYPE_INT,	  /**< 32-bit integer */
	CDSL_TYPE_FLOAT,  /**< 64-bit floating point */
	CDSL_TYPE_BOOL,	  /**< Boolean (true/false) */
	CDSL_TYPE_STRING, /**< Null-terminated string */
	CDSL_TYPE_VOID	  /**< Void / no value */
} cdsl_type_t;

/**
 * @brief Binary and unary operators supported in DSL expressions.
 *
 * Comparison operators return BOOL. Logical operators support short-circuit evaluation.
 */
typedef enum {
	CDSL_OP_EQ,  /**< Equal to (==) */
	CDSL_OP_NE,  /**< Not equal to (!=) */
	CDSL_OP_LT,  /**< Less than (<) */
	CDSL_OP_GT,  /**< Greater than (>) */
	CDSL_OP_LE,  /**< Less than or equal (<=) */
	CDSL_OP_GE,  /**< Greater than or equal (>=) */
	CDSL_OP_AND, /**< Logical AND (&&) - short-circuit */
	CDSL_OP_OR,  /**< Logical OR (||) - short-circuit */
	CDSL_OP_NOT, /**< Logical NOT (!) */
	CDSL_OP_ADD, /**< Addition (+) */
	CDSL_OP_SUB, /**< Subtraction (-) */
	CDSL_OP_MUL, /**< Multiplication (*) */
	CDSL_OP_DIV, /**< Division (/) */
	CDSL_OP_NEG  /**< Unary negation (-expr) */
} cdsl_op_t;

/**
 * @brief Expression node types in the AST.
 *
 * Each expression node is one of these types, with data stored in the
 * corresponding union member of ::cdsl_expr_node_t.
 */
typedef enum {
	CDSL_EXPR_ID,	  /**< Identifier (variable reference, e.g. "user.age") */
	CDSL_EXPR_INT,	  /**< Integer literal */
	CDSL_EXPR_FLOAT,  /**< Float literal */
	CDSL_EXPR_BOOL,	  /**< Boolean literal (true/false) */
	CDSL_EXPR_STRING, /**< String literal ("...") */
	CDSL_EXPR_BINARY, /**< Binary operation (e.g. a + b, x == y) */
	CDSL_EXPR_UNARY,  /**< Unary operation (e.g. !expr) */
	CDSL_EXPR_CALL	  /**< Function call (e.g. strlen("hello")) */
} cdsl_expr_type_t;

/**
 * @brief AST expression node.
 *
 * Represents any expression in a DSL rule. The @c type field determines
 * which union member holds the actual data.
 *
 * @code
 * // Example: user.age >= 18
 * // This is a binary expression with op=GE, left=ID("user.age"), right=INT(18)
 * @endcode
 */
typedef struct cdsl_expr_node {
	cdsl_expr_type_t type; /**< Type discriminator */
	union {
		char* id_val;	  /**< Variable name (CDSL_EXPR_ID) */
		int int_val;	  /**< Integer value (CDSL_EXPR_INT) */
		double float_val; /**< Float value (CDSL_EXPR_FLOAT) */
		int bool_val;	  /**< Boolean value (CDSL_EXPR_BOOL) */
		char* string_val; /**< String value (CDSL_EXPR_STRING) */
		struct {
			cdsl_op_t op;		      /**< Operator */
			struct cdsl_expr_node* left;  /**< Left operand */
			struct cdsl_expr_node* right; /**< Right operand */
		} binary; /**< Binary expression data (CDSL_EXPR_BINARY) */
		struct {
			cdsl_op_t op;		     /**< Operator (typically CDSL_OP_NOT) */
			struct cdsl_expr_node* expr; /**< Operand */
		} unary;			     /**< Unary expression data (CDSL_EXPR_UNARY) */
		struct {
			char* func_name;	    /**< Function name */
			struct cdsl_arg_node* args; /**< Argument list */
		} call;				    /**< Function call data (CDSL_EXPR_CALL) */
	} data;
} cdsl_expr_node_t;

/**
 * @brief Linked list node for function/action arguments.
 */
typedef struct cdsl_arg_node {
	cdsl_expr_node_t* expr;	    /**< Argument expression */
	struct cdsl_arg_node* next; /**< Next argument in list */
} cdsl_arg_node_t;

/**
 * @brief Action invocation node (e.g. `reject_supplier("reason")`).
 *
 * Stores the action name and its argument list. Actions are resolved
 * against the schema during verification and executed via registered
 * callbacks in the VM.
 */
typedef struct cdsl_action_node {
	char* action_name;     /**< Name of the action to invoke */
	cdsl_arg_node_t* args; /**< Linked list of arguments */
} cdsl_action_node_t;

/**
 * @brief Key-value metadata item (e.g. `description = "..."`).
 *
 * Used in META blocks to store rule/metric properties like description,
 * weight, risk_level, pass_threshold, etc.
 */
typedef struct cdsl_meta_item {
	char* key;		     /**< Metadata key */
	char* value;		     /**< Metadata value (always stored as string) */
	struct cdsl_meta_item* next; /**< Next item in list */
} cdsl_meta_item_t;

/**
 * @brief CASE branch in a METRIC block.
 *
 * Evaluates the condition against the context; if true, executes the
 * associated action and stops further CASE evaluation.
 */
typedef struct cdsl_case_node {
	cdsl_expr_node_t* condition; /**< CASE condition expression */
	cdsl_action_node_t* action;  /**< Action to execute when condition is true */
	struct cdsl_case_node* next; /**< Next CASE in the metric */
} cdsl_case_node_t;

/**
 * @brief METRIC block for multi-indicator scoring.
 *
 * Contains a list of CASE branches and a DEFAULT action. Each metric
 * contributes a weighted score to the overall rule assessment.
 *
 * @code
 * METRIC capital_check {
 *     META { description = "Capital score"; weight = "40"; }
 *     CASE supplier.capital >= 5000000 THEN score(40)
 *     CASE supplier.capital >= 1000000 THEN score(20)
 *     DEFAULT score(0)
 * }
 * @endcode
 */
typedef struct cdsl_metric_node {
	char* name;			    /**< Metric identifier */
	cdsl_meta_item_t* meta_list;	    /**< Metadata (weight, is_critical, etc.) */
	cdsl_case_node_t* case_list;	    /**< Ordered list of CASE branches */
	cdsl_action_node_t* default_action; /**< DEFAULT action if no CASE matches */
	struct cdsl_metric_node* next;	    /**< Next metric in the rule */
} cdsl_metric_node_t;

/**
 * @brief Top-level rule node.
 *
 * Supports two modes:
 * - **Simple rule**: WHEN/THEN (binary pass/fail)
 * - **Metric rule**: Multiple METRIC blocks with weighted scoring
 */
typedef struct cdsl_rule {
	char* name;			 /**< Rule identifier */
	cdsl_meta_item_t* meta_list;	 /**< Rule metadata */
	cdsl_expr_node_t* when_expr;	 /**< WHEN expression (simple rules only) */
	cdsl_action_node_t* then_action; /**< THEN action (simple rules only) */
	cdsl_metric_node_t* metrics;	 /**< METRIC list (metric rules only, NULL for simple) */
} cdsl_rule_t;

/** @name Expression Constructors */
/** @{ */
cdsl_expr_node_t* cdsl_create_expr_id(char* id);
cdsl_expr_node_t* cdsl_create_expr_int(int val);
cdsl_expr_node_t* cdsl_create_expr_float(double val);
cdsl_expr_node_t* cdsl_create_expr_bool(int val);
cdsl_expr_node_t* cdsl_create_expr_string(char* val);
cdsl_expr_node_t*
cdsl_create_expr_binary(cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right);
cdsl_expr_node_t* cdsl_create_expr_unary(cdsl_op_t op, cdsl_expr_node_t* expr);
cdsl_expr_node_t* cdsl_create_expr_call(char* func_name, cdsl_arg_node_t* args);
/** @} */

/** @name Argument List Functions */
/** @{ */
cdsl_arg_node_t* cdsl_create_arg(cdsl_expr_node_t* expr);
cdsl_arg_node_t* cdsl_append_arg(cdsl_arg_node_t* list, cdsl_expr_node_t* expr);
/** @} */

/** @name Action Constructors */
/** @{ */
cdsl_action_node_t* cdsl_create_action(char* name, cdsl_arg_node_t* args);
/** @} */

/** @name Metadata Functions */
/** @{ */
cdsl_meta_item_t* cdsl_create_meta_item(char* key, char* value);
cdsl_meta_item_t* cdsl_append_meta(cdsl_meta_item_t* list, cdsl_meta_item_t* item);
char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);
/** @} */

/** @name Case/Metric Constructors */
/** @{ */
cdsl_case_node_t* cdsl_create_case(cdsl_expr_node_t* cond, cdsl_action_node_t* action);
cdsl_case_node_t* cdsl_append_case(cdsl_case_node_t* list, cdsl_case_node_t* item);
cdsl_metric_node_t* cdsl_create_metric(char* name,
				       cdsl_meta_item_t* meta,
				       cdsl_case_node_t* cases,
				       cdsl_action_node_t* def_act);
cdsl_metric_node_t* cdsl_append_metric(cdsl_metric_node_t* list, cdsl_metric_node_t* item);
/** @} */

/** @name Rule Constructors */
/** @{ */
cdsl_rule_t* cdsl_create_simple_rule(char* name,
				     cdsl_meta_item_t* meta,
				     cdsl_expr_node_t* when,
				     cdsl_action_node_t* then);
cdsl_rule_t*
cdsl_create_metric_rule(char* name, cdsl_meta_item_t* meta, cdsl_metric_node_t* metrics);
cdsl_rule_t* cdsl_create_extends_rule(char* name, char* template_name, cdsl_meta_item_t* meta);
/** @} */

/** @name Memory Management */
/** @{ */
void cdsl_free_expr(cdsl_expr_node_t* expr);
void cdsl_free_arg(cdsl_arg_node_t* arg);
void cdsl_free_action(cdsl_action_node_t* action);
void cdsl_free_meta(cdsl_meta_item_t* meta);
void cdsl_free_case(cdsl_case_node_t* cs);
void cdsl_free_metric(cdsl_metric_node_t* m);
void cdsl_free_rule(cdsl_rule_t* rule);
/** @} */

/**
 * @brief Parse a DSL string into an AST rule.
 *
 * Uses the Flex/Bison-generated parser to tokenize and parse the input.
 * The returned rule must be freed with cdsl_free_rule().
 *
 * @param dsl_code Null-terminated DSL source string
 * @return Parsed rule, or NULL on parse error
 */
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);

/** @name Template Registry */
/** @{ */
void cdsl_template_register(cdsl_rule_t* template_rule);
cdsl_rule_t* cdsl_template_get(const char* name);
void cdsl_template_clear(void);
/** @} */

#endif
/** @} */
