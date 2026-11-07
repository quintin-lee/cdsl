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

#include "cdsl/util/portability.h"

#include "cdsl/util/arena.h"
#include "cdsl/util/error.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define CDSL_MAX_EXPR_DEPTH 64
#define CDSL_MAX_CASES 16
#define CDSL_MAX_RULES 128
#define CDSL_MAX_INPUT_LENGTH 65536

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
	CDSL_TYPE_DATE,	  /**< Date/Time value (ISO 8601) */
	CDSL_TYPE_LONG,	  /**< 64-bit signed integer (int64_t) */
	CDSL_TYPE_ARRAY,  /**< Array of values */
	CDSL_TYPE_MAP,	  /**< Map/Object of key-value pairs (v1.2.0) */
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
	CDSL_OP_NEG, /**< Unary negation (-expr) */
	CDSL_OP_IN   /**< Inclusion / membership check (in) */
} cdsl_op_t;

#define CDSL_OP_COUNT 15

/**
 * @brief 算子元数据集中描述符结构体
 */
typedef struct cdsl_op_desc {
	cdsl_op_t op;	      /**< 算子枚举常量 */
	const char* c_symbol; /**< 对应的 C 代码运算符（如 "==", "&&", "!", "+", "-"） */
	const char* name;     /**< 算子友好名称（如 "==", "AND", "OR", "NOT", "NEG"） */
	int arity;	      /**< 算子目数：1 为一元算子，2 为二元算子 */
	bool is_relational;   /**< 是否为关系比较算子 (==, !=, <, >, <=, >=) */
	bool is_arithmetic;   /**< 是否为二元算术算子 (+, -, *, /) */
	bool is_logical;      /**< 是否为逻辑算子 (AND, OR, NOT) */
} cdsl_op_desc_t;

/**
 * @brief Expression node types in the AST.
 *
 * Each expression node is one of these types, with data stored in the
 * corresponding union member of ::cdsl_expr_node_t.
 */
typedef enum {
	CDSL_EXPR_ID,	     /**< Identifier (variable reference, e.g. "user.age") */
	CDSL_EXPR_INT,	     /**< Integer literal */
	CDSL_EXPR_FLOAT,     /**< Float literal */
	CDSL_EXPR_BOOL,	     /**< Boolean literal (true/false) */
	CDSL_EXPR_STRING,    /**< String literal ("...") */
	CDSL_EXPR_DATE,	     /**< Date literal (@2026-05-30) */
	CDSL_EXPR_LONG,	     /**< Long integer literal (123L) */
	CDSL_EXPR_ARRAY,     /**< Array literal ([...]) */
	CDSL_EXPR_BINARY,    /**< Binary operation (e.g. a + b, x == y) */
	CDSL_EXPR_UNARY,     /**< Unary operation (e.g. !expr) */
	CDSL_EXPR_CALL,	     /**< Function call (e.g. strlen("hello")) */
	CDSL_EXPR_ERROR,     /**< Error placeholder (recovery from parse error) */
	CDSL_EXPR_MAP,	     /**< Map literal ({ key: expr, ... }) */
	CDSL_EXPR_SUBSCRIPT, /**< Subscript / property access (expr[expr] or expr.id) */
	CDSL_EXPR_RANGE,     /**< Interval range ([min..max]) */
	CDSL_EXPR_LAMBDA     /**< Lambda expression (param -> expr) */
} cdsl_expr_type_t;

struct cdsl_arg_node;
struct cdsl_expr_node;

/**
 * @brief Linked list node for AST Map entries ({ key: expr }).
 */
