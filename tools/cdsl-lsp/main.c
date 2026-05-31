/**
 * @file tools/cdsl-lsp/main.c
 * @brief Minimal C-DSL Language Server (LSP) over stdio.
 *
 * Provides real-time diagnostics, auto-completion, and hover info
 * for .dsl files using the CDSL parser and verifier.
 *
 * Protocol: JSON-RPC 2.0 over stdin/stdout (no TCP).
 * Uses Content-Length: N\r\n\r\n framing per LSP spec.
 */

#define _POSIX_C_SOURCE 200809L
#include <cdsl/cdsl.h>
#include <cdsl/ast.h>
#include <cdsl/schema.h>
#include <cdsl/execution.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- JSON helpers (minimal, no external lib) ---- */

static char* json_esc(const char* s, char* buf, size_t sz) {
	size_t j = 0;
	for (const char* p = s; *p && j < sz - 2; p++) {
		if (*p == '"' || *p == '\\') { if (j < sz - 3) buf[j++] = '\\'; }
		buf[j++] = *p;
	}
	buf[j] = '\0';
	return buf;
}

#define MAX_MSG 65536
static char g_in[MAX_MSG], g_out[MAX_MSG], g_work[8192];

static int read_message(void) {
	char header[256];
	if (!fgets(header, sizeof(header), stdin)) return 0;
	int len = 0;
	sscanf(header, "Content-Length: %d", &len);
	/* skip optional Content-Type line and blank line */
	while (1) {
		int c = fgetc(stdin);
		if (c == '\n') break;
	}
	if (len <= 0 || len >= MAX_MSG) return 0;
	size_t total = 0;
	while (total < (size_t)len) {
		size_t n = fread(g_in + total, 1, (size_t)len - total, stdin);
		if (n == 0) break;
		total += n;
	}
	g_in[total] = '\0';
	return 1;
}

static void send_message(const char* json) {
	size_t len = strlen(json);
	fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", len, json);
	fflush(stdout);
}

static int json_get_str(const char* json, const char* key, char* out, size_t sz) {
	char pat[256];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char* p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	size_t i = 0;
	while (*p && *p != '"' && i < sz - 1) out[i++] = *p++;
	out[i] = '\0';
	return 1;
}

static int json_get_int(const char* json, const char* key, int* val) {
	char pat[128];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char* p = strstr(json, pat);
	if (!p) return 0;
	*val = atoi(p + strlen(pat));
	return 1;
}

/* ---- Schema management ---- */

static cdsl_schema_t* g_schema = NULL;
static char g_uri[1024] = "";
static char g_text[MAX_MSG] = "";
static int g_doc_version = 0;

