/**
 * @file src/vm/instructions.inc
 * @brief Shared opcode instruction execution bodies for switch and threaded dispatchers.
 *
 * This file is included by src/vm/bytecode.c (switch-based dispatch) and
 * src/vm/threaded.c (direct-threaded computed-goto dispatch).
 *
 * Caller must define:
 *   OP_HANDLER(op) - expanded to "case op" or "DO_##op"
 *   NEXT_INST()    - advances ip and breaks or dispatches to next instruction
 *   DISPATCH_NEXT()- breaks (switch loop) or dispatches directly (threaded)
 */

OP_HANDLER(BC_PUSH_INT) : result.type = CDSL_TYPE_INT;
result.data.int_val = ip->operand.int_val;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_FLOAT) : result.type = CDSL_TYPE_FLOAT;
result.data.float_val = ip->operand.float_val;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_BOOL) : result.type = CDSL_TYPE_BOOL;
result.data.bool_val = ip->operand.bool_val;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_STRING) : result.type = CDSL_TYPE_STRING;
result.data.string_val = bc->constants[ip->operand.const_idx].data.string_val;
result.owned_string = 0;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_DATE) : result.type = CDSL_TYPE_DATE;
result.data.date_val = ip->operand.date_val;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_LONG) : result.type = CDSL_TYPE_LONG;
result.data.long_val = ip->operand.long_val;
PUSH(result);
NEXT_INST();

OP_HANDLER(BC_PUSH_ARRAY) :
{
	int count = ip->operand.int_val;
	cdsl_array_t* arr = cdsl_vm_calloc(vm, 1, sizeof(cdsl_array_t));
	if (arr) {
		arr->items =
		    cdsl_vm_calloc(vm, count > 0 ? (size_t)count : 1, sizeof(cdsl_value_t));
		if (!arr->items) {
			free(arr);
			arr = NULL;
		}
	}
	if (!arr) {
		for (int i = 0; i < count; i++) {
			cdsl_value_t tmp = POP();
			cdsl_value_free(&tmp);
		}
		if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_vm_set_last_error(
			    vm, CDSL_ERR_RUNTIME, 0, 0, "Out of memory allocating array", NULL);
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
		}
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	arr->count = count;
	arr->capacity = count;
	for (int i = count - 1; i >= 0; i--) {
		arr->items[i] = POP();
	}
	result.type = CDSL_TYPE_ARRAY;
	result.data.array_val = arr;
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_PUSH_VAR) :
{
	const char* var_name = bc->constants[ip->operand.const_idx].data.string_val;
	const cdsl_value_t* val_ptr = cdsl_context_lookup_value(ctx, var_name);
	if (val_ptr) {
		cdsl_value_t v = *val_ptr;
		if (v.type == CDSL_TYPE_STRING && v.data.string_val) {
			v.data.string_val = cdsl_vm_strdup(vm, v.data.string_val);
			v.owned_string = 1;
			if (!v.data.string_val) {
				v.type = CDSL_TYPE_VOID;
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
				if (!vm->last_error) {
					cdsl_vm_set_last_error(
					    vm,
					    CDSL_ERR_RESOURCE_LIMIT,
					    0,
					    0,
					    "Out of memory duplicating string variable in "
					    "bytecode",
					    NULL);
				}
				goto done;
			}
		} else if (v.type == CDSL_TYPE_ARRAY || v.type == CDSL_TYPE_MAP) {
			v = cdsl_value_copy(val_ptr);
		}
		PUSH(v);
	} else {
		result.type = CDSL_TYPE_VOID;
		result.data.int_val = 0;
		PUSH(result);
	}
}
NEXT_INST();

OP_HANDLER(BC_POP) : if (sp > 0)
{
	cdsl_value_t discarded = stack[--sp];
	cdsl_value_free(&discarded);
}
NEXT_INST();

