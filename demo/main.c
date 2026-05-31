/**
 * @file main.c
 * @brief C-DSL Framework demo application.
 *
 * Demonstrates the full C-DSL pipeline across six scenarios:
 * 1. Supplier Qualification Audit (AI NL-to-DSL + scoring)
 * 2. Document Format & Signature Audit
 * 3. Content Safety & Moderation Audit
 * 4. JSON Context Loading
 * 5. Simple Pass/Fail Rules (no scoring)
 * 6. RuleSet Batch Execution
 *
 * Each scenario walks through: AI generation, safety review, parsing,
 * schema verification, context binding, rule execution, and reporting.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/execution.h"
#include "cdsl/ai.h"

static cdsl_rule_t*
parse_dsl(const char* dsl)
{
	cdsl_error_list_t* errs = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	if (errs) {
		for (int i = 0; i < errs->count; i++) {
			cdsl_error_print(errs->errors[i]);
		}
		cdsl_error_list_free(errs);
	}
	return rule;
}

/**
 * @brief Callback function for action invocations in the demo.
 *
 * Prints the action name and its arguments in a readable format.
 *
 * @param action_name Name of the triggered action
 * @param args Linked list of argument expressions
 * @param user_data User-provided data pointer (unused in demo)
 */
static void
action_callback(const char* action_name, cdsl_arg_node_t* args, void* user_data)
{
	printf("  [ACTION] %s(", action_name);
	int first = 1;
	for (cdsl_arg_node_t* a = args; a; a = a->next) {
		if (!first) {
			printf(", ");
		}
		first = 0;
		if (a->expr) {
			switch (a->expr->type) {
			case CDSL_EXPR_INT:
				printf("%d", a->expr->data.int_val);
				break;
			case CDSL_EXPR_FLOAT:
				printf("%.2f", a->expr->data.float_val);
				break;
			case CDSL_EXPR_BOOL:
				printf("%s", a->expr->data.bool_val ? "true" : "false");
				break;
			case CDSL_EXPR_STRING:
				printf("\"%s\"",
				       a->expr->data.string_val ? a->expr->data.string_val : "");
				break;
			default:
				printf("?");
				break;
			}
		}
	}
	printf(") triggered\n");
}

/**
 * @brief Print a section separator with a title.
 * @param title Section title string
 */
static void
print_separator(const char* title)
{
	printf("\n%s\n", title);
}

/**
 * @brief Demo 1: Supplier Qualification Audit.
 *
 * Demonstrates AI-driven NL-to-DSL translation, safety review,
 * and scoring-based rule execution with multiple test cases
 * including a good supplier, small supplier, blacklisted supplier,
 * and medium supplier.
 *
 * @param schema Registered schema with supplier variables and actions
 * @param ai_cfg AI configuration (mock mode in demo)
 */
