
#ifndef KJSON_H
#define KJSON_H

#include <stdio.h>	/* FILE */
#include <stdbool.h>	/* bool, true, false */
#include <stdint.h>	/* intmax_t */

#ifdef __cplusplus
extern "C" {
#endif

struct kjson_parser {
	char *s;
};

enum kjson_value_type {
	KJSON_VALUE_NULL,
	KJSON_VALUE_BOOLEAN,
	KJSON_VALUE_NUMBER_INTEGER,
	KJSON_VALUE_NUMBER_DOUBLE,
	KJSON_VALUE_STRING,
	KJSON_VALUE_ARRAY,
	KJSON_VALUE_OBJECT,
};

struct kjson_string {
	char *begin;
	size_t len;
};

/* low-level interface (no composite values, no allocations) */

bool kjson_read_bool(struct kjson_parser *p, bool *v);
bool kjson_read_null(struct kjson_parser *p);
bool kjson_read_string_utf8(struct kjson_parser *p, char **begin, size_t *len);
bool kjson_read_integer(struct kjson_parser *p, intmax_t *v);
void kjson_read_double(struct kjson_parser *p, double *v);

/* mid-level interface (callback-based streaming parser, no allocations) */

enum kjson_leaf_type {
	KJSON_LEAF_NULL           = KJSON_VALUE_NULL,
	KJSON_LEAF_BOOLEAN        = KJSON_VALUE_BOOLEAN,
	KJSON_LEAF_NUMBER_INTEGER = KJSON_VALUE_NUMBER_INTEGER,
	KJSON_LEAF_NUMBER_DOUBLE  = KJSON_VALUE_NUMBER_DOUBLE,
	KJSON_LEAF_STRING         = KJSON_VALUE_STRING,
};

union kjson_leaf_raw {
	bool b;
	intmax_t i;
	double d;
	struct kjson_string s;
};

struct kjson_mid_cb {
	void (*leaf   )(struct kjson_mid_cb *c, enum kjson_leaf_type type,
	                union kjson_leaf_raw *l);
	void (*a_begin)(struct kjson_mid_cb *c);
	void (*a_entry)(struct kjson_mid_cb *c);
	void (*a_end  )(struct kjson_mid_cb *c);
	void (*o_begin)(struct kjson_mid_cb *c);
	void (*o_key  )(struct kjson_mid_cb *c, struct kjson_string *key);
	void (*o_end  )(struct kjson_mid_cb *c);
};

/* requires stack space linear in the depth of the document */
bool kjson_parse_mid_rec(struct kjson_parser *p, struct kjson_mid_cb *c);
/* requires only constant stack space, on my laptop same speed or a bit faster
 * than kjson_parse_mid_rec() */
bool kjson_parse_mid(struct kjson_parser *p, struct kjson_mid_cb *c);

/* high-level interface (dynamically build tree structure) */

struct kjson_object_entry;

struct kjson_value {
	enum kjson_value_type type;
	union {
		bool b;
		intmax_t i;
		double d;
		struct kjson_string s;
		struct kjson_array {
			struct kjson_value *data;
			size_t n;
		} a;
		struct kjson_object {
			struct kjson_object_entry *data;
			size_t n;
		} o;
	};
};

struct kjson_object_entry {
	struct kjson_string key;
	struct kjson_value value;
};

bool kjson_parse(struct kjson_parser *p, struct kjson_value *v);
void kjson_value_print(FILE *f, const struct kjson_value *v);
void kjson_value_fini(const struct kjson_value *v);

#ifdef __cplusplus
}
#endif

#endif
