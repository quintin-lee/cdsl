/**
 * @file vm_context.c
 * @brief Execution context and VM lifecycle implementation.
 *
 * Implements the runtime context for variable bindings, the Virtual
 * Machine lifecycle (create/destroy), action and function registration,
 * and JSON-based context loading with automatic type inference.
 *
 * The context uses a dual-index structure: a linked list for iteration
 * and a hash map for O(1) variable lookups during expression evaluation.
 *
 * @defgroup context Execution Context & VM
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include "cdsl/util/json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

/**
 * @brief Get a high-resolution timestamp in microseconds.
 *
 * Uses CLOCK_MONOTONIC for elapsed time measurements that are
 * immune to system clock adjustments.
 *
 * @return Current timestamp in microseconds
 */
double
cdsl_get_time_us_internal(void)
{
#ifdef _WIN32
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);
	return (double)count.QuadPart * 1e6 / (double)freq.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((double)ts.tv_sec * 1e6) + ((double)ts.tv_nsec / 1e3);
#endif
}

cdsl_context_entry_t*
cdsl_context_get_entry_internal(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !ctx->map || !name) {
		return NULL;
	}
	return (cdsl_context_entry_t*)cdsl_hashmap_get(ctx->map, name);
}

typedef struct cdsl_context_hook_node {
	cdsl_context_change_hook_fn hook;
	void* user_data;
	struct cdsl_context_hook_node* next;
} cdsl_context_hook_node_t;

int
cdsl_context_add_change_hook(cdsl_context_t* ctx, cdsl_context_change_hook_fn hook, void* user_data)
{
	if (!ctx || !hook) {
		return 0;
	}
	cdsl_context_hook_node_t* node = calloc(1, sizeof(*node));
	if (!node) {
		return 0;
	}
	node->hook = hook;
	node->user_data = user_data;
	node->next = ctx->hooks;
	ctx->hooks = node;
	return 1;
}

int
cdsl_context_remove_change_hook(cdsl_context_t* ctx,
				cdsl_context_change_hook_fn hook,
				void* user_data)
{
	if (!ctx || !hook) {
		return 0;
	}
	cdsl_context_hook_node_t* prev = NULL;
	for (cdsl_context_hook_node_t* curr = ctx->hooks; curr; prev = curr, curr = curr->next) {
		if (curr->hook == hook && curr->user_data == user_data) {
			if (prev) {
				prev->next = curr->next;
			} else {
				ctx->hooks = curr->next;
			}
			free(curr);
			return 1;
		}
	}
	return 0;
}

void
cdsl_context_notify_change(cdsl_context_t* ctx, const char* var_name)
{
	if (!ctx) {
		return;
	}
	for (cdsl_context_hook_node_t* curr = ctx->hooks; curr; curr = curr->next) {
		if (curr->hook) {
			curr->hook(ctx, var_name, curr->user_data);
		}
	}
}

cdsl_context_t*
cdsl_context_create(const cdsl_schema_t* schema)
{
	cdsl_context_t* ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}
	ctx->schema = schema;
	ctx->map = cdsl_hashmap_create(64);
	if (!ctx->map) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void
cdsl_value_free(cdsl_value_t* val)
{
	if (!val) {
		return;
	}
	if (val->type == CDSL_TYPE_STRING && val->data.string_val && val->owned_string) {
		free(val->data.string_val);
		val->data.string_val = NULL;
	} else if (val->type == CDSL_TYPE_ARRAY && val->data.array_val) {
		cdsl_array_t* arr = val->data.array_val;
		for (int i = 0; i < arr->count; i++) {
			cdsl_value_free(&arr->items[i]);
		}
		free(arr->items);
		free(arr);
		val->data.array_val = NULL;
	} else if (val->type == CDSL_TYPE_MAP && val->data.map_val) {
		cdsl_map_free(val->data.map_val);
		val->data.map_val = NULL;
	}
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
		cdsl_value_free(&e->value);
		free(e);
		e = next;
	}
	cdsl_context_notify_change(ctx, NULL);
	cdsl_context_hook_node_t* h = ctx->hooks;
	while (h) {
		cdsl_context_hook_node_t* next = h->next;
		free(h);
		h = next;
	}
	ctx->hooks = NULL;
	free(ctx);
}