static void
demo_supplier_audit(cdsl_schema_t* schema, cdsl_ai_config_t* ai_cfg)
{
	print_separator("=== DEMO 1: Supplier Qualification Audit ===");

	const char* nl =
	    "supplier qualification audit with blacklist check and capital verification";
	printf("User NL request: \"%s\"\n\n", nl);

	printf("[Step 1] AI translating NL to DSL...\n");
	char* dsl_code = cdsl_ai_translate(nl, schema, ai_cfg);
	if (!dsl_code) {
		printf("  [ERROR] AI translation returned NULL\n");
		return;
	}
	printf("Generated DSL:\n%s\n", dsl_code);

	printf("[Step 2] AI reviewing DSL for safety...\n");
	cdsl_ai_review_t* review = cdsl_ai_review(dsl_code, schema, ai_cfg);
	if (!review) {
		printf("  [ERROR] AI review returned NULL\n");
		free(dsl_code);
		return;
	}
	printf("  Approved: %s (Risk Score: %d/100)\n",
	       review->approved ? "YES" : "NO",
	       review->risk_score);
	printf("  Reason: %s\n", review->reason ? review->reason : "");
	if (review->suggestions && review->suggestions[0]) {
		printf("  Suggestions: %s\n", review->suggestions);
	}

	if (!review->approved) {
		printf("  [BLOCKED] DSL did not pass AI safety review.\n");
		cdsl_ai_review_free(review);
		free(dsl_code);
		return;
	}

	printf("[Step 3] Parsing DSL...\n");
	cdsl_rule_t* rule = parse_dsl(dsl_code);
	if (!rule) {
		printf("  Parse failed.\n");
		return;
	}

	printf("[Step 4] Verifying against schema...\n");
	char err[512] = {0};
	if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
		printf("  Verification failed: %s\n", err);
		cdsl_free_rule(rule);
		free(dsl_code);
		return;
	}
	printf("  Schema verification PASSED.\n");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", action_callback);
	cdsl_vm_register_action(vm, "fail_metric", action_callback);
	cdsl_vm_register_action(vm, "reject_supplier", action_callback);

	typedef struct {
		const char* name;
		int cap;
		int years;
		int blacklisted;
	} test_case_t;
	test_case_t cases[] = {
	    {"Good supplier", 6000000, 10, 0},
	    {"Small supplier", 500000, 1, 0},
	    {"Blacklisted", 8000000, 20, 1},
	    {"Medium supplier", 1500000, 3, 0},
	};
	int ncases = sizeof(cases) / sizeof(cases[0]);

	for (int i = 0; i < ncases; i++) {
		printf("\n[Step 5] Evaluating: %s\n", cases[i].name);
		cdsl_context_t* ctx = cdsl_context_create(schema);
		cdsl_context_set_int(ctx, "supplier.registered_capital", cases[i].cap);
		cdsl_context_set_int(ctx, "supplier.years_in_business", cases[i].years);
		cdsl_context_set_bool(ctx, "supplier.is_blacklisted", cases[i].blacklisted);

		cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
		cdsl_report_print(report);

		cdsl_report_free(report);
		cdsl_context_free(ctx);
	}

	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_ai_review_free(review);
	free(dsl_code);
}

/**
 * @brief Demo 2: Document Format & Signature Audit.
 *
 * Demonstrates string and boolean context variables with scoring rules
 * for document format validation and digital signature checking.
 *
 * @param schema Registered schema with document variables and actions
 * @param ai_cfg AI configuration
 */
static void
demo_doc_audit(cdsl_schema_t* schema, cdsl_ai_config_t* ai_cfg)
{
	print_separator("=== DEMO 2: Document Format & Signature Audit ===");

	const char* nl = "document format audit with signature check";
	printf("User NL request: \"%s\"\n\n", nl);

	char* dsl_code = cdsl_ai_translate(nl, schema, ai_cfg);
	if (!dsl_code) {
		printf("[ERROR] AI translation returned NULL\n");
		return;
	}
	printf("[AI Generated DSL]:\n%s\n", dsl_code);

	cdsl_ai_review_t* review = cdsl_ai_review(dsl_code, schema, ai_cfg);
	if (!review) {
		printf("[ERROR] AI review returned NULL\n");
		free(dsl_code);
		return;
	}
	printf("[AI Review] Approved: %s (Risk: %d)\n",
	       review->approved ? "YES" : "NO",
	       review->risk_score);

	cdsl_rule_t* rule = parse_dsl(dsl_code);
	if (!rule) {
		printf("Parse failed.\n");
		cdsl_ai_review_free(review);
		free(dsl_code);
		return;
	}

	char err[512] = {0};
	if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
		printf("Verify failed: %s\n", err);
		cdsl_free_rule(rule);
		cdsl_ai_review_free(review);
		free(dsl_code);
		return;
	}

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", action_callback);
	cdsl_vm_register_action(vm, "fail_metric", action_callback);

	struct {
		const char* name;
		const char* fmt;
		double size;
		int sig;
	} cases[] = {
	    {"Valid PDF", "pdf", 5.0, 1},
	    {"Large PDF", "pdf", 25.0, 1},
	    {"Unsigned PDF", "pdf", 3.0, 0},
	    {"Word doc", "docx", 2.0, 1},
	};
	int ncases = 4;

	for (int i = 0; i < ncases; i++) {
		printf("\n[Case] %s\n", cases[i].name);
		cdsl_context_t* ctx = cdsl_context_create(schema);
		cdsl_context_set_string(ctx, "document.format", cases[i].fmt);
		cdsl_context_set_float(ctx, "document.size_mb", cases[i].size);
		cdsl_context_set_bool(ctx, "document.has_digital_signature", cases[i].sig);

		cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
		cdsl_report_print(report);
		cdsl_report_free(report);
		cdsl_context_free(ctx);
	}

	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_ai_review_free(review);
	free(dsl_code);
}

