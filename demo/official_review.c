/**
 * @file official_review.c
 * @brief 党政机关公文格式审查 (GB/T 9704-2012)
 *
 * Covers all checkable requirements from the standard:
 * - 5. 版面 (page size, margins, lines/chars)
 * - 7.2 版头 (份号, 密级, 紧急程度, 发文机关标志, 发文字号, 签发人, 分隔线)
 * - 7.3 主体 (标题, 主送机关, 正文, 附件说明)
 * - 7.4 版记 (分隔线, 抄送机关, 印发机关)
 * - 7.5 页码 (字体, 一字线)
 *
 * Loads DSL rules and executes against JSON context via
 * cdsl_context_load_json(). Outputs both human-readable
 * report and structured JSON with categorized results.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdsl/vm.h"
#include "cdsl/context.h"
#include "cdsl/report.h"
#include "cdsl/schema.h"

static void
action_callback(const char* action_name, cdsl_arg_node_t* args, void* user_data)
{
	(void)action_name;
	(void)args;
	(void)user_data;
}

/**
 * @brief Map metric name prefix to display category.
 */
static const char*
get_category(const char* metric_name)
{
	if (!metric_name) {
		return "其他";
	}
	if (strncmp(metric_name, "page_", 5) == 0 || strncmp(metric_name, "margin_", 7) == 0 ||
	    strncmp(metric_name, "lines_", 6) == 0 || strncmp(metric_name, "chars_", 6) == 0) {
		return "版面";
	}
	if (strncmp(metric_name, "hdr_", 4) == 0) {
		return "版头";
	}
	if (strncmp(metric_name, "body_", 5) == 0) {
		return "主体";
	}
	if (strncmp(metric_name, "rec_", 4) == 0) {
		return "版记";
	}
	if (strncmp(metric_name, "page_number_", 12) == 0) {
		return "页码";
	}
	return "其他";
}

/**
 * @brief Print per-metric result in structured format matching review schema.
 */
static void
print_structured_result(const cdsl_rule_report_t* report)
{
	if (!report || report->metric_count == 0) {
		return;
	}

	printf("\n===== 结构化审查报告 =====\n");
	printf("rule_name: %s\n", report->rule_name ? report->rule_name : "");
	printf("description: %s\n", report->description ? report->description : "");
	printf("status: %s\n",
	       report->status == CDSL_STATUS_PASSED		? "PASSED"
	       : report->status == CDSL_STATUS_PARTIALLY_PASSED ? "PARTIALLY_PASSED"
	       : report->status == CDSL_STATUS_FAILED		? "FAILED"
								: "ERROR");
	printf("score: %d/%d\n", report->total_obtained_score, report->total_max_score);
	printf("summary: %s\n\n", report->decision_summary ? report->decision_summary : "");

	const char* current_category = NULL;
	for (int i = 0; i < report->metric_count; i++) {
		const cdsl_metric_result_t* m = &report->metrics[i];
		const char* category = get_category(m->metric_name);

		if (!current_category || strcmp(category, current_category) != 0) {
			current_category = category;
			printf("--- [%s] ---\n", category);
		}

		printf("  {\n");
		printf("    \"title\": \"%s\",\n",
		       m->description ? m->description : m->metric_name);
		printf("    \"result\": \"%s\",\n", m->is_passed ? "PASSED" : "FAILED");
		printf("    \"score\": \"%d/%d\"%s\n",
		       m->score_obtained,
		       m->max_weight,
		       m->is_critical ? " [CRITICAL]" : "");
		if (m->violation_reason) {
			/* Parse violation_reason: "question | suggestion" format */
			char* reason = strdup(m->violation_reason);
			if (reason) {
				char* sep = strstr(reason, " | ");
				if (sep) {
					*sep = '\0';
					printf("    \"question\": \"%s\",\n", reason);
					printf("    \"suggestion\": \"%s\",\n", sep + 3);
				} else {
					printf("    \"question\": \"%s\",\n", reason);
				}
				free(reason);
			}
		}
		printf("  },\n");
	}
	printf("===== 报告结束 =====\n");
}