/**
 * @brief Check if a context variable is read-only per schema.
 */
static int
context_var_is_readonly(const cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !ctx->schema || !name) {
		return 0;
	}
	cdsl_var_schema_t* vs = cdsl_hashmap_get(ctx->schema->var_map, name);
	return vs && vs->is_readonly;
}

/**
 * @brief Track memory allocation and enforce per-execution VM budget (internal).
 * @return 1 if allocation should proceed, 0 if limit exceeded
 */
int
cdsl_vm_track_alloc(cdsl_vm_t* vm, size_t bytes)
{
	if (!vm) {
		return 1;
	}
	if (vm->memory_limit <= 0) {
		return 1;
	}
	int64_t cur = CDSL_ATOMIC_FETCH_ADD(&vm->alloc_bytes, (int64_t)bytes) + (int64_t)bytes;
	if (cur > vm->memory_limit) {
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RUNTIME,
				       0,
				       0,
				       "Memory quota exceeded",
				       "Increase VM memory limit or reduce runtime allocations");
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return 0;
	}
	return 1;
}

/**
 * @brief Duplicate a string while charging the VM's per-execution memory budget.
 */
char*
cdsl_vm_strdup(cdsl_vm_t* vm, const char* s)
{
	if (!s) {
		return NULL;
	}
#if defined(CDSL_TESTING) || !defined(NDEBUG)
	if (cdsl_test_oom_check_and_decrement()) {
		return NULL;
	}
#endif
	size_t n = strlen(s) + 1;
	if (!cdsl_vm_track_alloc(vm, n)) {
		return NULL;
	}
	char* p = malloc(n);
	if (p) {
		memcpy(p, s, n);
	}
	return p;
}

/**
 * @brief Zero-allocate memory while charging the VM's per-execution memory budget.
 */
void*
cdsl_vm_calloc(cdsl_vm_t* vm, size_t nmemb, size_t size)
{
	if (size != 0 && nmemb > ((size_t)-1) / size) {
		return NULL;
	}
	size_t total = nmemb * size;
	if (!cdsl_vm_track_alloc(vm, total)) {
		return NULL;
	}
	return calloc(nmemb, size);
}

/**
 * @brief Track memory deallocation (internal).
 */
void
cdsl_vm_track_free(cdsl_vm_t* vm, size_t bytes)
{
	if (!vm || !bytes) {
		return;
	}
	CDSL_ATOMIC_FETCH_SUB(&vm->alloc_bytes, (int64_t)bytes);
}

/**
 * @brief Check if execution has been aborted (timeout or OOM).
 */
int
cdsl_vm_check_abort(cdsl_vm_t* vm, double start_time_us)
{
	if (!vm) {
		return 0;
	}
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		return 1;
	}
	if (vm->timeout_us > 0) {
		double elapsed = cdsl_get_time_us_internal() - start_time_us;
		if (elapsed > (double)vm->timeout_us) {
			if (!vm->last_error) {
				cdsl_vm_set_last_error(
				    vm, CDSL_ERR_RUNTIME, 0, 0, "Execution timeout exceeded", NULL);
			}
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
			return 1;
		}
	}
	return 0;
}

static unsigned int
map_hash(const char* str)
{
	unsigned int hash = 5381;
	int c;
	while ((c = (unsigned char)*str++)) {
		hash = ((hash << 5) + hash) + (unsigned int)c;
	}
	return hash;
}

cdsl_map_t*
cdsl_map_create(int bucket_count)
{
	if (bucket_count <= 0) {
		bucket_count = 16;
	}
	cdsl_map_t* m = calloc(1, sizeof(*m));
	if (!m) {
		return NULL;
	}
	m->buckets = calloc((size_t)bucket_count, sizeof(cdsl_map_entry_t*));
	if (!m->buckets) {
		free(m);
		return NULL;
	}
	m->bucket_count = bucket_count;
	m->count = 0;
	return m;
}