/**
 * @brief Demo 3: Content Safety & Moderation Audit.
 *
 * Demonstrates content moderation rules with sensitive word counting,
 * PII detection, and AI spam scoring.
 *
 * @param schema Registered schema with content variables and actions
 * @param ai_cfg AI configuration
 */
static void
demo_content_audit(cdsl_schema_t* schema, cdsl_ai_config_t* ai_cfg)
{
	print_separator("=== DEMO 3: Content Safety & Moderation Audit ===");

	const char* nl = "content safety audit with sensitive words and PII detection";
	printf("User NL request: \"%s\"\n\n", nl);

	char* dsl_code = cdsl_ai_translate(nl, schema, ai_cfg);
	if (!dsl_code) {
		printf("[ERROR] AI translation returned NULL\n");
		return;
	}
	printf("[AI Generated DSL]:\n%s\n", dsl_code);

	cdsl_ai_review_t* review = cdsl_ai_review(dsl_code, schema, ai_cfg);
	if (!review) {
		printf("[ERROR] AI review returned NULL\n");
		free(dsl_code);
		return;
	}
	printf("[AI Review] Approved: %s (Risk: %d)\n",
	       review->approved ? "YES" : "NO",
	       review->risk_score);

	cdsl_rule_t* rule = parse_dsl(dsl_code);
	if (!rule) {
		printf("Parse failed.\n");
		cdsl_ai_review_free(review);
		free(dsl_code);
		return;
	}

	char err[512] = {0};
	if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
		printf("Verify failed: %s\n", err);
		cdsl_free_rule(rule);
		cdsl_ai_review_free(review);
		free(dsl_code);
		return;
	}

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", action_callback);
	cdsl_vm_register_action(vm, "fail_metric", action_callback);

	struct {
		const char* name;
		int sens;
		int pii;
		double spam;
	} cases[] = {
	    {"Clean content", 0, 0, 0.1},
	    {"Mild spam", 0, 0, 0.6},
	    {"PII exposed", 0, 1, 0.2},
	    {"Toxic + PII", 3, 1, 0.9},
	};
	int ncases = 4;

	for (int i = 0; i < ncases; i++) {
		printf("\n[Case] %s\n", cases[i].name);
		cdsl_context_t* ctx = cdsl_context_create(schema);
		cdsl_context_set_int(ctx, "content.sensitive_words_count", cases[i].sens);
		cdsl_context_set_bool(ctx, "content.contains_pii", cases[i].pii);
		cdsl_context_set_float(ctx, "content.ai_spam_score", cases[i].spam);

		cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
		cdsl_report_print(report);
		cdsl_report_free(report);
		cdsl_context_free(ctx);
	}

	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_ai_review_free(review);
	free(dsl_code);
}

/**
 * @brief Demo 4: JSON Context Loading.
 *
 * Demonstrates loading context variables from a JSON string using
 * cdsl_context_load_json(), including automatic type inference for
 * integers, floats, and booleans.
 */
