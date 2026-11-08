/**
 * @file src/vm/dtable.c
 * @brief Enterprise Decision Table implementation.
 */

#include "cdsl/dtable.h"
#include "cdsl/context.h"
#include "cdsl/util/portability.h"
#include "value_ops.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cdsl_dtable_t*
cdsl_dtable_create(const char* name, cdsl_hit_policy_t hit_policy)
{
	if (!name) {
		return NULL;
	}
	cdsl_dtable_t* dt = calloc(1, sizeof(*dt));
	if (!dt) {
		return NULL;
	}
	dt->name = cdsl_strdup(name);
	if (!dt->name) {
		free(dt);
		return NULL;
	}
	dt->hit_policy = hit_policy;
	return dt;
}

int
cdsl_dtable_add_input(cdsl_dtable_t* dt, const char* name, cdsl_type_t type)
{
	if (!dt || !name) {
		return -1;
	}
	size_t new_cap = dt->num_inputs + 1;
	cdsl_dtable_col_t* new_cols = realloc(dt->input_cols, new_cap * sizeof(cdsl_dtable_col_t));
	if (!new_cols) {
		return -1;
	}
	dt->input_cols = new_cols;
	dt->input_cols[dt->num_inputs].name = cdsl_strdup(name);
	if (!dt->input_cols[dt->num_inputs].name) {
		return -1;
	}
	dt->input_cols[dt->num_inputs].type = type;
	dt->num_inputs++;
	return 0;
}

int
cdsl_dtable_add_output(cdsl_dtable_t* dt, const char* name, cdsl_type_t type)
{
	if (!dt || !name) {
		return -1;
	}
	size_t new_cap = dt->num_outputs + 1;
	cdsl_dtable_col_t* new_cols = realloc(dt->output_cols, new_cap * sizeof(cdsl_dtable_col_t));
	if (!new_cols) {
		return -1;
	}
	dt->output_cols = new_cols;
	dt->output_cols[dt->num_outputs].name = cdsl_strdup(name);
	if (!dt->output_cols[dt->num_outputs].name) {
		return -1;
	}
	dt->output_cols[dt->num_outputs].type = type;
	dt->num_outputs++;
	return 0;
}

int
cdsl_dtable_add_row(cdsl_dtable_t* dt,
		    const cdsl_dtable_cell_t* inputs,
		    const cdsl_dtable_cell_t* outputs)
{
	if (!dt || (!inputs && dt->num_inputs > 0) || (!outputs && dt->num_outputs > 0)) {
		return -1;
	}
	cdsl_dtable_row_t* r = calloc(1, sizeof(*r));
	if (!r) {
		return -1;
	}
	r->row_id = dt->num_rows + 1;

	if (dt->num_inputs > 0) {
		r->inputs = calloc(dt->num_inputs, sizeof(cdsl_dtable_cell_t));
		if (!r->inputs) {
			free(r);
			return -1;
		}
		for (size_t i = 0; i < dt->num_inputs; i++) {
			r->inputs[i] = inputs[i];
		}
	}

	if (dt->num_outputs > 0) {
		r->outputs = calloc(dt->num_outputs, sizeof(cdsl_dtable_cell_t));
		if (!r->outputs) {
			free(r->inputs);
			free(r);
			return -1;
		}
		for (size_t i = 0; i < dt->num_outputs; i++) {
			r->outputs[i] = outputs[i];
		}
	}

	if (!dt->rows) {
		dt->rows = r;
	} else {
		cdsl_dtable_row_t* curr = dt->rows;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = r;
	}
	dt->num_rows++;
	return 0;
}

