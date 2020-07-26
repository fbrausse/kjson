/*
 * kjson.h
 *
 * Copyright 2019-2020 Franz Brauße <brausse@informatik.uni-trier.de>
 *
 * This file is part of kjson.
 * See the LICENSE file for terms of distribution.
 */

/* Requires C11 (for anonymous struct / union members) */

#ifndef KJSON_H
#define KJSON_H

#include <stdio.h>	/* FILE */
#include <stdbool.h>	/* bool, true, false */
#include <stdint.h>	/* intmax_t */

#ifdef __cplusplus
extern "C" {
#endif

#define KJSON_VERSION_SPLIT(major,minor,patch) \
	((major) << 16 | (minor) << 8 | (patch) << 0)

#define KJSON_VERSION	KJSON_VERSION_SPLIT(0,2,1)

uint32_t kjson_version(void);

struct kjson_parser {
	char *s;
};

enum kjson_value_type {
	KJSON_VALUE_NULL,
	KJSON_VALUE_BOOLEAN,
	KJSON_VALUE_NUMBER,
	KJSON_VALUE_STRING,
	KJSON_VALUE_ARRAY,
	KJSON_VALUE_OBJECT,
	KJSON_VALUE_N
};

struct kjson_string {
	char *begin;
	size_t len;
};

/* --------------------------------------------------------------------------
 * low-level interface (no composite values, no allocations)
 * -------------------------------------------------------------------------- */

bool kjson_read_bool(struct kjson_parser *p, bool *v);
bool kjson_read_null(struct kjson_parser *p);

/* Parses a JSON string entry into a '\0'-terminated UTF-8 string at *begin,
 * decoding any escaped characters (including surrogate pairs). len is optional
 * and, if non-NULL, on success will contain the length of the string.
 * On success, *begin will point into the original source, i.e., the source
 * string will be overwritten.
 */
bool kjson_read_string_utf8(struct kjson_parser *p, char **begin, size_t *len);

struct kjson_number {
	char *integer;
	char *fractional;
	char *exponent;
	char *end;
};

union kjson_leaf_raw {
	bool b;
	struct kjson_number n;
	struct kjson_string s;
};

int kjson_read_number(struct kjson_parser *p, union kjson_leaf_raw *leaf);

/* --------------------------------------------------------------------------
 * mid-level interface (callback-based parser, no allocations)
 * -------------------------------------------------------------------------- */

enum kjson_leaf_type {
	KJSON_LEAF_NULL    = KJSON_VALUE_NULL,
	KJSON_LEAF_BOOLEAN = KJSON_VALUE_BOOLEAN,
	KJSON_LEAF_NUMBER  = KJSON_VALUE_NUMBER,
	KJSON_LEAF_STRING  = KJSON_VALUE_STRING,
	KJSON_LEAF_N
};

struct kjson_mid_cb;

/* Tries to parse the non-string, non-boolean and non-null leaf at p->s.
 * On success returns its kjson_leaf_type after storing the interpreted content
 * in *l and advancing p->s to point just after the leaf. On error returns a
 * negative value. */
typedef int kjson_read_other_f(const struct kjson_mid_cb *c,
                               struct kjson_parser *p, union kjson_leaf_raw *l);

struct kjson_mid_cb {
	/* Called whenever a null-, boolean, numeric or string value is
	 * encountered, with the appropriate values set in 'type' and '*l'. */
	void (*leaf   )(const struct kjson_mid_cb *c, enum kjson_leaf_type type,
	                union kjson_leaf_raw *l);

	/* Called when a composite value is encountered, i.e. on parsing '['
	 * or '{'. */
	void (*begin)(const struct kjson_mid_cb *c, bool in_array);

	/* Called just before an array entry is parsed. */
	void (*a_entry)(const struct kjson_mid_cb *c);

	/* Called when an object entry is parsed, with *key containing a pointer
	 * to the key of the entry. */
	void (*o_entry)(const struct kjson_mid_cb *c, struct kjson_string *key);

	/* Called at the end of a composite value, i.e. on parsing ']'
	 * or '}'. */
	void (*end  )(const struct kjson_mid_cb *c, bool in_array);

	/* Called to parse a non-string, non-boolean and non-null leaf.
	 * If NULL, the default is to call kjson_read_number(p, l). */
	kjson_read_other_f *read_other;
};

/* requires stack space linear in the depth of the document */
bool kjson_parse_mid_rec(struct kjson_parser *p, const struct kjson_mid_cb *c);

/* requires only constant stack space, on my laptop same speed or a bit faster
 * than kjson_parse_mid_rec() */
bool kjson_parse_mid(struct kjson_parser *p, const struct kjson_mid_cb *c);

bool kjson_parse_mid2(struct kjson_parser *p, const struct kjson_mid_cb *c,
                      union kjson_leaf_raw *l);

/* --------------------------------------------------------------------------
 * high-level interface (dynamically build tree structure)
 * -------------------------------------------------------------------------- */

struct kjson_object_entry;

struct kjson_array {
	struct kjson_value *data;
	size_t n;
};

struct kjson_object {
	struct kjson_object_entry *data;
	size_t n;
};

struct kjson_value {
	enum kjson_value_type type;
	union {
		bool b;
		struct kjson_number n;
		struct kjson_string s;
		struct kjson_array a;
		struct kjson_object o;
	};
};

#define KJSON_VALUE_INIT	{ KJSON_VALUE_NULL, {} }

struct kjson_object_entry {
	struct kjson_string key;
	struct kjson_value value;
};

typedef void kjson_store_leaf_f(struct kjson_value *v,
                                enum kjson_leaf_type type,
                                union kjson_leaf_raw *l);

bool kjson_parse(struct kjson_parser *p, struct kjson_value *v);
bool kjson_parse2(struct kjson_parser *p, struct kjson_value *v,
                  kjson_read_other_f *read_other,
                  kjson_store_leaf_f *store_leaf);
void kjson_value_print(FILE *f, const struct kjson_value *v);
void kjson_value_fini(const struct kjson_value *v);

#ifdef __cplusplus
}
#endif

#endif