static void
demo_json_context(void)
{
	print_separator("=== DEMO 4: JSON Context Loading ===");

	const char* dsl = "RULE json_demo {\n"
			  "    META {\n"
			  "        description = \"Demo using JSON-loaded context\"\n"
			  "        pass_threshold = \"70\"\n"
			  "        partial_threshold = \"40\"\n"
			  "    }\n"
			  "    METRIC score_check {\n"
			  "        META {\n"
			  "            description = \"Performance score check\"\n"
			  "            weight = \"60\"\n"
			  "        }\n"
			  "        CASE data.score >= 80 THEN score(60)\n"
			  "        CASE data.score >= 50 THEN score(30)\n"
			  "        DEFAULT score(0)\n"
			  "    }\n"
			  "    METRIC flag_check {\n"
			  "        META {\n"
			  "            description = \"Status flag verification\"\n"
			  "            weight = \"40\"\n"
			  "            is_critical = \"true\"\n"
			  "        }\n"
			  "        CASE data.active == true THEN score(40)\n"
			  "        DEFAULT fail_metric(0, \"inactive_record\")\n"
			  "    }\n"
			  "}\n";

	const char* json = "{\"data\":{\"score\":85,\"active\":true,\"name\":\"test\"}}";

	printf("DSL:\n%s\n", dsl);
	printf("JSON Context: %s\n\n", json);

	cdsl_rule_t* rule = parse_dsl(dsl);
	if (!rule) {
		printf("Parse failed.\n");
		return;
	}

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "data.score", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "data.active", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "data.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);

	char err[512] = {0};
	if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
		printf("Verify failed: %s\n", err);
		cdsl_free_rule(rule);
		cdsl_schema_free(schema);
		return;
	}

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_load_json(ctx, json);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", action_callback);
	cdsl_vm_register_action(vm, "fail_metric", action_callback);

	cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
	cdsl_report_print(report);

	cdsl_report_free(report);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
}

/**
 * @brief Demo 5: Simple Pass/Fail Rules (No Scoring).
 *
 * Demonstrates traditional WHEN/THEN rules with boolean pass/fail
 * outcomes. Includes blacklist checking, capital floor verification,
 * and document format validation.
 *
 * @param schema Registered schema with supplier and document variables
 */
static void
demo_simple_rules(cdsl_schema_t* schema)
{
	print_separator("=== DEMO 5: Simple Pass/Fail Rules (No Scoring) ===");

	const char* dsl_rules[] = {
	    "RULE check_blacklist {"
	    "    META { description = \"Supplier must not be blacklisted\" }"
	    "    WHEN supplier.is_blacklisted == true"
	    "    THEN reject_supplier(\"blacklisted\")"
	    "}",

	    "RULE check_capital_floor {"
	    "    META { description = \"Capital must be at least 500k\" }"
	    "    WHEN supplier.registered_capital < 500000"
	    "    THEN reject_supplier(\"insufficient_capital\")"
	    "}",

	    "RULE check_format {"
	    "    META { description = \"Document must be PDF\" }"
	    "    WHEN document.format != \"pdf\""
	    "    THEN reject_document(\"not_pdf\")"
	    "}",
	};
	int nrules = 3;

	for (int i = 0; i < nrules; i++) {
		printf("\n[Rule] %s\n", dsl_rules[i]);
		cdsl_rule_t* rule = parse_dsl(dsl_rules[i]);
		if (!rule) {
			printf("  Parse failed.\n");
			continue;
		}

		char err[512] = {0};
		if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
			printf("  Verify failed: %s\n", err);
			cdsl_free_rule(rule);
			continue;
		}

		cdsl_vm_t* vm = cdsl_vm_create(schema);
		cdsl_vm_register_action(vm, "reject_supplier", action_callback);
		cdsl_vm_register_action(vm, "reject_document", action_callback);

		cdsl_context_t* ctx = cdsl_context_create(schema);
		cdsl_context_set_bool(ctx, "supplier.is_blacklisted", 0);
		cdsl_context_set_int(ctx, "supplier.registered_capital", 200000);
		cdsl_context_set_string(ctx, "document.format", "docx");

		cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
		cdsl_report_print(report);

		cdsl_report_free(report);
		cdsl_context_free(ctx);
		cdsl_vm_free(vm);
		cdsl_free_rule(rule);
	}
}

