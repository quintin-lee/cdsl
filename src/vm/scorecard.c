/**
 * @file src/vm/scorecard.c
 * @brief Standard Scorecard modeling and evaluation implementation.
 */

#include "cdsl/scorecard.h"
#include "cdsl/context.h"
#include "cdsl/util/portability.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cdsl_scorecard_t*
cdsl_scorecard_create(const char* name, double baseline, const char* output_var)
{
	if (!name) {
		return NULL;
	}
	cdsl_scorecard_t* sc = calloc(1, sizeof(*sc));
	if (!sc) {
		return NULL;
	}
	sc->name = cdsl_strdup(name);
	if (!sc->name) {
		free(sc);
		return NULL;
	}
	sc->baseline_score = baseline;
	if (output_var) {
		sc->output_var = cdsl_strdup(output_var);
	}
	return sc;
}

int
cdsl_scorecard_add_characteristic(cdsl_scorecard_t* sc, const char* name, double default_delta)
{
	if (!sc || !name) {
		return -1;
	}
	cdsl_scorecard_char_t* ch = calloc(1, sizeof(*ch));
	if (!ch) {
		return -1;
	}
	ch->name = cdsl_strdup(name);
	if (!ch->name) {
		free(ch);
		return -1;
	}
	ch->default_delta = default_delta;

	if (!sc->characteristics) {
		sc->characteristics = ch;
	} else {
		cdsl_scorecard_char_t* curr = sc->characteristics;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = ch;
	}
	sc->num_characteristics++;
	return 0;
}

int
cdsl_scorecard_add_case(cdsl_scorecard_t* sc,
			const char* char_name,
			cdsl_expr_node_t* condition,
			double delta)
{
	if (!sc || !char_name || !condition) {
		return -1;
	}
	cdsl_scorecard_char_t* ch = sc->characteristics;
	while (ch) {
		if (strcmp(ch->name, char_name) == 0) {
			break;
		}
		ch = ch->next;
	}
	if (!ch) {
		return -1;
	}

	cdsl_scorecard_case_t* cs = calloc(1, sizeof(*cs));
	if (!cs) {
		return -1;
	}
	cs->condition = condition;
	cs->delta = delta;

	if (!ch->cases) {
		ch->cases = cs;
	} else {
		cdsl_scorecard_case_t* curr = ch->cases;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = cs;
	}
	return 0;
}

void
cdsl_scorecard_free(cdsl_scorecard_t* sc)
{
	if (!sc) {
		return;
	}
	if (sc->name) {
		free(sc->name);
	}
	if (sc->output_var) {
		free(sc->output_var);
	}

	cdsl_scorecard_char_t* ch = sc->characteristics;
	while (ch) {
		cdsl_scorecard_char_t* next_ch = ch->next;
		if (ch->name) {
			free(ch->name);
		}
		cdsl_scorecard_case_t* cs = ch->cases;
		while (cs) {
			cdsl_scorecard_case_t* next_cs = cs->next;
			if (!sc->arena && cs->condition) {
				cdsl_free_expr(cs->condition);
			}
			free(cs);
			cs = next_cs;
		}
		free(ch);
		ch = next_ch;
	}

	if (sc->arena) {
		cdsl_arena_free(sc->arena);
	}

	free(sc);
}