void
cdsl_map_free(cdsl_map_t* map)
{
	if (!map) {
		return;
	}
	for (int i = 0; i < map->bucket_count; i++) {
		cdsl_map_entry_t* e = map->buckets[i];
		while (e) {
			cdsl_map_entry_t* next = e->next;
			free(e->key);
			cdsl_value_free(&e->value);
			free(e);
			e = next;
		}
	}
	free(map->buckets);
	free(map);
}

const cdsl_value_t*
cdsl_map_get(const cdsl_map_t* map, const char* key)
{
	if (!map || !map->buckets || !key) {
		return NULL;
	}
	unsigned int h = map_hash(key) % (unsigned int)map->bucket_count;
	for (cdsl_map_entry_t* e = map->buckets[h]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			return &e->value;
		}
	}
	return NULL;
}

int
cdsl_map_has(const cdsl_map_t* map, const char* key)
{
	return cdsl_map_get(map, key) != NULL;
}

cdsl_value_t
cdsl_value_copy(const cdsl_value_t* src)
{
	cdsl_value_t v = {.type = CDSL_TYPE_VOID};
	if (!src) {
		return v;
	}
	v.type = src->type;
	v.owned_string = 0;
	switch (src->type) {
	case CDSL_TYPE_INT:
		v.data.int_val = src->data.int_val;
		break;
	case CDSL_TYPE_FLOAT:
		v.data.float_val = src->data.float_val;
		break;
	case CDSL_TYPE_BOOL:
		v.data.bool_val = src->data.bool_val;
		break;
	case CDSL_TYPE_LONG:
		v.data.long_val = src->data.long_val;
		break;
	case CDSL_TYPE_DATE:
		v.data.date_val = src->data.date_val;
		break;
	case CDSL_TYPE_STRING:
		if (src->data.string_val) {
			v.data.string_val = cdsl_strdup(src->data.string_val);
			v.owned_string = 1;
		} else {
			v.data.string_val = NULL;
		}
		break;
	case CDSL_TYPE_ARRAY:
		if (src->data.array_val) {
			cdsl_array_t* src_arr = src->data.array_val;
			cdsl_array_t* arr = calloc(1, sizeof(*arr));
			if (arr) {
				arr->count = src_arr->count;
				arr->capacity = src_arr->count;
				if (arr->count > 0) {
					arr->items =
					    calloc((size_t)arr->count, sizeof(cdsl_value_t));
					if (arr->items) {
						for (int i = 0; i < arr->count; i++) {
							arr->items[i] =
							    cdsl_value_copy(&src_arr->items[i]);
						}
					} else {
						arr->count = 0;
					}
				}
			}
			v.data.array_val = arr;
		} else {
			v.data.array_val = NULL;
		}
		break;
	case CDSL_TYPE_MAP:
		v.data.map_val = cdsl_map_copy(src->data.map_val);
		break;
	case CDSL_TYPE_VOID:
	default:
		break;
	}
	return v;
}

int
cdsl_map_set(cdsl_map_t* map, const char* key, const cdsl_value_t* val)
{
	if (!map || !map->buckets || !key || !val) {
		return 0;
	}
	unsigned int h = map_hash(key) % (unsigned int)map->bucket_count;
	for (cdsl_map_entry_t* e = map->buckets[h]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			cdsl_value_free(&e->value);
			e->value = cdsl_value_copy(val);
			return 1;
		}
	}
	cdsl_map_entry_t* entry = calloc(1, sizeof(*entry));
	if (!entry) {
		return 0;
	}
	entry->key = cdsl_strdup(key);
	if (!entry->key) {
		free(entry);
		return 0;
	}
	entry->value = cdsl_value_copy(val);
	entry->next = map->buckets[h];
	map->buckets[h] = entry;
	map->count++;
	return 1;
}

cdsl_map_t*
cdsl_map_copy(const cdsl_map_t* map)
{
	if (!map) {
		return NULL;
	}
	cdsl_map_t* copy = cdsl_map_create(map->bucket_count);
	if (!copy) {
		return NULL;
	}
	for (int i = 0; i < map->bucket_count; i++) {
		for (cdsl_map_entry_t* e = map->buckets[i]; e; e = e->next) {
			if (!cdsl_map_set(copy, e->key, &e->value)) {
				cdsl_map_free(copy);
				return NULL;
			}
		}
	}
	return copy;
}