typedef struct cdsl_map_ast_entry {
	char* key;			 /**< Map key name */
	struct cdsl_expr_node* value;	 /**< Map value expression */
	struct cdsl_map_ast_entry* next; /**< Next entry in map literal */
} cdsl_map_ast_entry_t;

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
		time_t date_val;  /**< Date value (CDSL_EXPR_DATE) */
		int64_t long_val; /**< Long integer value (CDSL_EXPR_LONG) */
		struct {
			struct cdsl_arg_node* elements; /**< Array elements */
		} array; /**< Array expression data (CDSL_EXPR_ARRAY) */
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
		struct {
			cdsl_map_ast_entry_t* entries; /**< Map entries linked list */
			int count;		       /**< Number of entries */
		} map; /**< Map literal expression data (CDSL_EXPR_MAP) */
		struct {
			struct cdsl_expr_node* target; /**< Target object/array being indexed */
			struct cdsl_expr_node* index;  /**< Index or property expression */
		} subscript; /**< Subscript expression data (CDSL_EXPR_SUBSCRIPT) */
		struct {
			struct cdsl_expr_node* min_expr; /**< Min bound */
			struct cdsl_expr_node* max_expr; /**< Max bound */
		} range; /**< Interval range data (CDSL_EXPR_RANGE) */
		struct {
			char* param_name;	     /**< Lambda parameter name */
			struct cdsl_expr_node* body; /**< Lambda body expression */
		} lambda; /**< Lambda expression data (CDSL_EXPR_LAMBDA) */
	} data;
	int line;   /**< Source line number (0 if unknown) */
	int column; /**< Source column number (0 if unknown) */
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
	int line;	       /**< Source line number (0 if unknown) */
	int column;	       /**< Source column number (0 if unknown) */
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
	cdsl_arena_t* arena;		 /**< Arena for AST memory management (if any) */
	int line;			 /**< Source line number (0 if unknown) */
	int column;			 /**< Source column number (0 if unknown) */
} cdsl_rule_t;

/** @name AST Context Management */
/** @{ */

/**
 * @brief Set the current arena for AST node allocation.
 * @param arena Arena to use (NULL to use standard heap)
 */
void cdsl_ast_set_current_arena(cdsl_arena_t* arena);

/**
 * @brief Get the current arena for AST node allocation.
 * @return Current arena or NULL
 */
cdsl_arena_t* cdsl_ast_get_current_arena(void);
/** @} */

/** @name Expression Constructors */
/** @{ */

/**
 * @brief Create an identifier (variable) expression.
 * @param id Variable name (caller yields ownership of @p id string)
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_id(char* id, int line, int column);

/**
 * @brief Create an integer literal expression.
 * @param val Integer value
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_int(int val, int line, int column);

/**
 * @brief Create a float literal expression.
 * @param val Double-precision floating point value
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_float(double val, int line, int column);

/**
 * @brief Create a boolean literal expression.
 * @param val 1 for true, 0 for false
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_bool(int val, int line, int column);

/**
 * @brief Create a string literal expression.
 * @param val String value (caller yields ownership of @p val)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_string(char* val, int line, int column);

/**
 * @brief Create a date literal expression.
 * @param val Date value
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_date(time_t val, int line, int column);

/**
 * @brief Create a long integer literal expression (int64_t).
 * @param val Signed 64-bit integer value
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_long(int64_t val, int line, int column);

/**
 * @brief Create an array literal expression.
 * @param elements Linked list of array elements (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_array(cdsl_arg_node_t* elements, int line, int column);

/**
 * @brief Create a binary operation expression (e.g., a + b).
 * @param op Operator type
 * @param left Left operand (ownership transferred)
 * @param right Right operand (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_binary(
    cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right, int line, int column);

/**
 * @brief Create a unary operation expression (e.g., !a).
 * @param op Operator type (e.g., CDSL_OP_NOT, CDSL_OP_NEG)
 * @param expr Operand (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t*
cdsl_create_expr_unary(cdsl_op_t op, cdsl_expr_node_t* expr, int line, int column);

/**
 * @brief Create a function call expression.
 * @param func_name Name of the function (ownership transferred)
 * @param args Argument list (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t*
cdsl_create_expr_call(char* func_name, cdsl_arg_node_t* args, int line, int column);

/**
 * @brief Create an error placeholder expression node.
 *
 * Used during parser error recovery when an expression cannot be parsed.
 * The rest of the pipeline should gracefully handle this node type.
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated error expression node
 */
CDSL_NODISCARD
cdsl_expr_node_t* cdsl_create_expr_error(int line, int column);

/**
 * @brief Create a Map entry list node.
 * @param key Entry key (ownership transferred)
 * @param value Entry value expression (ownership transferred)
 * @return Newly allocated map entry node
 */