cdsl_scorecard_report_t*
cdsl_scorecard_eval(const cdsl_scorecard_t* sc, cdsl_context_t* ctx)
{
	if (!sc || !ctx) {
		return NULL;
	}
	cdsl_scorecard_report_t* rpt = calloc(1, sizeof(*rpt));
	if (!rpt) {
		return NULL;
	}
	rpt->scorecard_name = cdsl_strdup(sc->name);
	rpt->baseline_score = sc->baseline_score;
	rpt->final_score = sc->baseline_score;
	rpt->num_contributions = sc->num_characteristics;
	if (sc->num_characteristics > 0) {
		rpt->contributions =
		    calloc(sc->num_characteristics, sizeof(cdsl_char_contribution_t));
		if (!rpt->contributions) {
			cdsl_scorecard_report_free(rpt);
			return NULL;
		}
	}

	size_t ci = 0;
	cdsl_scorecard_char_t* ch = sc->characteristics;
	while (ch) {
		int case_idx = 0;
		bool matched = false;
		cdsl_scorecard_case_t* cs = ch->cases;
		while (cs) {
			if (cs->condition) {
				cdsl_value_t v =
				    cdsl_eval_expr_internal(cs->condition, ctx, NULL, 0, 0);
				if (v.type == CDSL_TYPE_BOOL && v.data.bool_val != 0) {
					matched = true;
					rpt->final_score += cs->delta;
					rpt->contributions[ci].char_name = cdsl_strdup(ch->name);
					rpt->contributions[ci].delta = cs->delta;
					rpt->contributions[ci].matched_case_idx = case_idx;
					cdsl_value_free(&v);
					break;
				}
				cdsl_value_free(&v);
			}
			case_idx++;
			cs = cs->next;
		}

		if (!matched) {
			rpt->final_score += ch->default_delta;
			rpt->contributions[ci].char_name = cdsl_strdup(ch->name);
			rpt->contributions[ci].delta = ch->default_delta;
			rpt->contributions[ci].matched_case_idx = -1;
		}

		ci++;
		ch = ch->next;
	}

	if (sc->output_var) {
		cdsl_context_set_float(ctx, sc->output_var, rpt->final_score);
	}

	return rpt;
}

void
cdsl_scorecard_report_free(cdsl_scorecard_report_t* rpt)
{
	if (!rpt) {
		return;
	}
	if (rpt->scorecard_name) {
		free(rpt->scorecard_name);
	}
	if (rpt->contributions) {
		for (size_t i = 0; i < rpt->num_contributions; i++) {
			if (rpt->contributions[i].char_name) {
				free(rpt->contributions[i].char_name);
			}
		}
		free(rpt->contributions);
	}
	free(rpt);
}

int
cdsl_schema_verify_scorecard(const cdsl_scorecard_t* sc,
			     const cdsl_schema_t* schema,
			     cdsl_error_list_t* errors)
{
	if (!sc || !schema) {
		return -1;
	}
	int err_count = 0;
	cdsl_scorecard_char_t* ch = sc->characteristics;
	while (ch) {
		bool found = false;
		for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
			if (v->name && strcmp(v->name, ch->name) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			if (errors) {
				char buf[128];
				snprintf(buf,
					 sizeof(buf),
					 "Scorecard characteristic '%s' not found in schema",
					 ch->name);
				cdsl_error_list_add(
				    errors, cdsl_error_create(CDSL_ERR_TYPE, 0, 0, buf, NULL));
			}
			err_count++;
		}
		ch = ch->next;
	}
	return err_count == 0 ? 0 : -1;
}