OP_HANDLER(BC_ADD)
    : OP_HANDLER(BC_SUB)
    : OP_HANDLER(BC_MUL)
    : OP_HANDLER(BC_DIV)
    : OP_HANDLER(BC_EQ)
    : OP_HANDLER(BC_NE)
    : OP_HANDLER(BC_LT)
    : OP_HANDLER(BC_GT) : OP_HANDLER(BC_LE) : OP_HANDLER(BC_GE) : OP_HANDLER(BC_IN) :
{
	cdsl_value_t b = POP();
	cdsl_value_t a = POP();
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		cdsl_value_free(&a);
		cdsl_value_free(&b);
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	cdsl_op_t op = cdsl_bc_to_op(ip->op);
	cdsl_valop_status_t st = cdsl_value_binop(op, &a, &b, &result);
	cdsl_value_free(&a);
	cdsl_value_free(&b);
	if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
		if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_vm_set_last_error(vm,
					       CDSL_ERR_RUNTIME,
					       0,
					       0,
					       cdsl_valop_status_message(st),
					       cdsl_valop_status_hint(st));
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
		}
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_IN_RANGE) :
{
	cdsl_value_t max_v = POP();
	cdsl_value_t min_v = POP();
	cdsl_value_t val = POP();
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		cdsl_value_free(&val);
		cdsl_value_free(&min_v);
		cdsl_value_free(&max_v);
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	cdsl_value_t ge_res, le_res;
	cdsl_valop_status_t st1 = cdsl_value_binop(CDSL_OP_GE, &val, &min_v, &ge_res);
	cdsl_valop_status_t st2 = cdsl_value_binop(CDSL_OP_LE, &val, &max_v, &le_res);
	result.type = CDSL_TYPE_BOOL;
	if (st1 == CDSL_VALOP_OK && st2 == CDSL_VALOP_OK) {
		result.data.bool_val = (ge_res.data.bool_val && le_res.data.bool_val);
	} else {
		result.data.bool_val = 0;
	}
	cdsl_value_free(&val);
	cdsl_value_free(&min_v);
	cdsl_value_free(&max_v);
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_BUILD_MAP) :
{
	int count = ip->operand.int_val;
	cdsl_map_t* m = cdsl_map_create(count > 0 ? count * 2 : 16);
	if (!m) {
		result.type = CDSL_TYPE_VOID;
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		goto done;
	}
	for (int i = count - 1; i >= 0; i--) {
		cdsl_value_t item_val = POP();
		cdsl_value_t key_val = POP();
		if (key_val.type == CDSL_TYPE_STRING && key_val.data.string_val) {
			cdsl_map_set(m, key_val.data.string_val, &item_val);
		}
		cdsl_value_free(&key_val);
		cdsl_value_free(&item_val);
	}
	result.type = CDSL_TYPE_MAP;
	result.data.map_val = m;
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_GET_INDEX) :
{
	cdsl_value_t idx = POP();
	cdsl_value_t target = POP();
	result.type = CDSL_TYPE_VOID;
	if (target.type == CDSL_TYPE_MAP && target.data.map_val) {
		const char* k = (idx.type == CDSL_TYPE_STRING) ? idx.data.string_val : NULL;
		if (k) {
			const cdsl_value_t* found = cdsl_map_get(target.data.map_val, k);
			if (found) {
				result = cdsl_value_copy(found);
			}
		}
	} else if (target.type == CDSL_TYPE_ARRAY && target.data.array_val) {
		int i = (idx.type == CDSL_TYPE_INT)    ? idx.data.int_val
			: (idx.type == CDSL_TYPE_LONG) ? (int)idx.data.long_val
						       : -1;
		if (i >= 0 && i < target.data.array_val->count) {
			result = cdsl_value_copy(&target.data.array_val->items[i]);
		}
	}
	cdsl_value_free(&target);
	cdsl_value_free(&idx);
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_GET_PROP) :
{
	const char* prop = bc->constants[ip->operand.const_idx].data.string_val;
	cdsl_value_t target = POP();
	result.type = CDSL_TYPE_VOID;
	if (target.type == CDSL_TYPE_MAP && target.data.map_val && prop) {
		const cdsl_value_t* found = cdsl_map_get(target.data.map_val, prop);
		if (found) {
			result = cdsl_value_copy(found);
		}
	}
	cdsl_value_free(&target);
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_NOT) : OP_HANDLER(BC_NEG) :
{
	cdsl_value_t v = POP();
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		cdsl_value_free(&v);
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	cdsl_op_t op = cdsl_bc_to_op(ip->op);
	cdsl_valop_status_t st = cdsl_value_unop(op, &v, &result);
	cdsl_value_free(&v);
	if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
		if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_vm_set_last_error(vm,
					       CDSL_ERR_RUNTIME,
					       0,
					       0,
					       cdsl_valop_status_message(st),
					       cdsl_valop_status_hint(st));
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
		}
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_CALL) :
{
	const char* fn_name = bc->constants[ip->operand.const_idx].data.string_val;
	cdsl_value_t nv = POP();
	int nargs = (nv.type == CDSL_TYPE_INT && nv.data.int_val >= 0) ? (int)nv.data.int_val : 0;
	cdsl_arg_node_t* arg_list = NULL;
	cdsl_arg_node_t* arg_nodes = calloc(nargs > 0 ? (size_t)nargs : 1, sizeof(cdsl_arg_node_t));
	if (!arg_nodes) {
		for (int i = 0; i < nargs; i++) {
			cdsl_value_t av = POP();
			cdsl_value_free(&av);
		}
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		if (!vm->last_error) {
			cdsl_vm_set_last_error(vm,
					       CDSL_ERR_RESOURCE_LIMIT,
					       0,
					       0,
					       "Out of memory allocating builtin call arguments",
					       NULL);
		}
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	if (nargs > 0) {
		for (int i = nargs - 1; i >= 0; i--) {
			cdsl_value_t av = POP();
			arg_nodes[i].expr = cdsl_val_to_temp_expr_internal(&av);
			cdsl_value_free(&av);
		}
		for (int i = 0; i < nargs; i++) {
			arg_nodes[i].next = (i + 1 < nargs) ? &arg_nodes[i + 1] : NULL;
		}
		arg_list = &arg_nodes[0];
	}

	result.type = CDSL_TYPE_VOID;
	cdsl_func_cb_t target_fn = NULL;
	CDSL_RWLOCK_RDLOCK(&vm->rwlock);
	for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
		if (strcmp(fn->func_name, fn_name) == 0) {
			target_fn = fn->cb;
			break;
		}
	}
	CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);

	if (target_fn) {
		result = target_fn(fn_name, arg_list, ctx, vm);
	}

	for (int i = 0; i < nargs; i++) {
		cdsl_free_expr(arg_nodes[i].expr);
	}
	free(arg_nodes);
	PUSH(result);
}
NEXT_INST();

