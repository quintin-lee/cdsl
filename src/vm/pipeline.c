/**
 * @file src/vm/pipeline.c
 * @brief DAG Rule Pipeline Orchestration Engine.
 */

#include "cdsl/pipeline.h"
#include "cdsl/context.h"
#include "cdsl/report.h"
#include "cdsl/util/portability.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cdsl_pipeline_t*
cdsl_pipeline_create(const char* name)
{
	if (!name) {
		return NULL;
	}
	cdsl_pipeline_t* p = calloc(1, sizeof(*p));
	if (!p) {
		return NULL;
	}
	p->name = cdsl_strdup(name);
	if (!p->name) {
		free(p);
		return NULL;
	}
	return p;
}

cdsl_pipeline_stage_t*
cdsl_pipeline_create_stage(const char* name, cdsl_on_fail_t on_fail)
{
	if (!name) {
		return NULL;
	}
	cdsl_pipeline_stage_t* s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->name = cdsl_strdup(name);
	if (!s->name) {
		free(s);
		return NULL;
	}
	s->on_fail = on_fail;
	return s;
}

int
cdsl_pipeline_stage_add_rule(cdsl_pipeline_stage_t* stage, cdsl_rule_t* rule)
{
	if (!stage || !rule) {
		return -1;
	}
	cdsl_stage_item_t* item = calloc(1, sizeof(*item));
	if (!item) {
		return -1;
	}
	item->kind = CDSL_STAGE_ITEM_RULE;
	item->name = cdsl_strdup(rule->name ? rule->name : "rule");
	item->rule = rule;

	if (!stage->items) {
		stage->items = item;
	} else {
		cdsl_stage_item_t* curr = stage->items;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = item;
	}
	stage->num_items++;
	return 0;
}

int
cdsl_pipeline_stage_add_dtable(cdsl_pipeline_stage_t* stage, cdsl_dtable_t* dt)
{
	if (!stage || !dt) {
		return -1;
	}
	cdsl_stage_item_t* item = calloc(1, sizeof(*item));
	if (!item) {
		return -1;
	}
	item->kind = CDSL_STAGE_ITEM_DTABLE;
	item->name = cdsl_strdup(dt->name ? dt->name : "dtable");
	item->dtable = dt;

	if (!stage->items) {
		stage->items = item;
	} else {
		cdsl_stage_item_t* curr = stage->items;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = item;
	}
	stage->num_items++;
	return 0;
}

int
cdsl_pipeline_stage_add_scorecard(cdsl_pipeline_stage_t* stage, cdsl_scorecard_t* sc)
{
	if (!stage || !sc) {
		return -1;
	}
	cdsl_stage_item_t* item = calloc(1, sizeof(*item));
	if (!item) {
		return -1;
	}
	item->kind = CDSL_STAGE_ITEM_SCORECARD;
	item->name = cdsl_strdup(sc->name ? sc->name : "scorecard");
	item->scorecard = sc;

	if (!stage->items) {
		stage->items = item;
	} else {
		cdsl_stage_item_t* curr = stage->items;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = item;
	}
	stage->num_items++;
	return 0;
}

int
cdsl_pipeline_stage_add_dependency(cdsl_pipeline_stage_t* stage, const char* dep_stage_name)
{
	if (!stage || !dep_stage_name) {
		return -1;
	}
	size_t new_cap = stage->num_dependencies + 1;
	char** new_deps = realloc(stage->dependencies, new_cap * sizeof(char*));
	if (!new_deps) {
		return -1;
	}
	stage->dependencies = new_deps;
	stage->dependencies[stage->num_dependencies] = cdsl_strdup(dep_stage_name);
	if (!stage->dependencies[stage->num_dependencies]) {
		return -1;
	}
	stage->num_dependencies++;
	return 0;
}

int
cdsl_pipeline_add_stage(cdsl_pipeline_t* p, cdsl_pipeline_stage_t* stage)
{
	if (!p || !stage) {
		return -1;
	}
	if (!p->stages) {
		p->stages = stage;
	} else {
		cdsl_pipeline_stage_t* curr = p->stages;
		while (curr->next) {
			curr = curr->next;
		}
		curr->next = stage;
	}
	p->num_stages++;
	return 0;
}