int
cdsl_dtable_set_default(cdsl_dtable_t* dt, const cdsl_dtable_cell_t* default_outputs)
{
	if (!dt || (!default_outputs && dt->num_outputs > 0)) {
		return -1;
	}
	if (dt->default_outputs) {
		for (size_t i = 0; i < dt->num_outputs; i++) {
			if (dt->default_outputs[i].expr) {
				cdsl_free_expr(dt->default_outputs[i].expr);
			}
		}
		free(dt->default_outputs);
		dt->default_outputs = NULL;
	}
	if (dt->num_outputs > 0) {
		dt->default_outputs = calloc(dt->num_outputs, sizeof(cdsl_dtable_cell_t));
		if (!dt->default_outputs) {
			return -1;
		}
		for (size_t i = 0; i < dt->num_outputs; i++) {
			dt->default_outputs[i] = default_outputs[i];
		}
	}
	return 0;
}

void
cdsl_dtable_free(cdsl_dtable_t* dt)
{
	if (!dt) {
		return;
	}
	if (dt->name) {
		free(dt->name);
	}
	if (dt->input_cols) {
		for (size_t i = 0; i < dt->num_inputs; i++) {
			if (dt->input_cols[i].name) {
				free(dt->input_cols[i].name);
			}
		}
		free(dt->input_cols);
	}
	if (dt->output_cols) {
		for (size_t i = 0; i < dt->num_outputs; i++) {
			if (dt->output_cols[i].name) {
				free(dt->output_cols[i].name);
			}
		}
		free(dt->output_cols);
	}

	cdsl_dtable_row_t* r = dt->rows;
	while (r) {
		cdsl_dtable_row_t* next = r->next;
		if (r->inputs) {
			if (!dt->arena) {
				for (size_t i = 0; i < dt->num_inputs; i++) {
					if (r->inputs[i].expr) {
						cdsl_free_expr(r->inputs[i].expr);
					}
				}
			}
			free(r->inputs);
		}
		if (r->outputs) {
			if (!dt->arena) {
				for (size_t i = 0; i < dt->num_outputs; i++) {
					if (r->outputs[i].expr) {
						cdsl_free_expr(r->outputs[i].expr);
					}
				}
			}
			free(r->outputs);
		}
		free(r);
		r = next;
	}

	if (dt->default_outputs) {
		if (!dt->arena) {
			for (size_t i = 0; i < dt->num_outputs; i++) {
				if (dt->default_outputs[i].expr) {
					cdsl_free_expr(dt->default_outputs[i].expr);
				}
			}
		}
		free(dt->default_outputs);
	}

	if (dt->arena) {
		cdsl_arena_free(dt->arena);
	}

	free(dt);
}