const cdsl_value_t*
cdsl_context_lookup_value(const cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !name) {
		return NULL;
	}
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		return &e->value;
	}
	const char* dot = strchr(name, '.');
	if (!dot) {
		return NULL;
	}
	size_t root_len = (size_t)(dot - name);
	char root_name[128];
	if (root_len >= sizeof(root_name)) {
		return NULL;
	}
	memcpy(root_name, name, root_len);
	root_name[root_len] = '\0';

	cdsl_context_entry_t* root_entry =
	    cdsl_context_get_entry_internal((cdsl_context_t*)ctx, root_name);
	if (!root_entry || root_entry->value.type != CDSL_TYPE_MAP ||
	    !root_entry->value.data.map_val) {
		return NULL;
	}

	const cdsl_map_t* cur_map = root_entry->value.data.map_val;
	const char* rem = dot + 1;
	while (rem && *rem) {
		const char* next_dot = strchr(rem, '.');
		if (!next_dot) {
			return cdsl_map_get(cur_map, rem);
		}
		size_t seg_len = (size_t)(next_dot - rem);
		char seg[128];
		if (seg_len >= sizeof(seg)) {
			return NULL;
		}
		memcpy(seg, rem, seg_len);
		seg[seg_len] = '\0';
		const cdsl_value_t* sub_val = cdsl_map_get(cur_map, seg);
		if (!sub_val || sub_val->type != CDSL_TYPE_MAP || !sub_val->data.map_val) {
			return NULL;
		}
		cur_map = sub_val->data.map_val;
		rem = next_dot + 1;
	}
	return NULL;
}

void
cdsl_context_set_map(cdsl_context_t* ctx, const char* name, cdsl_map_t* val)
{
	if (!ctx || !name) {
		return;
	}
	if (context_var_is_readonly(ctx, name)) {
		return;
	}
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_MAP;
		e->value.data.map_val = val;
		e->value.owned_string = 0;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		e = calloc(1, sizeof(*e));
		if (!e) {
			free(new_name);
			return;
		}
		e->name = new_name;
		e->value.type = CDSL_TYPE_MAP;
		e->value.data.map_val = val;
		e->value.owned_string = 0;
		e->next = ctx->entries;
		ctx->entries = e;
		cdsl_hashmap_put(ctx->map, name, e);
	}
	cdsl_context_notify_change(ctx, name);
}

cdsl_map_t*
cdsl_context_get_map(const cdsl_context_t* ctx, const char* name)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v && v->type == CDSL_TYPE_MAP) {
		return v->data.map_val;
	}
	return NULL;
}

int
cdsl_context_push_var(cdsl_context_t* ctx, const char* name, cdsl_value_t val)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_context_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return 0;
	}
	e->name = cdsl_strdup(name);
	if (!e->name) {
		free(e);
		return 0;
	}
	e->value = val;
	e->next = ctx->entries;
	ctx->entries = e;
	cdsl_hashmap_put(ctx->map, name, e);
	cdsl_context_notify_change(ctx, name);
	return 1;
}

void
cdsl_context_pop_var(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !name) {
		return;
	}
	cdsl_context_entry_t* prev = NULL;
	cdsl_context_entry_t* target = NULL;
	for (cdsl_context_entry_t* e = ctx->entries; e; prev = e, e = e->next) {
		if (strcmp(e->name, name) == 0) {
			target = e;
			if (prev) {
				prev->next = e->next;
			} else {
				ctx->entries = e->next;
			}
			break;
		}
	}
	if (target) {
		cdsl_context_entry_t* shadowed = NULL;
		for (cdsl_context_entry_t* e = ctx->entries; e; e = e->next) {
			if (strcmp(e->name, name) == 0) {
				shadowed = e;
				break;
			}
		}
		if (shadowed) {
			cdsl_hashmap_put(ctx->map, name, shadowed);
		} else {
			cdsl_hashmap_remove(ctx->map, name, NULL);
		}
		free(target->name);
		cdsl_value_free(&target->value);
		free(target);
		cdsl_context_notify_change(ctx, name);
	}
}