void
cdsl_pipeline_free(cdsl_pipeline_t* p)
{
	if (!p) {
		return;
	}
	if (p->name) {
		free(p->name);
	}
	cdsl_pipeline_stage_t* s = p->stages;
	while (s) {
		cdsl_pipeline_stage_t* next_s = s->next;
		if (s->name) {
			free(s->name);
		}
		if (s->dependencies) {
			for (size_t i = 0; i < s->num_dependencies; i++) {
				if (s->dependencies[i]) {
					free(s->dependencies[i]);
				}
			}
			free(s->dependencies);
		}
		cdsl_stage_item_t* item = s->items;
		while (item) {
			cdsl_stage_item_t* next_item = item->next;
			if (item->name) {
				free(item->name);
			}
			if (item->rule) {
				cdsl_free_rule(item->rule);
			}
			if (item->dtable) {
				cdsl_dtable_free(item->dtable);
			}
			if (item->scorecard) {
				cdsl_scorecard_free(item->scorecard);
			}
			free(item);
			item = next_item;
		}
		free(s);
		s = next_s;
	}
	free(p);
}

cdsl_pipeline_report_t*
cdsl_pipeline_execute(cdsl_pipeline_t* p, cdsl_vm_t* vm, cdsl_context_t* ctx)
{
	if (!p || !ctx) {
		return NULL;
	}
	double start_us = cdsl_get_time_us_internal();

	cdsl_pipeline_report_t* rpt = calloc(1, sizeof(*rpt));
	if (!rpt) {
		return NULL;
	}
	rpt->pipeline_name = cdsl_strdup(p->name);
	rpt->status = CDSL_PIPELINE_SUCCESS;
	rpt->num_stages = p->num_stages;
	if (p->num_stages > 0) {
		rpt->stage_reports = calloc(p->num_stages, sizeof(cdsl_stage_report_t));
		if (!rpt->stage_reports) {
			cdsl_pipeline_report_free(rpt);
			return NULL;
		}
	}

	size_t si = 0;
	cdsl_pipeline_stage_t* s = p->stages;
	while (s) {
		double stage_start = cdsl_get_time_us_internal();
		rpt->stage_reports[si].stage_name = cdsl_strdup(s->name);
		rpt->stage_reports[si].passed = true;

		/* Check dependencies */
		bool deps_satisfied = true;
		for (size_t d = 0; d < s->num_dependencies; d++) {
			const char* dep = s->dependencies[d];
			bool found_dep = false;
			for (size_t prev = 0; prev < si; prev++) {
				if (strcmp(rpt->stage_reports[prev].stage_name, dep) == 0) {
					found_dep = true;
					if (!rpt->stage_reports[prev].passed) {
						deps_satisfied = false;
					}
					break;
				}
			}
			if (!found_dep || !deps_satisfied) {
				deps_satisfied = false;
				break;
			}
		}

		if (!deps_satisfied) {
			rpt->stage_reports[si].passed = false;
			rpt->stage_reports[si].duration_us =
			    cdsl_get_time_us_internal() - stage_start;
			if (s->on_fail == CDSL_ON_FAIL_ABORT) {
				rpt->status = CDSL_PIPELINE_ABORTED;
				si++;
				break;
			} else {
				rpt->status = CDSL_PIPELINE_FAILED;
			}
			si++;
			s = s->next;
			continue;
		}

		/* Execute items in stage */
		cdsl_stage_item_t* item = s->items;
		while (item) {
			rpt->stage_reports[si].num_items_executed++;
			if (item->kind == CDSL_STAGE_ITEM_RULE && item->rule) {
				cdsl_rule_report_t* rule_rpt = cdsl_vm_execute(vm, item->rule, ctx);
				if (rule_rpt) {
					if (rule_rpt->status != CDSL_STATUS_PASSED) {
						rpt->stage_reports[si].passed = false;
					}
					cdsl_report_free(rule_rpt);
				} else {
					rpt->stage_reports[si].passed = false;
				}
			} else if (item->kind == CDSL_STAGE_ITEM_DTABLE && item->dtable) {
				cdsl_dtable_report_t* dt_rpt = cdsl_dtable_eval(item->dtable, ctx);
				if (dt_rpt) {
					if (!dt_rpt->matched || dt_rpt->error_message != NULL) {
						rpt->stage_reports[si].passed = false;
					}
					cdsl_dtable_report_free(dt_rpt);
				} else {
					rpt->stage_reports[si].passed = false;
				}
			} else if (item->kind == CDSL_STAGE_ITEM_SCORECARD && item->scorecard) {
				cdsl_scorecard_report_t* sc_rpt =
				    cdsl_scorecard_eval(item->scorecard, ctx);
				if (sc_rpt) {
					cdsl_scorecard_report_free(sc_rpt);
				} else {
					rpt->stage_reports[si].passed = false;
				}
			}

			if (!rpt->stage_reports[si].passed && s->on_fail == CDSL_ON_FAIL_ABORT) {
				break;
			}
			item = item->next;
		}

		rpt->stage_reports[si].duration_us = cdsl_get_time_us_internal() - stage_start;

		if (!rpt->stage_reports[si].passed) {
			if (s->on_fail == CDSL_ON_FAIL_ABORT) {
				rpt->status = CDSL_PIPELINE_ABORTED;
				si++;
				break;
			} else {
				rpt->status = CDSL_PIPELINE_FAILED;
			}
		}

		si++;
		s = s->next;
	}

	rpt->num_stages = si;
	rpt->total_duration_us = cdsl_get_time_us_internal() - start_us;
	return rpt;
}

