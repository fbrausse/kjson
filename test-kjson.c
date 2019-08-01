
/* Requires C11 (for anonymous struct / union members)
 * and      _POSIX_C_SOURCE >= 200809L */

#include <stdio.h>	/* FILE, getline(3) */
#include <assert.h>	/* assert(3) */
#include <stdlib.h>	/* exit(3), free(3) */
#include <inttypes.h>	/* PRIdMAX */

#include "kjson.h"

static void dbg_leaf(const struct kjson_mid_cb *c, enum kjson_leaf_type type,
                     union kjson_leaf_raw *l)
{
	(void)c;
	printf("leaf: ");
	switch (type) {
	case KJSON_LEAF_NULL: printf("null\n"); break;
	case KJSON_LEAF_BOOLEAN: printf("%s\n", l->b ? "true" : "false"); break;
	case KJSON_LEAF_NUMBER_INTEGER: printf("%" PRIdMAX "\n", l->i); break;
	case KJSON_LEAF_NUMBER_DOUBLE: printf("%f\n", l->d); break;
	case KJSON_LEAF_STRING:
		printf("\"%.*s\"\n", (int)l->s.len, l->s.begin);
		break;
	}
}

static void dbg_begin(const struct kjson_mid_cb *c, bool in_array)
{
	(void)c;
	printf("%s begin\n", in_array ? "array" : "obj");
}

static void dbg_a_entry(const struct kjson_mid_cb *c)
{
	(void)c;
	printf("array entry\n");
}

static void dbg_end(const struct kjson_mid_cb *c, bool in_array)
{
	(void)c;
	printf("%s end\n", in_array ? "array" : "obj");
}

static void dbg_o_entry(const struct kjson_mid_cb *c, struct kjson_string *key)
{
	(void)c;
	printf("obj entry: %.*s\n", (int)key->len, key->begin);
}

static const struct kjson_mid_cb dbg_cb = {
	.leaf    = dbg_leaf   ,
	.begin   = dbg_begin  ,
	.a_entry = dbg_a_entry,
	.o_entry = dbg_o_entry,
	.end     = dbg_end    ,
};

static void null() {}
static void null_be(const struct kjson_mid_cb *c, bool in_array)
{
	(void)c;
	(void)in_array;
}

static const struct kjson_mid_cb null_cb = {
	.leaf    = null,
	.begin   = null_be,
	.a_entry = null,
	.o_entry = null,
	.end     = null_be,
};

#include <unistd.h>

#define DIE(code,...) do { fprintf(stderr, __VA_ARGS__); exit(code); } while (0)

static bool high_v(struct kjson_parser *p, const struct kjson_mid_cb *cb)
{
	(void)cb;
	struct kjson_value v;
	bool r = kjson_parse(p, &v);
	kjson_value_print(stdout, &v);
	printf("\n");
	kjson_value_fini(&v);
	return r;
}

static bool high(struct kjson_parser *p, const struct kjson_mid_cb *cb)
{
	(void)cb;
	struct kjson_value v;
	bool r = kjson_parse(p, &v);
	kjson_value_fini(&v);
	return r;
}

int main(int argc, char **argv)
{
	int mid_cb = 0;
	int verbosity = 0;
	for (int opt; (opt = getopt(argc, argv, ":hm:v")) != -1;)
		switch (opt) {
		case 'h': DIE(1,"usage: %s [ -m { 1 | 2 } | -v ]\n", argv[0]);
		case 'm': mid_cb = atoi(optarg); break;
		case 'v': verbosity++; break;
		case ':': DIE(1,"error: option '-%c' requires a parameter\n",
			        optopt);
		case '?': DIE(1,"error: unknown option '-%c'\n", optopt);
		}
	bool (*parse_f)(struct kjson_parser *, const struct kjson_mid_cb *) =
		mid_cb == 1 ? kjson_parse_mid_rec :
		mid_cb == 2 ? kjson_parse_mid :
		verbosity ? high_v : high;
	const struct kjson_mid_cb *cb = verbosity ? &dbg_cb : &null_cb;
	char *line = NULL;
	size_t sz = 0;
	for (int n=0; getline(&line, &sz, stdin) > 0; n++) {
		struct kjson_parser p = { line };
		bool r = parse_f(&p, cb);
		assert(r);
		(void)r;
	}
	free(line);
}