cdsl_dtable_report_t*
cdsl_dtable_eval(const cdsl_dtable_t* dt, cdsl_context_t* ctx)
{
	if (!dt || !ctx) {
		return NULL;
	}
	cdsl_dtable_report_t* rpt = calloc(1, sizeof(*rpt));
	if (!rpt) {
		return NULL;
	}
	rpt->table_name = cdsl_strdup(dt->name);
	rpt->num_outputs = dt->num_outputs;

	cdsl_dtable_row_t* r = dt->rows;
	while (r) {
		bool row_matched = true;
		for (size_t i = 0; i < dt->num_inputs; i++) {
			if (r->inputs[i].kind == CDSL_CELL_WILDCARD) {
				continue;
			}
			if (!r->inputs[i].expr) {
				continue;
			}
			cdsl_value_t v =
			    cdsl_eval_expr_internal(r->inputs[i].expr, ctx, NULL, 0, 0);
			if (v.type == CDSL_TYPE_BOOL) {
				if (!v.data.bool_val) {
					row_matched = false;
				}
			} else {
				const char* col_name = dt->input_cols[i].name;
				const cdsl_value_t* cv = cdsl_context_lookup_value(ctx, col_name);
				if (!cv) {
					row_matched = false;
				} else {
					cdsl_value_t eq = {.type = CDSL_TYPE_BOOL,
							   .data = {.bool_val = 0}};
					if (cdsl_value_binop(CDSL_OP_EQ, cv, &v, &eq) !=
						CDSL_VALOP_OK ||
					    !eq.data.bool_val) {
						row_matched = false;
					}
					cdsl_value_free(&eq);
				}
			}
			cdsl_value_free(&v);
			if (!row_matched) {
				break;
			}
		}

		if (row_matched) {
			cdsl_value_t* row_outputs =
			    calloc(dt->num_outputs > 0 ? dt->num_outputs : 1, sizeof(cdsl_value_t));
			if (!row_outputs) {
				cdsl_dtable_report_free(rpt);
				return NULL;
			}
			for (size_t k = 0; k < dt->num_outputs; k++) {
				if (r->outputs[k].expr) {
					row_outputs[k] = cdsl_eval_expr_internal(
					    r->outputs[k].expr, ctx, NULL, 0, 0);
				} else {
					row_outputs[k].type = CDSL_TYPE_VOID;
				}
			}

			if (dt->hit_policy == CDSL_HIT_FIRST) {
				rpt->matched = true;
				rpt->num_matches = 1;
				rpt->matches = calloc(1, sizeof(cdsl_dtable_match_t));
				if (!rpt->matches) {
					for (size_t k = 0; k < dt->num_outputs; k++) {
						cdsl_value_free(&row_outputs[k]);
					}
					free(row_outputs);
					cdsl_dtable_report_free(rpt);
					return NULL;
				}
				rpt->matches[0].row_id = r->row_id;
				rpt->matches[0].outputs = row_outputs;

				if (dt->num_outputs > 0) {
					rpt->final_outputs =
					    calloc(dt->num_outputs, sizeof(cdsl_value_t));
					if (rpt->final_outputs) {
						for (size_t k = 0; k < dt->num_outputs; k++) {
							rpt->final_outputs[k] =
							    cdsl_value_copy(&row_outputs[k]);
						}
					}
				}
				return rpt;
			} else if (dt->hit_policy == CDSL_HIT_RULE_ORDER) {
				rpt->matched = true;
				size_t new_count = rpt->num_matches + 1;
				cdsl_dtable_match_t* new_matches =
				    realloc(rpt->matches, new_count * sizeof(cdsl_dtable_match_t));
				if (!new_matches) {
					for (size_t k = 0; k < dt->num_outputs; k++) {
						cdsl_value_free(&row_outputs[k]);
					}
					free(row_outputs);
					cdsl_dtable_report_free(rpt);
					return NULL;
				}
				rpt->matches = new_matches;
				rpt->matches[rpt->num_matches].row_id = r->row_id;
				rpt->matches[rpt->num_matches].outputs = row_outputs;
				if (rpt->num_matches == 0 && dt->num_outputs > 0) {
					rpt->final_outputs =
					    calloc(dt->num_outputs, sizeof(cdsl_value_t));
					if (rpt->final_outputs) {
						for (size_t k = 0; k < dt->num_outputs; k++) {
							rpt->final_outputs[k] =
							    cdsl_value_copy(&row_outputs[k]);
						}
					}
				}
				rpt->num_matches = new_count;
			} else if (dt->hit_policy == CDSL_HIT_UNIQUE) {
				if (rpt->num_matches > 0) {
					rpt->matched = false;
					rpt->error_message = cdsl_strdup(
					    "UNIQUE hit policy violation: multiple rows matched");
					for (size_t k = 0; k < dt->num_outputs; k++) {
						cdsl_value_free(&row_outputs[k]);
					}
					free(row_outputs);
					return rpt;
				}
				rpt->matched = true;
				rpt->num_matches = 1;
				rpt->matches = calloc(1, sizeof(cdsl_dtable_match_t));
				if (!rpt->matches) {
					for (size_t k = 0; k < dt->num_outputs; k++) {
						cdsl_value_free(&row_outputs[k]);
					}
					free(row_outputs);
					cdsl_dtable_report_free(rpt);
					return NULL;
				}
				rpt->matches[0].row_id = r->row_id;
				rpt->matches[0].outputs = row_outputs;

				if (dt->num_outputs > 0) {
					rpt->final_outputs =
					    calloc(dt->num_outputs, sizeof(cdsl_value_t));
					if (rpt->final_outputs) {
						for (size_t k = 0; k < dt->num_outputs; k++) {
							rpt->final_outputs[k] =
							    cdsl_value_copy(&row_outputs[k]);
						}
					}
				}
			}
		}
		r = r->next;
	}

	if (!rpt->matched && dt->default_outputs && !rpt->error_message && dt->num_outputs > 0) {
		rpt->final_outputs = calloc(dt->num_outputs, sizeof(cdsl_value_t));
		if (rpt->final_outputs) {
			for (size_t k = 0; k < dt->num_outputs; k++) {
				if (dt->default_outputs[k].expr) {
					rpt->final_outputs[k] = cdsl_eval_expr_internal(
					    dt->default_outputs[k].expr, ctx, NULL, 0, 0);
				}
			}
			rpt->matched = true;
		}
	}

	return rpt;
}