cdsl_map_ast_entry_t* cdsl_create_map_ast_entry(char* key, cdsl_expr_node_t* value);

/**
 * @brief Append a Map entry to an existing list.
 * @param list Map entry list head (can be NULL)
 * @param item Entry item to append (ownership transferred)
 * @return Updated list head
 */
cdsl_map_ast_entry_t* cdsl_append_map_ast_entry(cdsl_map_ast_entry_t* list,
						cdsl_map_ast_entry_t* item);

/**
 * @brief Free a Map AST entry list.
 * @param entry Map entry list head (NULL-safe)
 */
void cdsl_free_map_ast_entry(cdsl_map_ast_entry_t* entry);

/**
 * @brief Create a Map literal expression node.
 * @param entries Map entries list (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_map(cdsl_map_ast_entry_t* entries, int line, int column);

/**
 * @brief Create a subscript / property access expression node (target[index]).
 * @param target Target expression (ownership transferred)
 * @param index Index or property expression (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t*
cdsl_create_expr_subscript(cdsl_expr_node_t* target, cdsl_expr_node_t* index, int line, int column);

/**
 * @brief Create an interval range expression node ([min..max]).
 * @param min_expr Minimum bound expression (ownership transferred)
 * @param max_expr Maximum bound expression (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t* cdsl_create_expr_range(cdsl_expr_node_t* min_expr,
					 cdsl_expr_node_t* max_expr,
					 int line,
					 int column);

/**
 * @brief Create a Lambda expression node (param -> body).
 * @param param_name Parameter identifier name (ownership transferred)
 * @param body Lambda body expression (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated expression node
 */
cdsl_expr_node_t*
cdsl_create_expr_lambda(char* param_name, cdsl_expr_node_t* body, int line, int column);
/** @} */

/** @name Argument List Functions */
/** @{ */

/**
 * @brief Create an argument list head.
 * @param expr First argument expression (ownership transferred)
 * @return Newly allocated argument node
 */
cdsl_arg_node_t* cdsl_create_arg(cdsl_expr_node_t* expr);

/**
 * @brief Append an expression to an argument list.
 * @param list Argument list head (can be NULL)
 * @param expr Expression to append (ownership transferred)
 * @return Updated list head
 */
cdsl_arg_node_t* cdsl_append_arg(cdsl_arg_node_t* list, cdsl_expr_node_t* expr);
/** @} */

/** @name Action Constructors */
/** @{ */

/**
 * @brief Create an action invocation node.
 * @param name Action name (ownership transferred)
 * @param args Argument list (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated action node
 */
cdsl_action_node_t* cdsl_create_action(char* name, cdsl_arg_node_t* args, int line, int column);
/** @} */

/** @name Metadata Functions */
/** @{ */

/**
 * @brief 创建元数据项（内部始终执行深拷贝，调用方保留入参所有权）
 * @param key 键名字符串（支持字面量或堆内存，允许只读）
 * @param value 键值字符串（支持字面量或堆内存，允许只读）
 * @return 新分配的元数据项指针，入参非法或 OOM 时返回 NULL
 */
cdsl_meta_item_t* cdsl_create_meta_item(const char* key, const char* value);

/**
 * @brief 创建元数据项并直接追加至列表末尾的便捷助手函数
 * @param list 现有元数据链表头指针（允许为 NULL）
 * @param key 键名
 * @param value 键值
 * @return 更新后的元数据链表头指针
 */
cdsl_meta_item_t* cdsl_append_meta_kv(cdsl_meta_item_t* list, const char* key, const char* value);

/**
 * @brief Append a metadata item to a list.
 * @param list Metadata list head (can be NULL)
 * @param item Item to append (ownership transferred)
 * @return Updated list head
 */
cdsl_meta_item_t* cdsl_append_meta(cdsl_meta_item_t* list, cdsl_meta_item_t* item);

/**
 * @brief Retrieve a metadata value by key from a list.
 * @param list Metadata list to search
 * @param key Key to find
 * @return String value if found, or NULL if not found
 */
char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);
/** @} */

/** @name Case/Metric Constructors */
/** @{ */