/* Helper to parse condition expression */
static cdsl_expr_node_t*
parse_case_expr(const char* char_name, const char* cond_str, cdsl_arena_t* target_arena)
{
	if (!char_name || !cond_str) {
		return NULL;
	}
	while (isspace((unsigned char)*cond_str)) {
		cond_str++;
	}
	if (*cond_str == '\0') {
		return NULL;
	}

	char buf[2048];
	if (*cond_str == '[' || strncmp(cond_str, "IN", 2) == 0 ||
	    strncmp(cond_str, "in", 2) == 0) {
		if (*cond_str == '[') {
			snprintf(buf,
				 sizeof(buf),
				 "RULE __sc_temp__ { WHEN %s in %s THEN dummy() }",
				 char_name,
				 cond_str);
		} else {
			snprintf(buf,
				 sizeof(buf),
				 "RULE __sc_temp__ { WHEN %s %s THEN dummy() }",
				 char_name,
				 cond_str);
		}
	} else if (*cond_str == '<' || *cond_str == '>' || *cond_str == '=' || *cond_str == '!') {
		snprintf(buf,
			 sizeof(buf),
			 "RULE __sc_temp__ { WHEN %s %s THEN dummy() }",
			 char_name,
			 cond_str);
	} else {
		snprintf(buf,
			 sizeof(buf),
			 "RULE __sc_temp__ { WHEN %s == %s THEN dummy() }",
			 char_name,
			 cond_str);
	}

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

cdsl_scorecard_t*
cdsl_scorecard_parse_string(const char* dsl, cdsl_error_list_t* errors)
{
	if (!dsl) {
		return NULL;
	}
	const char* p = dsl;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	if (strncmp(p, "SCORECARD", 9) != 0) {
		if (errors) {
			cdsl_error_list_add(
			    errors,
			    cdsl_error_create(CDSL_ERR_SYNTAX, 1, 1, "Expected SCORECARD", NULL));
		}
		return NULL;
	}
	p += 9;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	/* Extract scorecard name */
	char name[128] = {0};
	size_t ni = 0;
	while (*p && !isspace((unsigned char)*p) && ni + 1 < sizeof(name)) {
		name[ni++] = *p++;
	}

	cdsl_scorecard_t* sc = cdsl_scorecard_create(name, 0.0, NULL);
	if (!sc) {
		return NULL;
	}
	sc->arena = cdsl_arena_create(CDSL_ARENA_DEFAULT_BLOCK_SIZE);

	char line[1024] = {0};
	char current_char[128] = {0};

	while (*p) {
		while (*p == '\r' || *p == '\n') {
			p++;
		}
		if (!*p) {
			break;
		}

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

		if (strncmp(cur, "BASELINE:", 9) == 0) {
			cur += 9;
			sc->baseline_score = strtod(cur, NULL);
			continue;
		}

		if (strncmp(cur, "OUTPUT:", 7) == 0) {
			cur += 7;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			char* tend = cur + strlen(cur) - 1;
			while (tend > cur && isspace((unsigned char)*tend)) {
				*tend-- = '\0';
			}
			if (sc->output_var) {
				free(sc->output_var);
			}
			sc->output_var = cdsl_strdup(cur);
			continue;
		}

		if (strncmp(cur, "CHARACTERISTIC", 14) == 0) {
			cur += 14;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			char* colon = strchr(cur, ':');
			if (colon) {
				*colon = '\0';
			}
			char* tend = cur + strlen(cur) - 1;
			while (tend > cur && isspace((unsigned char)*tend)) {
				*tend-- = '\0';
			}

			snprintf(current_char, sizeof(current_char), "%s", cur);
			cdsl_scorecard_add_characteristic(sc, current_char, 0.0);
			continue;
		}

		if (strncmp(cur, "CASE", 4) == 0) {
			cur += 4;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			char* colon = strchr(cur, ':');
			if (!colon || current_char[0] == '\0') {
				continue;
			}
			*colon = '\0';
			char* cond_part = cur;
			char* delta_part = colon + 1;
			while (isspace((unsigned char)*delta_part)) {
				delta_part++;
			}

			double delta = strtod(delta_part, NULL);
			cdsl_expr_node_t* cond_expr =
			    parse_case_expr(current_char, cond_part, sc->arena);
			if (cond_expr) {
				cdsl_scorecard_add_case(sc, current_char, cond_expr, delta);
			}
			continue;
		}

		if (strncmp(cur, "DEFAULT:", 8) == 0) {
			cur += 8;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			double def_delta = strtod(cur, NULL);
			if (current_char[0] != '\0') {
				cdsl_scorecard_char_t* ch = sc->characteristics;
				while (ch) {
					if (strcmp(ch->name, current_char) == 0) {
						ch->default_delta = def_delta;
						break;
					}
					ch = ch->next;
				}
			}
			continue;
		}
	}

	return sc;
}