OP_HANDLER(BC_ACTION) :
{
	const char* act_name = bc->constants[ip->operand.const_idx].data.string_val;
	cdsl_value_t nv = POP();
	int nargs = (nv.type == CDSL_TYPE_INT && nv.data.int_val >= 0) ? nv.data.int_val : 0;
	cdsl_arg_node_t* arg_list = NULL;
	cdsl_arg_node_t* arg_nodes = calloc(nargs > 0 ? (size_t)nargs : 1, sizeof(cdsl_arg_node_t));
	if (!arg_nodes) {
		for (int i = 0; i < nargs; i++) {
			cdsl_value_t av = POP();
			cdsl_value_free(&av);
		}
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		if (!vm->last_error) {
			cdsl_vm_set_last_error(vm,
					       CDSL_ERR_RESOURCE_LIMIT,
					       0,
					       0,
					       "Out of memory allocating action call arguments",
					       NULL);
		}
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	for (int i = nargs - 1; i >= 0; i--) {
		cdsl_value_t av = POP();
		arg_nodes[i].expr = cdsl_val_to_temp_expr_internal(&av);
		cdsl_value_free(&av);
	}
	for (int i = 0; i < nargs; i++) {
		arg_nodes[i].next = (i + 1 < nargs) ? &arg_nodes[i + 1] : NULL;
	}
	arg_list = (nargs > 0) ? &arg_nodes[0] : NULL;

	cdsl_action_cb_t target_cb = NULL;
	CDSL_RWLOCK_RDLOCK(&vm->rwlock);
	for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
		if (strcmp(cb->action_name, act_name) == 0) {
			target_cb = cb->cb;
			break;
		}
	}
	CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);

	if (target_cb) {
		CDSL_ATOMIC_FETCH_ADD(&vm->stats.total_actions_triggered, 1);
		target_cb(act_name, arg_list, vm->user_data);
	}

	for (int i = 0; i < nargs; i++) {
		cdsl_free_expr(arg_nodes[i].expr);
	}
	free(arg_nodes);
}
NEXT_INST();

OP_HANDLER(BC_DUP) :
{
	if (sp <= 0) {
		cdsl_bc_stack_underflow(vm);
		result.type = CDSL_TYPE_VOID;
		goto done;
	}
	cdsl_value_t dup_v = cdsl_value_copy(&stack[sp - 1]);
	PUSH(dup_v);
}
NEXT_INST();

OP_HANDLER(BC_JMP_IF_TRUE) :
{
	cdsl_value_t v = PEEK();
	if (v.data.bool_val) {
		ip += ip->operand.jump_offset;
	} else {
		cdsl_value_t discarded = POP();
		cdsl_value_free(&discarded);
		ip++;
	}
}
DISPATCH_NEXT();

OP_HANDLER(BC_JMP_IF_FALSE) :
{
	cdsl_value_t v = PEEK();
	if (!v.data.bool_val) {
		ip += ip->operand.jump_offset;
	} else {
		cdsl_value_t discarded = POP();
		cdsl_value_free(&discarded);
		ip++;
	}
}
DISPATCH_NEXT();

OP_HANDLER(BC_JMP) : ip += ip->operand.jump_offset;
DISPATCH_NEXT();

OP_HANDLER(BC_METRIC_START)
    : OP_HANDLER(BC_SET_SCORE) : OP_HANDLER(BC_FAIL_METRIC) : OP_HANDLER(BC_RULE_END) :
{
	const cdsl_value_t* metric_val = NULL;
	cdsl_value_t popped = {.type = CDSL_TYPE_VOID};
	if (ip->op == BC_SET_SCORE) {
		popped = POP();
		metric_val = &popped;
	}
	if (metric_handler) {
		bc_metric_event_t ev;
		switch (ip->op) {
		case BC_METRIC_START:
			ev = BC_EV_METRIC_START;
			break;
		case BC_SET_SCORE:
			ev = BC_EV_SET_SCORE;
			break;
		case BC_FAIL_METRIC:
			ev = BC_EV_FAIL_METRIC;
			break;
		case BC_RULE_END:
			ev = BC_EV_RULE_END;
			break;
		default:
			ev = BC_EV_METRIC_START;
			break;
		}
		if (!metric_handler(ev, bc, ip, metric_val, user_data)) {
			cdsl_value_free(&popped);
			goto done;
		}
	}
	cdsl_value_free(&popped);
}
NEXT_INST();

OP_HANDLER(BC_RET) : result = POP();
goto done;