void
cdsl_pipeline_report_free(cdsl_pipeline_report_t* rpt)
{
	if (!rpt) {
		return;
	}
	if (rpt->pipeline_name) {
		free(rpt->pipeline_name);
	}
	if (rpt->stage_reports) {
		for (size_t i = 0; i < rpt->num_stages; i++) {
			if (rpt->stage_reports[i].stage_name) {
				free(rpt->stage_reports[i].stage_name);
			}
		}
		free(rpt->stage_reports);
	}
	free(rpt);
}

cdsl_pipeline_t*
cdsl_pipeline_parse_string(const char* dsl, cdsl_error_list_t* errors)
{
	if (!dsl) {
		return NULL;
	}
	const char* p = dsl;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	if (strncmp(p, "PIPELINE", 8) != 0) {
		if (errors) {
			cdsl_error_list_add(
			    errors,
			    cdsl_error_create(CDSL_ERR_SYNTAX, 1, 1, "Expected PIPELINE", NULL));
		}
		return NULL;
	}
	p += 8;
	while (isspace((unsigned char)*p)) {
		p++;
	}

	/* Extract name */
	char name[128] = {0};
	size_t ni = 0;
	while (*p && !isspace((unsigned char)*p) && ni + 1 < sizeof(name)) {
		name[ni++] = *p++;
	}

	cdsl_pipeline_t* pipe = cdsl_pipeline_create(name);
	if (!pipe) {
		return NULL;
	}

	char line[1024] = {0};
	cdsl_pipeline_stage_t* current_stage = NULL;

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

		if (strncmp(cur, "STAGE", 5) == 0) {
			cur += 5;
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

			current_stage = cdsl_pipeline_create_stage(cur, CDSL_ON_FAIL_ABORT);
			if (current_stage) {
				cdsl_pipeline_add_stage(pipe, current_stage);
			}
			continue;
		}

		if (!current_stage) {
			continue;
		}

		if (strncmp(cur, "ON_FAIL:", 8) == 0) {
			cur += 8;
			while (isspace((unsigned char)*cur)) {
				cur++;
			}
			if (strncmp(cur, "CONTINUE", 8) == 0) {
				current_stage->on_fail = CDSL_ON_FAIL_CONTINUE;
			} else {
				current_stage->on_fail = CDSL_ON_FAIL_ABORT;
			}
			continue;
		}

		if (strncmp(cur, "DEPENDS_ON", 10) == 0) {
			cur += 10;
			while (*cur && *cur != '(' && *cur != ':') {
				cur++;
			}
			if (*cur == '(' || *cur == ':') {
				cur++;
			}
			char* end_paren = strrchr(cur, ')');
			if (end_paren) {
				*end_paren = '\0';
			}

			char* token = strtok(cur, ",");
			while (token) {
				while (isspace((unsigned char)*token)) {
					token++;
				}
				char* tend = token + strlen(token) - 1;
				while (tend > token && isspace((unsigned char)*tend)) {
					*tend-- = '\0';
				}
				if (*token) {
					cdsl_pipeline_stage_add_dependency(current_stage, token);
				}
				token = strtok(NULL, ",");
			}
			continue;
		}
	}

	return pipe;
}
