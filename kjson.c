/*
 * kjson.c
 *
 * Copyright 2019-2020 Franz Brauße <brausse@informatik.uni-trier.de>
 *
 * This file is part of kjson.
 * See the LICENSE file for terms of distribution.
 */

/* Requires C11 (for anonymous struct / union members) */

#include <string.h>	/* strncmp(3), strchr(3) */
#include <stdlib.h>	/* malloc(3), free(3) */
#include <inttypes.h>	/* uint_least32_t, PRIdMAX */
#include <errno.h>	/* errno(3) */
#include <assert.h>	/* assert(3) */
#include <limits.h>	/* CHAR_BIT */
#include <math.h>	/* ldexp() */

#include "kjson.h"

uint32_t kjson_version(void) { return KJSON_VERSION; }

/* --------------------------------------------------------------------------
 * low-level interface
 * -------------------------------------------------------------------------- */

bool kjson_read_null(struct kjson_parser *p)
{
	if (strncmp(p->s, "null", 4))
		return false;
	p->s += 4; /* skip "null" */
	return true;
}

bool kjson_read_bool(struct kjson_parser *p, bool *v)
{
	if (!strncmp(p->s, "true", 4)) {
		p->s += 4;
		*v = true;
	} else if (!strncmp(p->s, "false", 5)) {
		p->s += 5;
		*v = false;
	} else
		return false;
	return true;
}

/* Converts the hex-string u[0..3] to a 16-bit value and stores it in *uni. */
static bool hex4(unsigned *uni, const char *u)
{
	*uni = 0;
	for (int i=0; i<4; i++) {
		unsigned v;
		if ('0' <= *u && *u <= '9')
			v = *u - '0';
		else if ('A' <= *u && *u <= 'F')
			v = 10 + (*u - 'A');
		else if ('a' <= *u && *u <= 'f')
			v = 10 + (*u - 'a');
		else
			return false;
		*uni = *uni << 4 | v;
		u++;
	}
	return true;
}

/*                                 0 12345678 */
static const char str_subst1[] = "\"\\/\b\f\n\r\t";
static const char str_pat   [] = "\"\\/bfnrtu";

/* Decodes escape sequence(s) at p->s into *r and advances both, p->s and *r
 * to reflect the amount read and written, respectively. p->s and *r are
 * assumed to point to UTF-8 strings, in which case escaped(r, p) is just
 * appending the corresponding JSON-escaped code-point to *r. See README.md
 * for details. */
static bool escaped(char **r, struct kjson_parser *p)
{
	const char *at = strchr(str_pat, *p->s);
	if (!at)
		return false;
	if (at - str_pat <= 7) {
		*(*r)++ = str_subst1[at - str_pat];
		p->s++;
		return true;
	}
	unsigned uni;
	if (!hex4(&uni, ++p->s)) /* skip 'u' */
		return false;
	p->s += 4;
	if (uni < 0x80)
		*(*r)++ = uni;
	else if (uni < 0x800) {
		*(*r)++ = 0xc0 | uni >> 6;
		*(*r)++ = 0x80 | (uni & 0x3f);
	} else if (uni < 0xd800 || uni >= 0xe000) {
		*(*r)++ = 0xe0 | uni >> 12;
		*(*r)++ = 0x80 | ((uni >> 6) & 0x3f);
		*(*r)++ = 0x80 | (uni & 0x3f);
	} else if (uni < 0xdc00) {
		/* surrogate pair; uni is hi surrogate */
		unsigned lo;
		if (p->s[0] != '\\' || p->s[1] != 'u' || !hex4(&lo, p->s+2) ||
		    !(0xdc00 <= lo && lo < 0xe000))
			return false;
		p->s += 6;
		uint_least32_t v = uni & ~0xdc00;
		v <<= 10;
		v |= lo & ~0xdc00;
		v += 0x10000;
		*(*r)++ = 0xf0 | v >> 18;
		*(*r)++ = 0x80 | ((v >> 12) & 0x3f);
		*(*r)++ = 0x80 | ((v >> 6) & 0x3f);
		*(*r)++ = 0x80 | (v & 0x3f);
	} else {
		/* invalid lo surrogate first */
		return false;
	}
	return true;
}

#define REPEATED8_16(v)	((v) << 8 | (v))
#define REPEATED8_32(v)	(REPEATED8_16(v) << 16 | REPEATED8_16(v))
#define REPEATED8_64(v)	(REPEATED8_32(v) << 32 | REPEATED8_32(v))
#define CONCAT(a,b)	a ## b
#define XCONCAT(a,b)	CONCAT(a,b)