static void init_schema(void) {
	if (g_schema) cdsl_schema_free(g_schema);
	g_schema = cdsl_schema_create();
	/* Register common domain variables */
	cdsl_schema_register_var(g_schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(g_schema, "user.email", CDSL_TYPE_STRING);
	cdsl_schema_register_var(g_schema, "user.role", CDSL_TYPE_STRING);
	cdsl_schema_register_var(g_schema, "transaction.amount", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(g_schema, "transaction.type", CDSL_TYPE_STRING);
	cdsl_schema_register_var(g_schema, "transaction.date", CDSL_TYPE_DATE);
	cdsl_schema_register_var(g_schema, "supplier.capital", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "supplier.rating", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(g_schema, "score.total", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(g_schema, "score.pass_threshold", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "big.id", CDSL_TYPE_LONG);
	cdsl_schema_register_var(g_schema, "big.count", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "start", CDSL_TYPE_DATE);
	cdsl_schema_register_var(g_schema, "name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(g_schema, "a", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_var(g_schema, "y", CDSL_TYPE_FLOAT);
	/* Register standard actions */
	cdsl_schema_register_action(g_schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(g_schema, "reject", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(g_schema, "approve", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(g_schema, "record_warning", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(g_schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
}

/* ---- Diagnostics ---- */

static void publish_diagnostics(void) {
	char items[MAX_MSG] = "";
	char buf[512];
	int has_error = 0;

	if (g_text[0]) {
		cdsl_rule_t* rule = cdsl_parse_string(g_text);
		if (!rule) {
			snprintf(buf, sizeof(buf),
				 "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
				 "\"end\":{\"line\":0,\"character\":0}},"
				 "\"severity\":1,\"message\":\"Parse error\"}%s",
				 items[0] ? "," : "");
			has_error = 1;
		} else {
			char verr[512] = {0};
			if (!cdsl_verify_rule(rule, g_schema, verr, sizeof(verr))) {
				char esc[1024];
				snprintf(buf, sizeof(buf),
					 "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
					 "\"end\":{\"line\":0,\"character\":0}},"
					 "\"severity\":1,\"message\":\"%s\"}%s",
					 json_esc(verr, esc, sizeof(esc)),
					 items[0] ? "," : "");
				has_error = 1;
			}
			cdsl_free_rule(rule);
		}
		if (has_error) strcat(items, buf);
	}

	snprintf(g_out, sizeof(g_out),
		 "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
		 "\"params\":{\"uri\":\"%s\",\"diagnostics\":[%s]}}",
		 g_uri, items);
	send_message(g_out);
}

/* ---- Completions ---- */

static const char* g_keywords[] = {
	"RULE", "META", "WHEN", "THEN", "METRIC", "CASE", "DEFAULT",
	"TEMPLATE", "EXTENDS", "AND", "OR", "NOT", "true", "false", NULL
};
static const char* g_builtins[] = {
	"strlen(", "contains(", "uppercase(", "lowercase(", "trim(",
	"startswith(", "endswith(", "abs(", "min(", "max(", "round(",
	"typeof(", "now(", "is_before(", "is_after(", "days_between(",
	"date_add(", NULL
};
static const char* g_metas[] = {
	"description", "weight", "is_critical", "pass_threshold",
	"partial_threshold", "depends_on", NULL
};
static const char* g_types[] = {"INT", "FLOAT", "BOOL", "STRING", "DATE", "LONG", "VOID", NULL};

static void send_completions(void) {
	char items[MAX_MSG] = "";
	char buf[512];

	/* Keywords */
	for (const char** k = g_keywords; *k; k++) {
		snprintf(buf, sizeof(buf), "{\"label\":\"%s\",\"kind\":14}%s", *k, items[0]?",":"");
		strcat(items, buf);
	}
	/* Built-in functions */
	for (const char** b = g_builtins; *b; b++) {
		snprintf(buf, sizeof(buf), "{\"label\":\"%s\",\"kind\":2,\"insertText\":\"%s\"}%s",
			 *b, *b, items[0]?",":"");
		strcat(items, buf);
	}
	/* Schema variables */
	if (g_schema) {
		for (cdsl_var_schema_t* v = g_schema->vars; v; v = v->next) {
			snprintf(buf, sizeof(buf),
				 "{\"label\":\"%s\",\"kind\":6,\"detail\":\"%s\"}%s",
				 v->name, g_types[v->type], items[0]?",":"");
			strcat(items, buf);
		}
		/* Schema actions */
		for (cdsl_action_schema_t* a = g_schema->actions; a; a = a->next) {
			snprintf(buf, sizeof(buf),
				 "{\"label\":\"%s(\",\"kind\":2,\"detail\":\"action\"}%s",
				 a->name, items[0]?",":"");
			strcat(items, buf);
		}
	}
	/* Meta keys */
	for (const char** m = g_metas; *m; m++) {
		snprintf(buf, sizeof(buf), "{\"label\":\"%s\",\"kind\":14,\"detail\":\"META key\"}%s",
			 *m, items[0]?",":"");
		strcat(items, buf);
	}

	snprintf(g_out, sizeof(g_out),
		 "{\"jsonrpc\":\"2.0\",\"id\":%d,"
		 "\"result\":{\"isIncomplete\":false,\"items\":[%s]}}",
		 g_doc_version, items);
	send_message(g_out);
}

/* ---- Hover ---- */

static void send_hover(void) {
	char result[1024] = "\"No information available\"";
	if (g_schema) {
		for (cdsl_var_schema_t* v = g_schema->vars; v; v = v->next) {
			if (strstr(g_in, v->name)) {
				snprintf(result, sizeof(result),
					 "\"Variable `%s`: %s%s\"",
					 v->name, g_types[v->type],
					 v->is_readonly ? " (read-only)" : "");
				break;
			}
		}
	}
	snprintf(g_out, sizeof(g_out),
		 "{\"jsonrpc\":\"2.0\",\"id\":%d,"
		 "\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":%s}}}",
		 g_doc_version, result);
	send_message(g_out);
}

/* ---- Main LSP loop ---- */

int main(void) {
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	init_schema();

	while (read_message()) {
		if (strstr(g_in, "initialize")) {
			g_doc_version = 1;
			json_get_int(g_in, "id", &g_doc_version);
			snprintf(g_out, sizeof(g_out),
				 "{\"jsonrpc\":\"2.0\",\"id\":%d,"
				 "\"result\":{\"capabilities\":{"
				 "\"textDocumentSync\":{\"openClose\":true,\"change\":2},"
				 "\"completionProvider\":{\"triggerCharacters\":[\".\",\" \"]},"
				 "\"hoverProvider\":true}}}",
				 g_doc_version);
			send_message(g_out);
		} else if (strstr(g_in, "initialized")) {
			/* no response needed */
		} else if (strstr(g_in, "textDocument/didOpen") ||
			   strstr(g_in, "textDocument/didChange")) {
			json_get_str(g_in, "uri", g_uri, sizeof(g_uri));
			const char* text_start = strstr(g_in, "\"text\":\"");
			if (text_start) {
				text_start += 8;
				char* d = g_text;
				const char* s = text_start;
				size_t n = 0;
				while (*s && n < MAX_MSG - 1) {
					if (*s == '\\' && s[1]) {
						if (s[1] == 'n') { *d++ = '\n'; s += 2; }
						else if (s[1] == 'r') { s += 2; }
						else if (s[1] == '"') { *d++ = '"'; s += 2; }
						else if (s[1] == '\\') { *d++ = '\\'; s += 2; }
						else { *d++ = *s++; }
					} else if (*s == '"') {
						break;
					} else {
						*d++ = *s++;
					}
					n++;
				}
				*d = '\0';
			}
			publish_diagnostics();
		} else if (strstr(g_in, "textDocument/completion")) {
			json_get_int(g_in, "id", &g_doc_version);
			send_completions();
		} else if (strstr(g_in, "textDocument/hover")) {
			json_get_int(g_in, "id", &g_doc_version);
			send_hover();
		} else if (strstr(g_in, "textDocument/didClose")) {
			g_uri[0] = '\0';
			g_text[0] = '\0';
		} else if (strstr(g_in, "shutdown")) {
			json_get_int(g_in, "id", &g_doc_version);
			snprintf(g_out, sizeof(g_out),
				 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", g_doc_version);
			send_message(g_out);
		} else if (strstr(g_in, "exit")) {
			break;
		}
	}
	cdsl_schema_free(g_schema);
	return 0;
}