void
cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val)
{
	if (!ctx || !name) {
		return;
	}
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_INT;
		e->value.data.int_val = val;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_INT;
		ne->value.data.int_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

void
cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_FLOAT;
		e->value.data.float_val = val;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_FLOAT;
		ne->value.data.float_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

void
cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_BOOL;
		e->value.data.bool_val = val;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_BOOL;
		ne->value.data.bool_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

void
cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	char* new_str = val ? cdsl_strdup(val) : NULL;
	if (val && !new_str) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_STRING;
		e->value.data.string_val = new_str;
		e->value.owned_string = 1;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			free(new_str);
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			free(new_str);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_STRING;
		ne->value.data.string_val = new_str;
		ne->value.owned_string = 1;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

void
cdsl_context_set_date(cdsl_context_t* ctx, const char* name, time_t val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_DATE;
		e->value.data.date_val = val;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_DATE;
		ne->value.data.date_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

void
cdsl_context_set_long(cdsl_context_t* ctx, const char* name, int64_t val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_LONG;
		e->value.data.long_val = val;
	} else {
		char* new_name = cdsl_strdup(name);
		if (!new_name) {
			return;
		}
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			free(new_name);
			return;
		}
		ne->name = new_name;
		ne->value.type = CDSL_TYPE_LONG;
		ne->value.data.long_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
	cdsl_context_notify_change(ctx, name);
}

int
cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v) {
		if (v->type == CDSL_TYPE_INT) {
			return v->data.int_val;
		}
		if (v->type == CDSL_TYPE_LONG) {
			return (int)v->data.long_val;
		}
		if (v->type == CDSL_TYPE_FLOAT) {
			return (int)v->data.float_val;
		}
		if (v->type == CDSL_TYPE_BOOL) {
			return v->data.bool_val;
		}
	}
	return default_val;
}

double
cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v) {
		if (v->type == CDSL_TYPE_FLOAT) {
			return v->data.float_val;
		}
		if (v->type == CDSL_TYPE_INT) {
			return (double)v->data.int_val;
		}
		if (v->type == CDSL_TYPE_LONG) {
			return (double)v->data.long_val;
		}
		if (v->type == CDSL_TYPE_BOOL) {
			return (double)v->data.bool_val;
		}
	}
	return default_val;
}

int
cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v) {
		if (v->type == CDSL_TYPE_BOOL) {
			return v->data.bool_val;
		}
		if (v->type == CDSL_TYPE_INT) {
			return v->data.int_val != 0;
		}
		if (v->type == CDSL_TYPE_LONG) {
			return v->data.long_val != 0;
		}
		if (v->type == CDSL_TYPE_FLOAT) {
			return v->data.float_val != 0.0;
		}
	}
	return default_val;
}

const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v && v->type == CDSL_TYPE_STRING) {
		return v->data.string_val;
	}
	return default_val;
}

time_t
cdsl_context_get_date(const cdsl_context_t* ctx, const char* name, time_t default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v && v->type == CDSL_TYPE_DATE) {
		return v->data.date_val;
	}
	return default_val;
}

int64_t
cdsl_context_get_long(const cdsl_context_t* ctx, const char* name, int64_t default_val)
{
	const cdsl_value_t* v = cdsl_context_lookup_value(ctx, name);
	if (v) {
		if (v->type == CDSL_TYPE_LONG) {
			return v->data.long_val;
		}
		if (v->type == CDSL_TYPE_INT) {
			return (int64_t)v->data.int_val;
		}
		if (v->type == CDSL_TYPE_FLOAT) {
			return (int64_t)v->data.float_val;
		}
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
			cdsl_value_free(&e->value);
			free(e);
			cdsl_context_notify_change(ctx, name);
			return 1;
		}
	}
	return 0;
}

