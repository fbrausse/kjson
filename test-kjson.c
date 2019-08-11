
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

#include <string.h>	/* memcpy() */
#include <sys/time.h>	/* gettimeofday() */

#define MAX(a,b)	((a) > (b) ? (a) : (b))

int main(int argc, char **argv)
{
	int mid_cb = 0;
	int verbosity = 0;
	bool single_doc = false;
	size_t buf_sz = 4096;
	for (int opt; (opt = getopt(argc, argv, ":1b:hm:v")) != -1;)
		switch (opt) {
		case '1': single_doc = true; break;
		case 'b':
			if (sscanf(optarg, "%zu", &buf_sz) < 1 || !buf_sz)
				DIE(1,"cannot parse parameter to '-b' as size\n");
			break;
		case 'h': DIE(1,"usage: %s [-1] [ -m { 1 | 2 } | -v ]\n", argv[0]);
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
	size_t data_cap = buf_sz;
	char *data = malloc(data_cap);
	if (single_doc) {
		size_t data_sz = 0;
		static char buf[4096];
		for (size_t rd; (rd = fread(buf, 1, sizeof(buf), stdin)) > 0;) {
			if (data_sz + rd > data_cap) {
				data = realloc(data, data_cap = MAX(rd, data_cap * 2));
				assert(data);
			}
			memcpy(data + data_sz, buf, rd);
			data_sz += rd;
			if (rd < sizeof(buf))
				break;
		}
		assert(feof(stdin));
		struct kjson_parser p = { data };
		struct timeval tv, tw;
		gettimeofday(&tv, NULL);
		bool r = parse_f(&p, cb);
		gettimeofday(&tw, NULL);
		assert(r);
		(void)r;
		fprintf(stderr, "time: %luµs\n", (tw.tv_sec - tv.tv_sec) * 1000000 + (tw.tv_usec - tv.tv_usec));
	} else {
		for (int n=0; getline(&data, &data_cap, stdin) > 0; n++) {
			struct kjson_parser p = { data };
			bool r = parse_f(&p, cb);
			assert(r);
			(void)r;
		}
	}
	free(data);
	return 0;
}