void
cdsl_dtable_report_free(cdsl_dtable_report_t* rpt)
{
	if (!rpt) {
		return;
	}
	if (rpt->table_name) {
		free(rpt->table_name);
	}
	if (rpt->error_message) {
		free(rpt->error_message);
	}
	if (rpt->matches) {
		for (size_t i = 0; i < rpt->num_matches; i++) {
			if (rpt->matches[i].outputs) {
				for (size_t k = 0; k < rpt->num_outputs; k++) {
					cdsl_value_free(&rpt->matches[i].outputs[k]);
				}
				free(rpt->matches[i].outputs);
			}
		}
		free(rpt->matches);
	}
	if (rpt->final_outputs) {
		for (size_t k = 0; k < rpt->num_outputs; k++) {
			cdsl_value_free(&rpt->final_outputs[k]);
		}
		free(rpt->final_outputs);
	}
	free(rpt);
}

int
cdsl_schema_verify_dtable(const cdsl_dtable_t* dt,
			  const cdsl_schema_t* schema,
			  cdsl_error_list_t* errors)
{
	if (!dt || !schema) {
		return -1;
	}
	int err_count = 0;
	for (size_t i = 0; i < dt->num_inputs; i++) {
		bool found = false;
		for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
			if (v->name && strcmp(v->name, dt->input_cols[i].name) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			if (errors) {
				char buf[128];
				snprintf(buf,
					 sizeof(buf),
					 "Decision table input '%s' not found in schema",
					 dt->input_cols[i].name);
				cdsl_error_list_add(
				    errors, cdsl_error_create(CDSL_ERR_TYPE, 0, 0, buf, NULL));
			}
			err_count++;
		}
	}
	return err_count == 0 ? 0 : -1;
}

/* Helper to parse expressions via cdsl_parse_string */
static cdsl_expr_node_t*
parse_cell_expr(const char* expr_str, cdsl_arena_t* target_arena)
{
	if (!expr_str) {
		return NULL;
	}
	while (isspace((unsigned char)*expr_str)) {
		expr_str++;
	}
	if (*expr_str == '\0' || strcmp(expr_str, "_") == 0) {
		return NULL;
	}

	char buf[2048];
	snprintf(buf, sizeof(buf), "RULE __dtable_temp__ { WHEN %s THEN dummy() }", expr_str);
	cdsl_rule_t* rule = cdsl_parse_string(buf, NULL);
	if (!rule) {
		return NULL;
	}
	cdsl_expr_node_t* expr = rule->when_expr;

	if (rule->arena && target_arena) {
		if (!target_arena->blocks) {
			target_arena->blocks = rule->arena->blocks;
		} else {
			cdsl_arena_block_t* b = target_arena->blocks;
			while (b->next) {
				b = b->next;
			}
			b->next = rule->arena->blocks;
		}
		rule->arena->blocks = NULL;
	}
	cdsl_free_rule(rule);
	return expr;
}