/**
 * @brief Demo 6: RuleSet Batch Execution.
 *
 * Demonstrates creating a ruleset with priority-ordered rules and
 * executing them as a batch via cdsl_vm_execute_ruleset().
 * Includes aggregate reporting with pass/fail counts.
 *
 * @param schema Registered schema with supplier variables and actions
 */
static void
demo_ruleset_batch(cdsl_schema_t* schema)
{
	print_separator("=== DEMO 6: RuleSet Batch Execution ===");

	cdsl_ruleset_t* set = cdsl_ruleset_create();

	const char* dsl_rules[] = {
	    "RULE blacklist_check {"
	    "    META { description = \"Blacklist compliance\" }"
	    "    WHEN supplier.is_blacklisted == true"
	    "    THEN reject_supplier(\"blacklisted\")"
	    "}",
	    "RULE capital_check {"
	    "    META { description = \"Capital threshold\" }"
	    "    WHEN supplier.registered_capital < 500000"
	    "    THEN reject_supplier(\"low_capital\")"
	    "}",
	    "RULE experience_check {"
	    "    META { description = \"Business experience\" }"
	    "    WHEN supplier.years_in_business < 2"
	    "    THEN record_warning(\"short_experience\")"
	    "}",
	};
	int priorities[] = {1, 2, 3};

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "reject_supplier", action_callback);
	cdsl_vm_register_action(vm, "record_warning", action_callback);

	for (int i = 0; i < 3; i++) {
		cdsl_rule_t* rule = parse_dsl(dsl_rules[i]);
		if (rule) {
			cdsl_ruleset_add(set, rule, priorities[i]);
		}
	}

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_bool(ctx, "supplier.is_blacklisted", 0);
	cdsl_context_set_int(ctx, "supplier.registered_capital", 300000);
	cdsl_context_set_int(ctx, "supplier.years_in_business", 1);

	cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset(vm, set, ctx);
	cdsl_ruleset_report_print(batch);

	cdsl_ruleset_report_free(batch);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_ruleset_free(set);
}

/**
 * @brief Main entry point for the C-DSL demo application.
 *
 * Sets up the schema with all demo variables and actions, then
 * runs all six demo scenarios in sequence.
 *
 * @return 0 on success
 */
int
main(void)
{
	printf("========================================\n");
	printf("   C-DSL Framework Demo\n");
	printf("   AI-Powered Rule Engine with Scoring\n");
	printf("========================================\n");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "supplier.registered_capital", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "supplier.years_in_business", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "supplier.is_blacklisted", CDSL_TYPE_BOOL);

	cdsl_schema_register_var(schema, "document.format", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "document.size_mb", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "document.has_digital_signature", CDSL_TYPE_BOOL);

	cdsl_schema_register_var(schema, "content.sensitive_words_count", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "content.contains_pii", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "content.ai_spam_score", CDSL_TYPE_FLOAT);

	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "reject_supplier", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "reject_document", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "block_content", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "record_warning", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_ai_config_t ai_cfg = cdsl_ai_config_default();

	demo_supplier_audit(schema, &ai_cfg);
	demo_doc_audit(schema, &ai_cfg);
	demo_content_audit(schema, &ai_cfg);
	demo_json_context();
	demo_simple_rules(schema);
	demo_ruleset_batch(schema);

	cdsl_schema_free(schema);
	return 0;
}