static void
load_json_recursive(cdsl_context_t* ctx, cdsl_json_value_t* obj, const char* prefix)
{
	if (!obj || obj->type != CDSL_JSON_OBJECT) {
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
			key = cdsl_strdup(child->key);
			if (!key) {
				return;
			}
		}

		if (child->type == CDSL_JSON_OBJECT) {
			load_json_recursive(ctx, child, key);
		} else if (child->type == CDSL_JSON_NUMBER) {
			if (child->is_int) {
				int64_t iv = child->value.i64;
				if (iv > INT_MAX || iv < INT_MIN) {
					cdsl_context_set_long(ctx, key, iv);
				} else {
					cdsl_context_set_int(ctx, key, (int)iv);
				}
			} else {
				cdsl_context_set_float(ctx, key, child->value.f64);
			}
		} else if (child->type == CDSL_JSON_BOOL) {
			cdsl_context_set_bool(ctx, key, child->value.bool_val);
		} else if (child->type == CDSL_JSON_STRING) {
			cdsl_context_set_string(ctx, key, child->value.string_val);
		} else if (child->type == CDSL_JSON_ARRAY) {
			cdsl_array_t* arr = calloc(1, sizeof(cdsl_array_t));
			if (arr) {
				arr->count = child->value.array.count;
				arr->capacity = arr->count;
				if (arr->count > 0) {
					arr->items =
					    calloc((size_t)arr->count, sizeof(cdsl_value_t));
				}
				int idx = 0;
				for (cdsl_json_value_t* elem = child->value.array.items;
				     elem && arr->items && idx < arr->count;
				     elem = elem->next, idx++) {
					if (elem->type == CDSL_JSON_NUMBER) {
						if (elem->is_int) {
							int64_t iv = elem->value.i64;
							if (iv > INT_MAX || iv < INT_MIN) {
								arr->items[idx].type = CDSL_TYPE_LONG;
								arr->items[idx].data.long_val = iv;
							} else {
								arr->items[idx].type = CDSL_TYPE_INT;
								arr->items[idx].data.int_val = (int)iv;
							}
						} else {
							arr->items[idx].type = CDSL_TYPE_FLOAT;
							arr->items[idx].data.float_val = elem->value.f64;
						}
					} else if (elem->type == CDSL_JSON_BOOL) {
						arr->items[idx].type = CDSL_TYPE_BOOL;
						arr->items[idx].data.bool_val =
						    elem->value.bool_val;
					} else if (elem->type == CDSL_JSON_STRING &&
						   elem->value.string_val) {
						arr->items[idx].type = CDSL_TYPE_STRING;
						arr->items[idx].data.string_val =
						    cdsl_strdup(elem->value.string_val);
						arr->items[idx].owned_string = 1;
						if (!arr->items[idx].data.string_val) {
							cdsl_value_t tv = {.type = CDSL_TYPE_ARRAY,
									   .data.array_val = arr};
							cdsl_value_free(&tv);
							free(key);
							return;
						}
					}
				}
				cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, key);
				if (e && !context_var_is_readonly(ctx, key)) {
					cdsl_value_free(&e->value);
					e->value.type = CDSL_TYPE_ARRAY;
					e->value.data.array_val = arr;
				} else if (!e) {
					cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
					if (ne) {
						ne->name = cdsl_strdup(key);
						if (ne->name) {
							ne->value.type = CDSL_TYPE_ARRAY;
							ne->value.data.array_val = arr;
							ne->next = ctx->entries;
							ctx->entries = ne;
							cdsl_hashmap_put(ctx->map, key, ne);
						} else {
							cdsl_value_t tv = {.type = CDSL_TYPE_ARRAY,
									   .data.array_val = arr};
							cdsl_value_free(&tv);
							free(ne);
						}
					} else {
						cdsl_value_t tv = {.type = CDSL_TYPE_ARRAY,
								   .data.array_val = arr};
						cdsl_value_free(&tv);
					}
				} else {
					cdsl_value_t tv = {.type = CDSL_TYPE_ARRAY,
							   .data.array_val = arr};
					cdsl_value_free(&tv);
				}
			}
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

/** @} */
