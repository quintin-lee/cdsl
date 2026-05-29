#include "execution.h"
#include "execution_internal.h"
#include "cdsl_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

double
cdsl_get_time_us_internal(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

cdsl_context_entry_t*
cdsl_context_get_entry_internal(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !ctx->map || !name) {
		return NULL;
	}
	return (cdsl_context_entry_t*)cdsl_hashmap_get(ctx->map, name);
}

cdsl_context_t*
cdsl_context_create(const cdsl_schema_t* schema)
{
	cdsl_context_t* ctx = calloc(1, sizeof(*ctx));
	ctx->schema = schema;
	ctx->map = cdsl_hashmap_create(64);
	return ctx;
}

void
cdsl_context_free(cdsl_context_t* ctx)
{
	if (!ctx) {
		return;
	}
	cdsl_hashmap_free(ctx->map, NULL);
	cdsl_context_entry_t* e = ctx->entries;
	while (e) {
		cdsl_context_entry_t* next = e->next;
		free(e->name);
		if (e->value.type == CDSL_TYPE_STRING) {
			free(e->value.data.string_val);
		}
		free(e);
		e = next;
	}
	free(ctx);
}

void
cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_STRING) {
			free(e->value.data.string_val);
		}
		e->value.type = CDSL_TYPE_INT;
		e->value.data.int_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		ne->name = strdup(name);
		ne->value.type = CDSL_TYPE_INT;
		ne->value.data.int_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_STRING) {
			free(e->value.data.string_val);
		}
		e->value.type = CDSL_TYPE_FLOAT;
		e->value.data.float_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		ne->name = strdup(name);
		ne->value.type = CDSL_TYPE_FLOAT;
		ne->value.data.float_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_STRING) {
			free(e->value.data.string_val);
		}
		e->value.type = CDSL_TYPE_BOOL;
		e->value.data.bool_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		ne->name = strdup(name);
		ne->value.type = CDSL_TYPE_BOOL;
		ne->value.data.bool_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_STRING) {
			free(e->value.data.string_val);
		}
		e->value.type = CDSL_TYPE_STRING;
		e->value.data.string_val = strdup(val);
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		ne->name = strdup(name);
		ne->value.type = CDSL_TYPE_STRING;
		ne->value.data.string_val = strdup(val);
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

int
cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_INT) {
			return e->value.data.int_val;
		}
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return (int)e->value.data.float_val;
		}
		if (e->value.type == CDSL_TYPE_BOOL) {
			return e->value.data.bool_val;
		}
	}
	return default_val;
}

double
cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return e->value.data.float_val;
		}
		if (e->value.type == CDSL_TYPE_INT) {
			return (double)e->value.data.int_val;
		}
		if (e->value.type == CDSL_TYPE_BOOL) {
			return (double)e->value.data.bool_val;
		}
	}
	return default_val;
}

int
cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_BOOL) {
			return e->value.data.bool_val;
		}
		if (e->value.type == CDSL_TYPE_INT) {
			return e->value.data.int_val != 0;
		}
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return e->value.data.float_val != 0.0;
		}
	}
	return default_val;
}

const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e && e->value.type == CDSL_TYPE_STRING) {
		return e->value.data.string_val;
	}
	return default_val;
}

int
cdsl_context_remove(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_hashmap_remove(ctx->map, name, NULL);
	cdsl_context_entry_t* prev = NULL;
	for (cdsl_context_entry_t* e = ctx->entries; e; prev = e, e = e->next) {
		if (strcmp(e->name, name) == 0) {
			if (prev) {
				prev->next = e->next;
			} else {
				ctx->entries = e->next;
			}
			free(e->name);
			if (e->value.type == CDSL_TYPE_STRING) {
				free(e->value.data.string_val);
			}
			free(e);
			return 1;
		}
	}
	return 0;
}

static void
load_json_recursive(cdsl_context_t* ctx, cdsl_json_value_t* obj, const char* prefix)
{
	if (!obj || obj->type != JSON_OBJECT) {
		return;
	}
	cdsl_json_value_t* child = obj->value.object.items;
	while (child) {
		char* key;
		if (prefix[0]) {
			size_t klen = strlen(prefix) + strlen(child->key) + 2;
			key = malloc(klen);
			if (!key) {
				return;
			}
			snprintf(key, klen, "%s.%s", prefix, child->key);
		} else {
			key = strdup(child->key);
			if (!key) {
				return;
			}
		}

		if (child->type == JSON_OBJECT) {
			load_json_recursive(ctx, child, key);
		} else if (child->type == JSON_NUMBER) {
			double v = child->value.number_val;
			if (v != (double)(int)v) {
				cdsl_context_set_float(ctx, key, v);
			} else {
				cdsl_context_set_int(ctx, key, (int)v);
			}
		} else if (child->type == JSON_BOOL) {
			cdsl_context_set_bool(ctx, key, child->value.bool_val);
		} else if (child->type == JSON_STRING) {
			cdsl_context_set_string(ctx, key, child->value.string_val);
		}
		free(key);
		child = child->next;
	}
}

int
cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str)
{
	cdsl_json_value_t* root = cdsl_json_parse(json_str);
	if (!root) {
		return 0;
	}
	load_json_recursive(ctx, root, "");
	cdsl_json_free(root);
	return 1;
}

cdsl_vm_t*
cdsl_vm_create(const cdsl_schema_t* schema)
{
	cdsl_vm_t* vm = calloc(1, sizeof(*vm));
	vm->schema = schema;
	vm->debug_enabled = 0;
	vm->max_expr_depth = CDSL_MAX_EXPR_DEPTH;
	return vm;
}

void
cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled)
{
	if (vm) {
		vm->debug_enabled = enabled;
	}
}

int
cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm)
{
	return vm ? vm->max_expr_depth : CDSL_MAX_EXPR_DEPTH;
}

void
cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth)
{
	if (vm && depth > 0) {
		vm->max_expr_depth = depth;
	}
}

cdsl_stats_t*
cdsl_vm_get_stats(const cdsl_vm_t* vm)
{
	if (!vm) {
		return NULL;
	}
	cdsl_stats_t* s = malloc(sizeof(cdsl_stats_t));
	*s = vm->stats;
	if (s->total_executions > 0) {
		s->avg_time_us = s->total_time_us / s->total_executions;
	}
	return s;
}

void
cdsl_vm_reset_stats(cdsl_vm_t* vm)
{
	if (vm) {
		memset(&vm->stats, 0, sizeof(cdsl_stats_t));
	}
}

void
cdsl_vm_free(cdsl_vm_t* vm)
{
	if (!vm) {
		return;
	}
	cdsl_action_cb_entry_t* cb = vm->callbacks;
	while (cb) {
		cdsl_action_cb_entry_t* next = cb->next;
		free(cb->action_name);
		free(cb);
		cb = next;
	}
	cdsl_func_entry_t* fn = vm->functions;
	while (fn) {
		cdsl_func_entry_t* next = fn->next;
		free(fn->func_name);
		free(fn);
		fn = next;
	}
	free(vm);
}

void
cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb)
{
	cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
	e->action_name = strdup(action_name);
	e->cb = cb;
	e->next = vm->callbacks;
	vm->callbacks = e;
}

void
cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb)
{
	if (!vm || !func_name || !cb) {
		return;
	}
	cdsl_func_entry_t* e = calloc(1, sizeof(*e));
	e->func_name = strdup(func_name);
	e->cb = cb;
	e->next = vm->functions;
	vm->functions = e;
}