static cdsl_schema_t*
setup_schema(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();

	/* ---- 5. 版面 ---- */
	cdsl_schema_register_var(schema, "doc.page_width", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "doc.page_height", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "doc.margin_top", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "doc.margin_left", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "doc.lines_per_page", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "doc.chars_per_line", CDSL_TYPE_INT);

	/* ---- 7.2 版头 ---- */
	cdsl_schema_register_var(schema, "header.copy_number_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.has_secrecy", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "header.secrecy_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.has_urgency", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "header.urgency_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.org_logo_top_mm", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "header.org_logo_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.org_logo_color", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.serial_number_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.serial_bracket", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.serial_has_zero_padding", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "header.has_signer", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "header.signer_label_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.signer_name_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "header.separator_distance_mm", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "header.separator_color", CDSL_TYPE_STRING);

	/* ---- 7.3 主体 ---- */
	cdsl_schema_register_var(schema, "body.title_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "body.title_alignment", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "body.recipient_spacing_lines", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "body.recipient_indent", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "body.content_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "body.content_font_size", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "body.first_line_indent_chars", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "body.level1_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "body.level2_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "body.attachment_indent_chars", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "body.attachment_format_ok", CDSL_TYPE_BOOL);

	/* ---- 7.4 版记 ---- */
	cdsl_schema_register_var(schema, "rec.separator_width_mm", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "rec.has_copy_recipient", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "rec.copy_recipient_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "rec.copy_recipient_font_size", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "rec.issuer_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "rec.issuer_font_size", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "rec.issue_alignment", CDSL_TYPE_STRING);

	/* ---- 7.5 页码 ---- */
	cdsl_schema_register_var(schema, "page.number_font", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "page.number_font_size", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "page.number_has_dashes", CDSL_TYPE_BOOL);

	/* ---- 动作 ---- */
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);

	return schema;
}

static void
run_case(cdsl_vm_t* vm,
	 cdsl_rule_t* rule,
	 cdsl_schema_t* schema,
	 const char* case_name,
	 const char* json_ctx)
{
	printf("\n================================================\n");
	printf("  审查案例: %s\n", case_name);
	printf("================================================\n");

	cdsl_context_t* ctx = cdsl_context_create(schema);
	if (!ctx) {
		printf("  [ERROR] Failed to create context\n");
		return;
	}

	if (!cdsl_context_load_json(ctx, json_ctx)) {
		printf("  [ERROR] Failed to load JSON context\n");
		cdsl_context_free(ctx);
		return;
	}

	cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);
	if (!report) {
		printf("  [ERROR] Execution returned NULL\n");
		cdsl_context_free(ctx);
		return;
	}

	/* Print standard report */
	cdsl_report_print(report);

	/* Print structured output matching review schema */
	print_structured_result(report);

	/* Generate JSON output */
	char* json_out = cdsl_report_to_json(report);
	if (json_out) {
		printf("\n----- JSON Output -----\n%s\n", json_out);
		free(json_out);
	}

	cdsl_report_free(report);
	cdsl_context_free(ctx);
}