static char*
split_next(char** str, char delim)
{
	if (!str || !*str) {
		return NULL;
	}
	char* start = *str;
	while (*start && isspace((unsigned char)*start)) {
		start++;
	}
	if (*start == '\0') {
		*str = NULL;
		return NULL;
	}
	char* end = strchr(start, delim);
	if (end) {
		*end = '\0';
		*str = end + 1;
	} else {
		*str = NULL;
	}
	char* t = start + strlen(start) - 1;
	while (t >= start && isspace((unsigned char)*t)) {
		*t = '\0';
		t--;
	}
	return start;
}

cdsl_dtable_t*
cdsl_dtable_parse_string(const char* dsl, cdsl_error_list_t* errors)
{
	if (!dsl) {
		return NULL;
	}

	const char* p = dsl;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	if (strncmp(p, "DECISION_TABLE", 14) != 0) {
		if (errors) {
			cdsl_error_list_add(
			    errors,
			    cdsl_error_create(
				CDSL_ERR_SYNTAX, 1, 1, "Expected DECISION_TABLE", NULL));
		}
		return NULL;
	}
	p += 14;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	/* Extract name */
	char name[128] = {0};
	size_t ni = 0;
	while (*p && !isspace((unsigned char)*p) && *p != '(' && ni + 1 < sizeof(name)) {
		name[ni++] = *p++;
	}

	cdsl_dtable_t* dt = cdsl_dtable_create(name, CDSL_HIT_FIRST);
	if (!dt) {
		return NULL;
	}
	dt->arena = cdsl_arena_create(CDSL_ARENA_DEFAULT_BLOCK_SIZE);

	/* Parse body lines */
	char line[1024] = {0};
	while (*p) {
		while (*p == '\r' || *p == '\n') {
			p++;
		}
		if (!*p) {
			break;
		}

		/* Read one line */
		size_t li = 0;
		while (*p && *p != '\n' && *p != '\r' && li + 1 < sizeof(line)) {
			line[li++] = *p++;
		}
		line[li] = '\0';

		char* cur = line;
		while (isspace((unsigned char)*cur)) {
			cur++;
		}
		if (*cur == '\0' || *cur == '#') {
			continue;
		}

		if (strncmp(cur, "END", 3) == 0) {
			break;
		}

		if (strncmp(cur, "HIT_POLICY:", 11) == 0) {
			cur += 11;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			if (strncmp(cur, "FIRST", 5) == 0) {
				dt->hit_policy = CDSL_HIT_FIRST;
			} else if (strncmp(cur, "RULE_ORDER", 10) == 0) {
				dt->hit_policy = CDSL_HIT_RULE_ORDER;
			} else if (strncmp(cur, "UNIQUE", 6) == 0) {
				dt->hit_policy = CDSL_HIT_UNIQUE;
			}
			continue;
		}

		if (strncmp(cur, "INPUTS", 6) == 0) {
			cur += 6;
			while (*cur && *cur != '(') {
				cur++;
			}
			if (*cur == '(') {
				cur++;
			}
			char* end_paren = strrchr(cur, ')');
			if (end_paren) {
				*end_paren = '\0';
			}

			char* scan = cur;
			char* token;
			while ((token = split_next(&scan, ',')) != NULL) {
				char* colon = strchr(token, ':');
				if (colon) {
					*colon = '\0';
					char* col_type = colon + 1;
					while (isspace((unsigned char)*col_type)) {
						col_type++;
					}
					char* tend = token + strlen(token) - 1;
					while (tend > token && isspace((unsigned char)*tend)) {
						*tend-- = '\0';
					}

					cdsl_type_t t = CDSL_TYPE_INT;
					if (strncmp(col_type, "FLOAT", 5) == 0) {
						t = CDSL_TYPE_FLOAT;
					} else if (strncmp(col_type, "STRING", 6) == 0) {
						t = CDSL_TYPE_STRING;
					} else if (strncmp(col_type, "BOOL", 4) == 0) {
						t = CDSL_TYPE_BOOL;
					}

					cdsl_dtable_add_input(dt, token, t);
				}
			}
			continue;
		}

		if (strncmp(cur, "OUTPUTS", 7) == 0) {
			cur += 7;
			while (*cur && *cur != '(') {
				cur++;
			}
			if (*cur == '(') {
				cur++;
			}
			char* end_paren = strrchr(cur, ')');
			if (end_paren) {
				*end_paren = '\0';
			}

			char* scan = cur;
			char* token;
			while ((token = split_next(&scan, ',')) != NULL) {
				char* colon = strchr(token, ':');
				if (colon) {
					*colon = '\0';
					char* col_type = colon + 1;
					while (isspace((unsigned char)*col_type)) {
						col_type++;
					}
					char* tend = token + strlen(token) - 1;
					while (tend > token && isspace((unsigned char)*tend)) {
						*tend-- = '\0';
					}

					cdsl_type_t t = CDSL_TYPE_STRING;
					if (strncmp(col_type, "INT", 3) == 0) {
						t = CDSL_TYPE_INT;
					} else if (strncmp(col_type, "FLOAT", 5) == 0) {
						t = CDSL_TYPE_FLOAT;
					} else if (strncmp(col_type, "BOOL", 4) == 0) {
						t = CDSL_TYPE_BOOL;
					}

					cdsl_dtable_add_output(dt, token, t);
				}
			}
			continue;
		}

		if (strncmp(cur, "ROW:", 4) == 0) {
			cur += 4;
			char* arrow = strstr(cur, "->");
			if (!arrow) {
				continue;
			}
			*arrow = '\0';
			char* input_part = cur;
			char* output_part = arrow + 2;

			cdsl_dtable_cell_t* in_cells = calloc(
			    dt->num_inputs > 0 ? dt->num_inputs : 1, sizeof(cdsl_dtable_cell_t));
			cdsl_dtable_cell_t* out_cells = calloc(
			    dt->num_outputs > 0 ? dt->num_outputs : 1, sizeof(cdsl_dtable_cell_t));

			/* Parse comma separated inputs */
			size_t in_idx = 0;
			char* in_scan = input_part;
			char* tok;
			while ((tok = split_next(&in_scan, ',')) != NULL &&
			       in_idx < dt->num_inputs) {
				if (strcmp(tok, "_") == 0) {
					in_cells[in_idx].kind = CDSL_CELL_WILDCARD;
				} else {
					in_cells[in_idx].kind = CDSL_CELL_EXPR;
					in_cells[in_idx].expr = parse_cell_expr(tok, dt->arena);
				}
				in_idx++;
			}

			/* Parse comma separated outputs */
			size_t out_idx = 0;
			char* out_scan = output_part;
			while ((tok = split_next(&out_scan, ',')) != NULL &&
			       out_idx < dt->num_outputs) {
				out_cells[out_idx].kind = CDSL_CELL_EXPR;
				out_cells[out_idx].expr = parse_cell_expr(tok, dt->arena);
				out_idx++;
			}

			cdsl_dtable_add_row(dt, in_cells, out_cells);
			free(in_cells);
			free(out_cells);
			continue;
		}

		if (strncmp(cur, "DEFAULT:", 8) == 0) {
			cur += 8;
			cdsl_dtable_cell_t* def_cells = calloc(
			    dt->num_outputs > 0 ? dt->num_outputs : 1, sizeof(cdsl_dtable_cell_t));
			size_t out_idx = 0;
			char* def_scan = cur;
			char* tok;
			while ((tok = split_next(&def_scan, ',')) != NULL &&
			       out_idx < dt->num_outputs) {
				def_cells[out_idx].kind = CDSL_CELL_EXPR;
				def_cells[out_idx].expr = parse_cell_expr(tok, dt->arena);
				out_idx++;
			}
			cdsl_dtable_set_default(dt, def_cells);
			free(def_cells);
			continue;
		}
	}

	return dt;
}