/**
 * @brief Create a metric CASE branch.
 * @param cond Condition expression (ownership transferred)
 * @param action Action to execute on match (ownership transferred)
 * @return Newly allocated case node
 */
cdsl_case_node_t* cdsl_create_case(cdsl_expr_node_t* cond, cdsl_action_node_t* action);

/**
 * @brief Append a CASE branch to a list.
 * @param list CASE list head (can be NULL)
 * @param item Item to append (ownership transferred)
 * @return Updated list head
 */
cdsl_case_node_t* cdsl_append_case(cdsl_case_node_t* list, cdsl_case_node_t* item);

/**
 * @brief Create a METRIC block node.
 * @param name Metric name (ownership transferred)
 * @param meta Metadata list (ownership transferred)
 * @param cases CASE branch list (ownership transferred)
 * @param def_act DEFAULT action (ownership transferred)
 * @return Newly allocated metric node
 */
cdsl_metric_node_t* cdsl_create_metric(char* name,
				       cdsl_meta_item_t* meta,
				       cdsl_case_node_t* cases,
				       cdsl_action_node_t* def_act);

/**
 * @brief Append a METRIC node to a rule's metric list.
 * @param list Metric list head (can be NULL)
 * @param item Item to append (ownership transferred)
 * @return Updated list head
 */
cdsl_metric_node_t* cdsl_append_metric(cdsl_metric_node_t* list, cdsl_metric_node_t* item);
/** @} */

/** @name Rule Constructors */
/** @{ */

/**
 * @brief Create a simple WHEN/THEN rule.
 * @param name Rule name (ownership transferred)
 * @param meta Rule metadata (ownership transferred)
 * @param when Condition expression (ownership transferred)
 * @param then Action to execute (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated rule
 */
cdsl_rule_t* cdsl_create_simple_rule(char* name,
				     cdsl_meta_item_t* meta,
				     cdsl_expr_node_t* when,
				     cdsl_action_node_t* then,
				     int line,
				     int column);

/**
 * @brief Create a multi-metric scoring rule.
 * @param name Rule name (ownership transferred)
 * @param meta Rule metadata (ownership transferred)
 * @param metrics List of metrics (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated rule
 */
cdsl_rule_t* cdsl_create_metric_rule(
    char* name, cdsl_meta_item_t* meta, cdsl_metric_node_t* metrics, int line, int column);

/**
 * @brief Create a rule that extends a template.
 * @param name Rule name (ownership transferred)
 * @param template_name Name of the parent template (ownership transferred)
 * @param meta Override metadata (ownership transferred)
 * @param metrics Additional metrics (ownership transferred)
 * @param line Source line number
 * @param column Source column number
 * @return Newly allocated rule
 */
cdsl_rule_t* cdsl_create_extends_rule(char* name,
				      char* template_name,
				      cdsl_meta_item_t* meta,
				      cdsl_metric_node_t* metrics,
				      int line,
				      int column);
/** @} */

/**
 * @brief Deep-copy a metric list (internal, shared between template.c and ast.c).
 * @param src Source metric list to copy
 * @return Newly allocated copy of the metric list, or NULL on error
 */
cdsl_metric_node_t* copy_metric_list(const cdsl_metric_node_t* src);

/** @name Memory Management */
/** @{ */

/**
 * @brief Recursively free an expression AST.
 * @param expr Root node to free (NULL-safe)
 */
void cdsl_free_expr(cdsl_expr_node_t* expr);

/**
 * @brief Recursively free an argument list.
 * @param arg Head node to free (NULL-safe)
 */
void cdsl_free_arg(cdsl_arg_node_t* arg);

/**
 * @brief Free an action node and its arguments.
 * @param action Node to free (NULL-safe)
 */
void cdsl_free_action(cdsl_action_node_t* action);

/**
 * @brief Free a metadata list and all key/value strings.
 * @param meta Head node to free (NULL-safe)
 */
void cdsl_free_meta(cdsl_meta_item_t* meta);

/**
 * @brief Free a CASE branch.
 * @param cs Node to free (NULL-safe)
 */
void cdsl_free_case(cdsl_case_node_t* cs);