int
main(void)
{
	printf("========================================\n");
	printf("  党政机关公文格式审查\n");
	printf("  覆盖: 版面 | 版头 | 主体 | 版记 | 页码\n");
	printf("  标准: GB/T 9704-2012\n");
	printf("========================================\n");

	cdsl_schema_t* schema = setup_schema();

	/* Read DSL file */
	FILE* f = fopen("demo/official_doc.dsl", "r");
	if (!f) {
		printf("Failed to open demo/official_doc.dsl\n");
		cdsl_schema_free(schema);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* dsl = malloc((size_t)fsize + 1);
	if (!dsl) {
		printf("Failed to allocate memory\n");
		fclose(f);
		cdsl_schema_free(schema);
		return 1;
	}
	size_t nread = fread(dsl, 1, (size_t)fsize, f);
	dsl[nread] = '\0';
	fclose(f);

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	free(dsl);
	if (!rule) {
		printf("Failed to parse DSL\n");
		cdsl_schema_free(schema);
		return 1;
	}

	char err[512];
	if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
		printf("Verification failed: %s\n", err);
		cdsl_free_rule(rule);
		cdsl_schema_free(schema);
		return 1;
	}

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", action_callback);
	cdsl_vm_register_action(vm, "fail_metric", action_callback);

	/* ================================================================
	 * Case 1: 完全合规文档
	 *   版面、版头、主体、版记、页码全部符合 GB/T 9704-2012
	 * ================================================================ */
	const char* case_compliant = "{"
				     "\"doc\": {"
				     "\"page_width\": 210,"
				     "\"page_height\": 297,"
				     "\"margin_top\": 37,"
				     "\"margin_left\": 28,"
				     "\"lines_per_page\": 22,"
				     "\"chars_per_line\": 28"
				     "},"
				     "\"header\": {"
				     "\"copy_number_font\": \"仿宋\","
				     "\"has_secrecy\": true,"
				     "\"secrecy_font\": \"黑体\","
				     "\"has_urgency\": true,"
				     "\"urgency_font\": \"黑体\","
				     "\"org_logo_top_mm\": 35.0,"
				     "\"org_logo_font\": \"小标宋\","
				     "\"org_logo_color\": \"red\","
				     "\"serial_number_font\": \"仿宋\","
				     "\"serial_bracket\": \"六角括号\","
				     "\"serial_has_zero_padding\": false,"
				     "\"has_signer\": true,"
				     "\"signer_label_font\": \"仿宋\","
				     "\"signer_name_font\": \"楷体\","
				     "\"separator_distance_mm\": 4.0,"
				     "\"separator_color\": \"red\""
				     "},"
				     "\"body\": {"
				     "\"title_font\": \"小标宋\","
				     "\"title_alignment\": \"center\","
				     "\"recipient_spacing_lines\": 1,"
				     "\"recipient_indent\": 0,"
				     "\"content_font\": \"仿宋\","
				     "\"content_font_size\": 3,"
				     "\"first_line_indent_chars\": 2,"
				     "\"level1_font\": \"黑体\","
				     "\"level2_font\": \"楷体\","
				     "\"attachment_indent_chars\": 2,"
				     "\"attachment_format_ok\": true"
				     "},"
				     "\"rec\": {"
				     "\"separator_width_mm\": 156.0,"
				     "\"has_copy_recipient\": true,"
				     "\"copy_recipient_font\": \"仿宋\","
				     "\"copy_recipient_font_size\": 4,"
				     "\"issuer_font\": \"仿宋\","
				     "\"issuer_font_size\": 4,"
				     "\"issue_alignment\": \"两端对齐\""
				     "},"
				     "\"page\": {"
				     "\"number_font\": \"半角宋体\","
				     "\"number_font_size\": 4,"
				     "\"number_has_dashes\": true"
				     "}"
				     "}";
	run_case(vm, rule, schema, "完全合规文档", case_compliant);

	/* ================================================================
	 * Case 2: 典型不合规文档
	 *   版面尺寸错误，版头多处缺失/字体错误，标题非小标宋，
	 *   正文非仿宋，主送机关缩进错误，版记字体错误，页码缺一字线
	 * ================================================================ */
	const char* case_noncompliant = "{"
					"\"doc\": {"
					"\"page_width\": 210,"
					"\"page_height\": 297,"
					"\"margin_top\": 40,"
					"\"margin_left\": 25,"
					"\"lines_per_page\": 22,"
					"\"chars_per_line\": 28"
					"},"
					"\"header\": {"
					"\"copy_number_font\": \"宋体\","
					"\"has_secrecy\": false,"
					"\"secrecy_font\": \"\","
					"\"has_urgency\": false,"
					"\"urgency_font\": \"\","
					"\"org_logo_top_mm\": 36.0,"
					"\"org_logo_font\": \"黑体\","
					"\"org_logo_color\": \"black\","
					"\"serial_number_font\": \"宋体\","
					"\"serial_bracket\": \"方括号\","
					"\"serial_has_zero_padding\": true,"
					"\"has_signer\": false,"
					"\"signer_label_font\": \"\","
					"\"signer_name_font\": \"\","
					"\"separator_distance_mm\": 58.4,"
					"\"separator_color\": \"black\""
					"},"
					"\"body\": {"
					"\"title_font\": \"宋体\","
					"\"title_alignment\": \"left\","
					"\"recipient_spacing_lines\": 5,"
					"\"recipient_indent\": 2,"
					"\"content_font\": \"宋体\","
					"\"content_font_size\": 4,"
					"\"first_line_indent_chars\": 0,"
					"\"level1_font\": \"宋体\","
					"\"level2_font\": \"仿宋\","
					"\"attachment_indent_chars\": 0,"
					"\"attachment_format_ok\": false"
					"},"
					"\"rec\": {"
					"\"separator_width_mm\": 160.0,"
					"\"has_copy_recipient\": true,"
					"\"copy_recipient_font\": \"宋体\","
					"\"copy_recipient_font_size\": 5,"
					"\"issuer_font\": \"宋体\","
					"\"issuer_font_size\": 5,"
					"\"issue_alignment\": \"居中\""
					"},"
					"\"page\": {"
					"\"number_font\": \"宋体\","
					"\"number_font_size\": 5,"
					"\"number_has_dashes\": false"
					"}"
					"}";
	run_case(vm,
		 rule,
		 schema,
		 "典型不合规文档（版面/版头/主体/版记/页码全量问题）",
		 case_noncompliant);

	/* ================================================================
	 * Case 3: 边界合规案例
	 *   版面容差边界 (±1mm)，版头全合规，正文格式正确，
	 *   无密级/紧急程度/签发人（可选，不触发字体检查失败）
	 * ================================================================ */
	const char* case_edge = "{"
				"\"doc\": {"
				"\"page_width\": 210,"
				"\"page_height\": 297,"
				"\"margin_top\": 36,"
				"\"margin_left\": 27,"
				"\"lines_per_page\": 22,"
				"\"chars_per_line\": 28"
				"},"
				"\"header\": {"
				"\"copy_number_font\": \"仿宋\","
				"\"has_secrecy\": false,"
				"\"secrecy_font\": \"\","
				"\"has_urgency\": false,"
				"\"urgency_font\": \"\","
				"\"org_logo_top_mm\": 34.7,"
				"\"org_logo_font\": \"小标宋\","
				"\"org_logo_color\": \"red\","
				"\"serial_number_font\": \"仿宋\","
				"\"serial_bracket\": \"六角括号\","
				"\"serial_has_zero_padding\": false,"
				"\"has_signer\": false,"
				"\"signer_label_font\": \"\","
				"\"signer_name_font\": \"\","
				"\"separator_distance_mm\": 4.2,"
				"\"separator_color\": \"red\""
				"},"
				"\"body\": {"
				"\"title_font\": \"小标宋\","
				"\"title_alignment\": \"center\","
				"\"recipient_spacing_lines\": 1,"
				"\"recipient_indent\": 0,"
				"\"content_font\": \"仿宋\","
				"\"content_font_size\": 3,"
				"\"first_line_indent_chars\": 2,"
				"\"level1_font\": \"黑体\","
				"\"level2_font\": \"楷体\","
				"\"attachment_indent_chars\": 2,"
				"\"attachment_format_ok\": true"
				"},"
				"\"rec\": {"
				"\"separator_width_mm\": 155.7,"
				"\"has_copy_recipient\": false,"
				"\"copy_recipient_font\": \"\","
				"\"copy_recipient_font_size\": 0,"
				"\"issuer_font\": \"仿宋\","
				"\"issuer_font_size\": 4,"
				"\"issue_alignment\": \"两端对齐\""
				"},"
				"\"page\": {"
				"\"number_font\": \"半角宋体\","
				"\"number_font_size\": 4,"
				"\"number_has_dashes\": true"
				"}"
				"}";
	run_case(vm, rule, schema, "边界合规（版面容差/版头/主体/版记/页码）", case_edge);

	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	return 0;
}