#if CHAR_BIT == 8
# ifndef LONG_BIT
#  if ULONG_MAX == 0xffffffff
#   define LONG_BIT		32
#  elif ULONG_MAX == 0xffffffffffffffff
#   define LONG_BIT		64
#  endif
# endif
# ifdef LONG_BIT
#  define UL_REPEATED8(v)	XCONCAT(REPEATED8_,LONG_BIT)((unsigned long)(v))
# endif
#endif

bool kjson_read_string_utf8(struct kjson_parser *p, char **begin, size_t *len)
{
	if (*p->s != '"')
		return false;
	p->s++; /* skip '"' */
	*begin = p->s;
	char *end;
#ifndef UL_REPEATED8
	/* quite fast search (in case padding bits exist in unsigned long) */
	for (;; p->s++) {
		if (*p->s == '"' || *p->s == '\\')
			goto escape;
		if ((unsigned char)*p->s <= 0x1f)
			return false;
#else
	/* slow search until pointer is aligned */
	for (; (uintptr_t)p->s % sizeof(unsigned long); p->s++) {
		if (*p->s == '"' || *p->s == '\\')
			goto escape;
		if ((unsigned char)*p->s <= 0x1f)
			return false;
	}
	/* search sizeof(unsigned long) bytes at a time -- trick from
	 * glibc memchr(3) */
	for (unsigned long ones = UL_REPEATED8(0x01), high_bits = ones << 7;;
	     p->s += sizeof(unsigned long)) {
		/* x_i < 0x20 -> false
		 * '"' = 0x22 -> end / break
		 * '\\' = 0x5c -> escape */
		unsigned long x = *(unsigned long *)p->s;
		unsigned long a = x ^ UL_REPEATED8('"');
		unsigned long b = x ^ UL_REPEATED8('\\');
		if ((((a - ones) & ~a) | ((b - ones) & ~b)) & high_bits)
			goto escape;
		/* mask ASCII control symbols (except DEL = 0x7f, since it's
		 * allowed in JSON strings) */
		unsigned long d = x & ~UL_REPEATED8(0x1f);
		if (((d - ones) & ~d) & high_bits)
			return false;
	}
#endif
escape:
	end = p->s;
	/* even slower search, replacing escapes (they're always shorter than
	 * the escape sequence itself) */
	while (*p->s != '"') {
		if ((unsigned char)*p->s <= 0x1f)
			return false;
		if (*p->s == '\\') {
			p->s++;
			if (!escaped(&end, p))
				return false;
		} else {
			if (end != p->s)
				*end = *p->s;
			end++, p->s++;
		}
	}
	*end = '\0';
	if (len)
		*len = end - *begin;
	p->s++;
	return true;
}

/* --------------------------------------------------------------------------
 * mid-level interface
 * -------------------------------------------------------------------------- */

static void skip_space(struct kjson_parser *p)
{
	p->s += strspn(p->s, "\t\r\n ");
}

int kjson_read_number(struct kjson_parser *p, union kjson_leaf_raw *leaf)
{
	leaf->n.integer = p->s;
	if (*p->s == '-')
		p->s++;
	if (*p->s == '0') {
		p->s++;
	} else {
		if (*p->s < '1' || *p->s > '9')
			return -1;
		do p->s++; while ('0' <= *p->s && *p->s <= '9');
	}
	leaf->n.fractional = p->s;
	if (*p->s == '.') {
		p->s++;
		if (!('0' <= *p->s && *p->s <= '9'))
			return -1;
		do p->s++; while ('0' <= *p->s && *p->s <= '9');
	}
	leaf->n.exponent = p->s;
	if (*p->s == 'E' || *p->s == 'e') {
		p->s++;
		if (*p->s == '+' || *p->s == '-')
			p->s++;
		if (!('0' <= *p->s && *p->s <= '9'))
			return -1;
		do p->s++; while ('0' <= *p->s && *p->s <= '9');
	}
	leaf->n.end = p->s;
	return KJSON_LEAF_NUMBER;
}

static int kjson_parse_leaf(struct kjson_parser *p, union kjson_leaf_raw *leaf,
                            const struct kjson_mid_cb *cb)
{
	if (*p->s == '"') {
		if (!kjson_read_string_utf8(p, &leaf->s.begin, &leaf->s.len))
			return -1;
		return KJSON_LEAF_STRING;
	} else if (kjson_read_null(p)) {
		return KJSON_LEAF_NULL;
	} else if (kjson_read_bool(p, &leaf->b)) {
		return KJSON_LEAF_BOOLEAN;
	} else {
		return cb->read_other ? cb->read_other(cb, p, leaf)
		                      : kjson_read_number(p, leaf);
	}
}

bool kjson_parse_mid_rec(struct kjson_parser *p, const struct kjson_mid_cb *c)
{
	if (*p->s == '[') {
		p->s++; /* skip '[' */
		c->begin(c, true);
		skip_space(p);
		if (*p->s != ']')
			while (1) {
				c->a_entry(c);
				if (!kjson_parse_mid_rec(p, c))
					return false;
				skip_space(p);
				if (*p->s != ',')
					break;
				p->s++; /* skip ',' */
				skip_space(p);
			}
		if (*p->s != ']')
			return false;
		p->s++; /* skip ']' */
		c->end(c, true);
	} else if (*p->s == '{') {
		p->s++; /* skip '{' */
		c->begin(c, false);
		skip_space(p);
		if (*p->s != '}')
			while (1) {
				struct kjson_string key;
				if (!kjson_read_string_utf8(p, &key.begin,
				                            &key.len))
					return false;
				skip_space(p);
				if (*p->s != ':')
					return false;
				p->s++; /* skip ':' */
				c->o_entry(c, &key);
				skip_space(p);
				if (!kjson_parse_mid_rec(p, c))
					return false;
				skip_space(p);
				if (*p->s != ',')
					break;
				p->s++; /* skip ',' */
				skip_space(p);
			}
		if (*p->s != '}')
			return false;
		p->s++; /* skip '}' */
		c->end(c, false);
	} else {
		union kjson_leaf_raw leaf;
		int r = kjson_parse_leaf(p, &leaf, c);
		if (r < 0)
			return false;
		c->leaf(c, (enum kjson_leaf_type)r, &leaf);
	}
	return true;
}

bool kjson_parse_mid(struct kjson_parser *p, const struct kjson_mid_cb *c)
{
	union kjson_leaf_raw leaf;
	return kjson_parse_mid2(p, c, &leaf);
}

bool kjson_parse_mid2(struct kjson_parser *p, const struct kjson_mid_cb *c,
                      union kjson_leaf_raw *leaf)
{
	/* In the stack-based parser kjson_parse_mid_rec() above, the only
	 * information stored for each level of parse tree is whether it is
	 * inside an object or inside an array. However, upon reading new
	 * tokens, this information can locally be derived since inside objects
	 * they must start with STR ':' while arrays there is no ':'. Also the
	 * end of these composite structures can be differentiated by the
	 * different closing braces they are encoded by.
	 *
	 * Here we use this property to the effect of not requiring any
	 * dynamic allocations and still produce the same trace as the recursive
	 * version above. */

	/* Assume the parse tree has depth less than 2^31; use 'size_t' if
	 * that's not enough. */
	unsigned depth = 0;
	                             /* next string token, only valid if ... */
	bool leaf_have_str = false;  /* ... leaf_have_str is true (in arrays) */
	while (1) {
		char fst = *p->s;

		/* Remember whether an array or object has been opened in this
		 * iteration. */
		bool ao_fst = false;
		/* If there is a string in 'leaf', we for sure are in an array
		 * and don't need to decide that again at the end of the
		 * loop. */
		bool known_in_arr = leaf_have_str; /* optimization for arrays */

		if (leaf_have_str) {
			/* The previous iteration left a string token in
			 * 'leaf'. */
			c->leaf(c, KJSON_LEAF_STRING, leaf);
			leaf_have_str = false;
		} else if (fst == '[' || fst == '{') {
			/* Begin a new composite token; empty composites are
			 * handled entirely here. */
			p->s++;
			skip_space(p);
			bool this_is_arr = (fst == '[');
			c->begin(c, this_is_arr);
			/* We assume UTF-8 input, since it's JSON, therefore,
			 * in the ASCII subset, '['+2 == ']' and '{'+2 == '}' */
			if (*p->s == fst+2) {
				p->s++;
				skip_space(p);
				c->end(c, this_is_arr);
			} else {
				depth++;
				ao_fst = true;
				known_in_arr = this_is_arr;
			}
		} else {
			/* Leaf token. */
			int r = kjson_parse_leaf(p, leaf, c);
			if (r < 0)
				return false;
			c->leaf(c, r, leaf);
		}

		/* For all but the first entry of composites: the next token is
		 * not ',' if and only if this is the end of the composite.
		 * Close all such composites. */
		if (!ao_fst)
			while (depth && (skip_space(p), *p->s != ',')) {
				switch (*p->s) {
				case ']': c->end(c, true); break;
				case '}': c->end(c, false); break;
				default: return false;
				}
				p->s++;
				depth--;
				known_in_arr = false;
			}

		/* Token read (and back) on top-level, this is the end. */
		if (!depth)
			return true;

		/* Here, we are sure inside a composite (depth != 0) and the
		 * loop above ensures that the next token is ',', i.e., another
		 * element of the composite follows. */
		if (!ao_fst) {
			if (*p->s != ',')
				return false;
			p->s++;
			skip_space(p);
		}

		/* If we already determined that we are in an array *or* the
		 * next token is not a string, it must be an array. */
		if (known_in_arr || *p->s != '"')
			c->a_entry(c);
		else {
			/* maybe object */
			if (!kjson_read_string_utf8(p, &leaf->s.begin,
			                               &leaf->s.len))
				return false;
			skip_space(p);
			if (*p->s == ':') {
				/* in object */
				c->o_entry(c, &leaf->s);
				p->s++; /* skip ':' */
				skip_space(p);
			} else {
				/* in array, keep 'leaf' for the next
				 * iteration */
				c->a_entry(c);
				leaf_have_str = true;
			}
		}
	}
}

/* --------------------------------------------------------------------------
 * high-level interface
 * -------------------------------------------------------------------------- */

struct high_cb {
	const struct kjson_mid_cb parent;
	struct elem {
		struct kjson_value *v;
		union {
			struct kjson_array arr;
			struct kjson_object obj;
		};
		size_t cap;
	} *stack;
	size_t stack_sz;
	size_t stack_cap;
	kjson_store_leaf_f *store_leaf;
};

#define ELEM_INIT { .arr = { .data = NULL, .n = 0 }, .cap = 0 }

static struct elem * top(struct high_cb *cb)
{
	return &cb->stack[cb->stack_sz-1];
}

static void high_leaf(const struct kjson_mid_cb *c, enum kjson_leaf_type type,
                      union kjson_leaf_raw *l)
{
	struct high_cb *cb = (struct high_cb *)c;
	struct kjson_value *v = top(cb)->v;
	switch (type) {
	case KJSON_LEAF_NULL:
		v->type = KJSON_VALUE_NULL;
		break;
	case KJSON_LEAF_BOOLEAN:
		v->type = KJSON_VALUE_BOOLEAN;
		v->b = l->b;
		break;
	case KJSON_LEAF_NUMBER:
		v->type = KJSON_VALUE_NUMBER;
		v->n = l->n;
		break;
	case KJSON_LEAF_STRING:
		v->type = KJSON_VALUE_STRING;
		v->s = l->s;
		break;
	default:
		cb->store_leaf(v, type, l);
		break;
	}
}

static void ensure_one_left(size_t n, size_t *cap, void **data, size_t elem_sz)
{
	if (n == *cap) {
		*cap = 2*(*cap ? *cap : 1);
		*data = realloc(*data, *cap * elem_sz);
	}
}

#define ENSURE_ONE_LEFT(n,cap,data) \
	ensure_one_left(n,cap,(void **)(data),sizeof(**(data)))

static void high_begin(const struct kjson_mid_cb *c, bool in_a)
{
	(void)in_a;
	struct high_cb *cb = (struct high_cb *)c;
	ENSURE_ONE_LEFT(cb->stack_sz, &cb->stack_cap, &cb->stack);
	cb->stack_sz++;
	*top(cb) = (struct elem)ELEM_INIT;
}

static void high_a_entry(const struct kjson_mid_cb *c)
{
	struct high_cb *cb = (struct high_cb *)c;
	struct elem *e = top(cb);
	struct kjson_array *arr = &e->arr;
	ENSURE_ONE_LEFT(arr->n, &e->cap, &arr->data);
	e->v = &arr->data[arr->n++];
}

static void high_o_entry(const struct kjson_mid_cb *c, struct kjson_string *key)
{
	struct high_cb *cb = (struct high_cb *)c;
	struct elem *e = top(cb);
	struct kjson_object *obj = &e->obj;
	ENSURE_ONE_LEFT(obj->n, &e->cap, &obj->data);
	struct kjson_object_entry *oe = &obj->data[obj->n++];
	oe->key = *key;
	e->v = &oe->value;
}

static void high_end(const struct kjson_mid_cb *c, bool in_a)
{
	struct high_cb *cb = (struct high_cb *)c;
	struct elem *e = top(cb);
	cb->stack_sz--;
	struct kjson_value *v = top(cb)->v;
	if (in_a) {
		v->type = KJSON_VALUE_ARRAY;
		v->a = e->arr;
	} else {
		v->type = KJSON_VALUE_OBJECT;
		v->o = e->obj;
	}
}

bool kjson_parse(struct kjson_parser *p, struct kjson_value *v)
{
	return kjson_parse2(p, v, NULL, NULL);
}

bool kjson_parse2(struct kjson_parser *p, struct kjson_value *v,
                  kjson_read_other_f *read_other,
                  kjson_store_leaf_f *store_leaf)
{
	struct high_cb cb = {
		.parent = {
			.leaf       = high_leaf,
			.begin      = high_begin,
			.a_entry    = high_a_entry,
			.o_entry    = high_o_entry,
			.end        = high_end,
			.read_other = read_other,
		},
		.stack      = malloc(sizeof(struct elem)),
		.stack_sz   = 1,
		.stack_cap  = 1,
		.store_leaf = store_leaf,
	};
	cb.stack[0] = (struct elem){ .v = v, };
	bool r = kjson_parse_mid(p, &cb.parent);
	assert(!r || cb.stack_sz == 1);
	free(cb.stack);
	return r;
}

static void kjson_value_print_composite(FILE *f, const struct kjson_value *v,
                                        int depth)
{
	switch (v->type) {
	case KJSON_VALUE_NULL:
		fprintf(f, "null");
		break;
	case KJSON_VALUE_BOOLEAN:
		fprintf(f, "%s", v->b ? "true" : "false");
		break;
	case KJSON_VALUE_NUMBER:
		fprintf(f, "%.*s", (int)(v->n.end - v->n.integer),
		        v->n.integer);
		break;
	case KJSON_VALUE_STRING:
		fputc('"', f);
		for (size_t i=0; i<v->s.len; i++) {
			char c = v->s.begin[i];
			if (c == '"' || c == '\\')
				fprintf(f, "\\%c", c);
			else if ((unsigned char)c <= 0x1f)
				fprintf(f, "\\u%04x", c);
			else
				fputc(c, f);
		}
		fputc('"', f);
		break;
	case KJSON_VALUE_OBJECT:
		if (!v->o.n) {
			fprintf(f, "{}");
		} else {
			fprintf(f, "{\n%*s", 4*(depth+1), "");
			for (size_t i=0; i<v->o.n; i++) {
				fprintf(f, "\"%.*s\": ",
				        (int)v->o.data[i].key.len,
				        v->o.data[i].key.begin);
				kjson_value_print_composite(f,
				                            &v->o.data[i].value,
				                            depth+1);
				if (i+1 < v->o.n)
					fprintf(f, ",\n%*s", 4*(depth+1), "");
			}
			fprintf(f, "\n%*s}", 4*depth, "");
		}
		break;
	case KJSON_VALUE_ARRAY:
		if (!v->a.n) {
			fprintf(f, "[]");
		} else {
			fprintf(f, "[");
			for (size_t i=0; i<v->o.n; i++) {
				kjson_value_print_composite(f, &v->a.data[i],
				                            depth+1);
				if (i+1 < v->o.n)
					fprintf(f, ", ");
			}
			fprintf(f, "]");
		}
		break;
	default: return;
	}
}

void kjson_value_print(FILE *f, const struct kjson_value *v)
{
	kjson_value_print_composite(f, v, 0);
}

void kjson_value_fini(const struct kjson_value *v)
{
	switch (v->type) {
	case KJSON_VALUE_NULL:
	case KJSON_VALUE_BOOLEAN:
	case KJSON_VALUE_NUMBER:
	case KJSON_VALUE_STRING:
		return;
	case KJSON_VALUE_ARRAY:
		for (size_t i=0; i<v->a.n; i++)
			kjson_value_fini(&v->a.data[i]);
		free(v->a.data);
		return;
	case KJSON_VALUE_OBJECT:
		for (size_t i=0; i<v->o.n; i++)
			kjson_value_fini(&v->o.data[i].value);
		free(v->o.data);
		return;
	default: return;
	}
}