/**
 * @brief Free a METRIC block and all its cases.
 * @param m Node to free (NULL-safe)
 */
void cdsl_free_metric(cdsl_metric_node_t* m);

/**
 * @brief Free a complete rule AST.
 * @param rule Rule to free (NULL-safe)
 */
void cdsl_free_rule(cdsl_rule_t* rule);
/** @} */

/**
 * @brief Parse a DSL string into an AST rule.
 *
 * Uses the Flex/Bison-generated parser to tokenize and parse the input.
 * The returned rule must be freed with cdsl_free_rule().
 *
 * @param dsl_code Null-terminated DSL source string
 * @param errors   Optional output for parse errors (may be NULL).
 *                 If non-NULL and no errors pointer currently exists,
 *                 a new error list is allocated and returned.
 *                 If an error list already exists, errors are appended.
 * @return Parsed rule, or NULL on parse error
 */
CDSL_NODISCARD
cdsl_rule_t* cdsl_parse_string(const char* dsl_code, cdsl_error_list_t** errors);

/** @name Template Registry */
/** @{ */
void cdsl_template_register(cdsl_rule_t* template_rule);
cdsl_rule_t* cdsl_template_get(const char* name);
void cdsl_template_clear(void);
/** @} */

/** @name Operator and Type Queries */
/** @{ */
/**
 * @brief 获取算子对应的只读描述符
 * @param op 算子枚举
 * @return 描述符常量指针；若枚举非法越界则返回 NULL
 */
const cdsl_op_desc_t* cdsl_op_get_desc(cdsl_op_t op);

/**
 * @brief 判断算子是否为关系/比较算子 (==, !=, <, >, <=, >=)
 */
bool cdsl_op_is_relational(cdsl_op_t op);

/**
 * @brief 判断算子是否为二元算术算子 (+, -, *, /)
 */
bool cdsl_op_is_arithmetic(cdsl_op_t op);

/**
 * @brief 判断算子是否为二元逻辑算子 (AND, OR)
 */
bool cdsl_op_is_logical_binary(cdsl_op_t op);

/**
 * @brief 判断算子是否为一元算子 (NOT, NEG)
 */
bool cdsl_op_is_unary(cdsl_op_t op);

/**
 * @brief 获取算子在 C 代码中的运算符符号（供 codegen 使用）
 * @return 对应的 C 符号字符串（如 "==", "&&", "||", "!", "+", "-"），非法返回 "?"
 */
const char* cdsl_op_c_symbol(cdsl_op_t op);

/**
 * @brief 获取算子的可读名称（供 visualize / 跟踪 / 报错使用）
 * @return 对应的名称（如 "==", "AND", "OR", "NOT", "+", "-", "NEG"），非法返回 "?"
 */
const char* cdsl_op_name(cdsl_op_t op);

/**
 * @brief 获取 DSL 类型的字符串名称
 * @return 类型名称（如 "INT", "FLOAT", "BOOL", "STRING", "DATE", "LONG", "ARRAY", "VOID"），非法返回 "UNKNOWN"
 */
const char* cdsl_type_name(cdsl_type_t type);

/**
 * @brief 对标准化三值比较结果 (cmp < 0, cmp == 0, cmp > 0) 进行关系算子求值
 * @param op 关系算子枚举 (CDSL_OP_EQ, CDSL_OP_NEQ, CDSL_OP_LT, CDSL_OP_GT, CDSL_OP_LE, CDSL_OP_GE)
 * @param cmp 标准化比较差值（负数代表小于，0代表等于，正数代表大于）
 * @return 比较结果布尔值；若 op 不是关系算子则返回 false
 */
static inline bool
cdsl_op_eval_cmp(cdsl_op_t op, int cmp)
{
	switch (op) {
	case CDSL_OP_EQ:
		return cmp == 0;
	case CDSL_OP_NE:
		return cmp != 0;
	case CDSL_OP_LT:
		return cmp < 0;
	case CDSL_OP_GT:
		return cmp > 0;
	case CDSL_OP_LE:
		return cmp <= 0;
	case CDSL_OP_GE:
		return cmp >= 0;
	default:
		return false;
	}
}
/** @} */

#endif
/** @} */
