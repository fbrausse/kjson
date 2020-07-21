/*
 * kjson.hh
 *
 * Copyright 2019-2020 Franz Brauße <brausse@informatik.uni-trier.de>
 *
 * This file is part of kjson.
 * See the LICENSE file for terms of distribution.
 */

/*
 * C++ wrapper around kjson, offering a simple-to-use interface via the class
 * kjson::kjson and another one that can be used without exceptions or RTTI
 * called kjson::kjson_opt.  The latter uses std::optional to either represent
 * the requested value or nothing in case of an error.  kjson::kjson has the
 * advantage that usage is easier, e.g.
 *
 *   using kjson::kjson;
 *   kjson v = kjson::parse(std::cin);
 *   std::cout << v["key1"][1]["key2"].get<std::string_view>() << "\n";
 *
 * would print "42\n" for this JSON document:
 *
 *   { "key1": [ 23.4, { "key2": "42" }, -17 ] }
 *
 * kjson inherently is read-only, that is, it only provides read access to JSON
 * values.  This C++ wrapper does not interpret numeric strings in JSON
 * documents, just checks them for syntax and provides access to the string
 * representation via .get_number_rep().  For the example document above,
 *
 *   v["key1"][0].get_number_rep()
 *
 * would return a std::string_view containing "23.4".  In order to make access
 * easier, a .get<T>() function is provided, which uses the type traits
 *
 *   kjson::requests_string<S>
 *
 * and
 *
 *   kjson::requests_number<T>
 *
 * in order to ensure that the JSON node has the correct type when .get<T>() is
 * called on it.  Both default to std::false_type, however, the first one is
 * specialized to std::true_type for std::string and std::string_view, while for
 * the second one no specialization exists.  The reason is that all number types
 * built into C++ are not able to represent JSON numeric strings accurately.
 * E.g., 23.4 has no finite binary floating point representation and even
 * integers exceeding 64 bit have no standard C++ type, both however are
 * absolutely valid JSON values.
 *
 * Therefore std::requests_number<T> is a specialization point for user code.
 * If its ::value member is true, kjson invokes from_chars(start, end, T &x)
 * using argument-dependent lookup (and importing std::from_chars) and assumes
 * its signature follows that of std::from_chars, that is, it returns something
 * assignable to "auto [ec,p]" where ec is an std::errc error-code and p is a
 * pointer to the end of parsing.  If either ec != std::errc() or p != end,
 * parsing is assumed to have failed, otherwise x is returned by .get<T>().
 *
 * An example to parse the above 23.4 as a float using std::from_chars would be:
 *
 *   namespace kjson {
 *     template <> struct requests_number<float> : std::true_type {};
 *   }
 *   float f = v["key1"][0].get<float>();
 *
 * This specialization point can be used for arbitrary user-defined types, e.g.
 * kay::Z (from the 'kay' library) or mpz_class from GMP's C++ wrapper, in the
 * same way:
 *
 *   namespace kjson {
 *     template <> struct requests_number<mpz_class> : std::true_type {};
 *   }
 *   auto from_chars(const char *start, const char *end, mpz_class &x)
 *   {
 *      // GMP API doesn't know about std::string_view, so wrap the string
 *      x.set_str(std::string(start, end).c_str(), 10);
 *      return std::pair { std::errc(), end };
 *   }
 *   mpz_class g = v["key1"][2].get<mpz_class>();
 *
 * would set g to -17.
 *
 * Both, kjson::kjson and kjson::kjson_opt, at the moment use std::shared_ptr in
 * order to manage the "reference semantics" view that the C library kjson
 * assumes: the string given to kjson_parse() is assumed to exist while the
 * resulting kjson_value is used.  As an assumption, this works well in C,
 * however in the "ease of use" scenario in C++ this means we need to keep
 * around a copy of the string while any kjson::kjson{,_opt} instance is still
 * alive and we need to destroy the string afterwards as well as cleanup kjson
 * via kjson_value_fini().  It is important to note that "the string" could be
 * a (mutable) char * or a std::string, that is, its destructor has to be
 * called.  The alternative "copy the string" approach does not work with kjson,
 * as kjson_value-s point into the string that was originally parsed.  That is
 * the whole point of the C implementation.  Thus, here is a clear clash between
 * the views from C and from C++ and the most portable solution was by using the
 * heap and reference-count the parsed string object.
 */

#ifndef JSON_CC_HH
#define JSON_CC_HH

#include <vector>
#include <string_view>
#include <cstring>	/* memcmp */
#include <cassert>
#include <optional>
#include <regex>
#include <iostream>
#include <memory>
#include <charconv>	/* from_chars() */

#include <kjson.h>

namespace kjson {

namespace detail {

static const std::regex NUM_REGEX {"^"
	/* number ::= integer fraction exponent */
	"-?([0-9]|[1-9][0-9]+)" /* integer ::= '-'_opt (digit | onenine digits) */
	"(\\.[0-9]+)?"          /* fraction ::= '' | ('.' digits) */
	"([eE][+-]?[0-9]+)?"    /* exponent ::= '' | ([eE] sign digits) */
, std::regex_constants::extended };

template <typename T>
struct base : ::kjson_value {
	T str;
	base(T str)
	: ::kjson_value KJSON_VALUE_INIT
	, str(std::move(str))
	{}
	inline char * data();
};

template <> inline char * base<char *>::data() { return str; }
template <> inline char * base<std::string>::data() { return str.data(); }

template <typename Opt> class arr_itr;

}

template <typename T> struct requests_string : std::false_type {};
template <> struct requests_string<std::string> : std::true_type {};
template <> struct requests_string<std::string_view> : std::true_type {};

template <typename T> struct requests_number : std::false_type {};

enum class error : int {
	PARSE_JSON = 1,
	NOT_NULL,
	NOT_A_BOOLEAN,
	NOT_A_NUMBER,
	NOT_A_STRING,
	NOT_A_LIST,
	NOT_AN_OBJECT,
	KEY_NOT_UNIQUE,
	KEY_NOT_FOUND,
	INDEX_OUT_OF_BOUNDS,
	PARSE_NUMBER,
};

static const char *const error_messages[] = {
	nullptr,
	"JSON parse error",
	"not null",
	"not a boolean",
	"not a number",
	"not a list",
	"not an object",
	"key not unique",
	"key not found",
	"index out of bounds",
	"number parse error",
};

template <typename Opt>
class kjson_impl {

	template <typename R> using opt_t = typename Opt::template type<R>;

	static constexpr int NUMERIC = KJSON_VALUE_N;

	static int read_other(const struct kjson_mid_cb *,
	                      struct kjson_parser *p, union kjson_leaf_raw *l)
	{
		std::match_results<const char *> m;
		if (!regex_search(p->s, m, detail::NUM_REGEX))
			return -1;
		l->s.begin = p->s;
		l->s.len = m.length();
		p->s += m.length();
		return NUMERIC;
	}

	static void store_leaf(struct kjson_value *v,
	                       enum kjson_leaf_type type,
	                       union kjson_leaf_raw *l)
	{
		assert(type == NUMERIC);
		v->type = (enum kjson_value_type)NUMERIC;
		v->s = l->s;
	}

	template <typename T>
	static opt_t<kjson_impl<Opt>> parse(
		std::shared_ptr<detail::base<T>> ptr
	) {
		::kjson_parser p { ptr->data() };
		::kjson_value *v = ptr.get();
		if (!kjson_parse2(&p, v, read_other, store_leaf))
			return Opt::template none<kjson_impl<Opt>>(error::PARSE_JSON);
		return Opt::some(kjson_impl<Opt> { std::move(ptr), v });
	}

protected:
	std::shared_ptr<const ::kjson_value> b;
	const ::kjson_value *v;

	kjson_impl(const std::shared_ptr<const ::kjson_value> &b,
	           const ::kjson_value *v)
	: b(b)
	, v(v)
	{}

public:
	static opt_t<kjson_impl<Opt>> parse(char *s)
	{
		return parse(std::make_shared<detail::base<char *>>(s));
	}

	static opt_t<kjson_impl<Opt>> parse(std::string s)
	{
		return parse(std::make_shared<detail::base<std::string>>(std::move(s)));
	}

	static opt_t<kjson_impl<Opt>> parse(std::istream &i)
	{
		std::stringstream ss;
		ss << i.rdbuf();
		return parse(ss.str());
	}

	static opt_t<kjson_impl<Opt>> parse(std::istream &&i)
	{
		std::stringstream ss;
		ss << i.rdbuf();
		return parse(ss.str());
	}

	opt_t<size_t> count(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return Opt::template none<size_t>(error::NOT_AN_OBJECT);
		size_t r = 0;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin,
			            sv.length()))
				r++;
		return Opt::some(r);
	}

	opt_t<bool> contains(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return Opt::template none<bool>(error::NOT_AN_OBJECT);
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin,
			            sv.length()))
				return Opt::some(true);
		return Opt::some(false);
	}

	opt_t<std::vector<kjson_impl<Opt>>> get(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return Opt::template none<std::vector<kjson_impl<Opt>>>(
				error::NOT_AN_OBJECT
			);
		std::vector<kjson_impl<Opt>> r;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin, sv.length()))
				r.push_back({ b, &v->o.data[i].value });
		return Opt::some(std::move(r));
	}

	opt_t<kjson_impl<Opt>>  operator[](std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return Opt::template none<kjson_impl<Opt>>(error::NOT_AN_OBJECT);
		::kjson_value *found = nullptr;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin, sv.length())) {
				if (found)
					return Opt::template none<kjson_impl<Opt>>(error::KEY_NOT_UNIQUE);
				found = &v->o.data[i].value;
			}
		if (found)
			return Opt::some(kjson_impl<Opt> { b, found });
		return Opt::template none<kjson_impl<Opt>>(error::KEY_NOT_FOUND);
	}

	opt_t<size_t> size() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<size_t>(error::NOT_A_LIST);
		return Opt::some(v->a.n);
	}

	opt_t<kjson_impl<Opt>> operator[](size_t i) const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<kjson_impl<Opt>>(error::NOT_A_LIST);
		if (v->a.n <= i)
			return Opt::template none<kjson_impl<Opt>>(error::INDEX_OUT_OF_BOUNDS);
		return Opt::some(kjson_impl<Opt> { b, &v->a.data[i] });
	}

	opt_t<detail::arr_itr<Opt>> begin() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<detail::arr_itr<Opt>>(error::NOT_A_LIST);
		return Opt::some(detail::arr_itr<Opt> { b, &v->a.data[0] });
	}

	opt_t<detail::arr_itr<Opt>> end() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<detail::arr_itr<Opt>>(error::NOT_A_LIST);
		return Opt::some(detail::arr_itr<Opt> { b, &v->a.data[v->a.n] });
	}

	friend auto begin(const kjson_impl &a) { return a.begin(); }
	friend auto end  (const kjson_impl &a) { return a.end(); }

	opt_t<std::string_view> get_string() const
	{
		if (v->type != KJSON_VALUE_STRING)
			return Opt::template none<std::string_view>(error::NOT_A_STRING);
		return Opt::some(std::string_view { v->s.begin, v->s.len });
	}

	opt_t<std::string_view> get_number_rep() const
	{
		if (v->type != NUMERIC)
			return Opt::template none<std::string_view>(error::NOT_A_NUMBER);
		return Opt::some(std::string_view { v->s.begin, v->s.len });
	}

	opt_t<bool> get_bool() const
	{
		if (v->type != KJSON_VALUE_BOOLEAN)
			return Opt::template none<bool>(error::NOT_A_BOOLEAN);
		return Opt::some(v->b);
	}

	opt_t<std::nullptr_t> get_null() const
	{
		if (v->type != KJSON_VALUE_NULL)
			return Opt::template none<std::nullptr_t>(error::NOT_NULL);
		return Opt::some(nullptr);
	}

	friend opt_t<bool> operator==(const kjson_impl &a,
	                              const std::string_view &b)
	{
		return Opt::fmap([&b](auto sv){ return sv == b; },
		                 a.get_string());
	}

	friend opt_t<bool> operator==(const std::string_view &a,
	                              const kjson_impl &b)
	{
		return b == a;
	}

	friend std::ostream & operator<<(std::ostream &os, const kjson_impl &b)
	{
		char *buffer = NULL;
		size_t size = 0;
		::FILE *f = open_memstream(&buffer, &size);
		kjson_value_print(f, b.v);
		size_t n = ftell(f);
		fclose(f);
		os.write(buffer, n);
		free(buffer);
		return os;
	}

	template <typename T>
	std::enable_if_t<requests_string<T>::value,opt_t<T>> get() const
	{
		return Opt::fmap([](auto x){ return T(x); }, get_string());
	}

	template <typename T>
	std::enable_if_t<requests_number<T>::value,opt_t<T>> get() const
	{
		return Opt::bind(get_number_rep(), [](auto x){
			using std::from_chars;
			T r;
			const char *end = x.data() + x.length();
			if (auto [p,ec] = from_chars(x.data(), end, r);
			    ec != std::errc() || p != end)
				return Opt::template none<T>(error::PARSE_NUMBER);
			return Opt::some(std::move(r));
		});
	}
};

namespace detail {
template <typename Opt>
class arr_itr : kjson_impl<Opt> {

	using kjson_impl<Opt>::kjson_impl;
	friend class kjson_impl<Opt>;

public:
	arr_itr & operator++()
	{
		++this->v;
		return *this;
	}
	arr_itr & operator--()
	{
		--this->v;
		return *this;
	}

	friend ptrdiff_t operator-(const arr_itr &a, const arr_itr &b)
	{
		return a.v - b.v;
	}

	const kjson_impl<Opt> & operator*() const { return *this; }
	const kjson_impl<Opt> * operator->() const { return this; }

	friend bool operator==(const arr_itr &a, const arr_itr &b)
	{
		return a.b == b.b && a.v == b.v;
	}

	friend bool operator!=(const arr_itr &a, const arr_itr &b)
	{
		return !(a == b);
	}
};
}

namespace detail {

/* 2 monads, based on std::optional and throw */

template <template <typename> typename O>
struct opt_ctor {

	template <typename R> using type = O<R>;
	template <typename R> O<R> static none(error) { return {}; }
	template <typename R> O<R> static some(const R &v) { return { v }; }

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T &&>> bind(O<T> &&a, F &&f)
	{
		if (a)
			return std::forward<F>(f)(std::move(*a));
		else
			return none<std::invoke_result_t<F,T &&>>({});
	}

	template <typename F, typename T>
	static type<std::invoke_result_t<F,const T &>> bind(const O<T> &a, F &&f)
	{
		if (a)
			return std::forward<F>(f)(*a);
		else
			return none<std::invoke_result_t<F,const T &>>({});
	}

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T &&>> fmap(F &&f, O<T> &&x)
	{
		if (x)
			return some(std::forward<F>(f)(std::move(*x)));
		else
			return none<std::invoke_result_t<F,T &&>>({});
	}
};

struct opt_throw {

	struct exception : std::runtime_error {

		error code;

		exception(error code)
		: std::runtime_error(error_messages[static_cast<int>(code)])
		, code(code)
		{}
	};

	template <typename R> using type = R;
	template <typename R> static R    none(error err) { throw exception(err); }
	template <typename R> static auto some(R &&v) { return std::forward<R>(v); }

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T>> bind(T &&a, F &&f)
	{
		return std::forward<F>(f)(std::forward<T>(a));
	}

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T>> fmap(F &&f, T &&x)
	{
		return std::forward<F>(f)(std::forward<T>(x));
	}
};

} // end ns detail

/* "not optional": error code is ignored; however errors are quite
 * straight-forward to diagnose. Ideally, we'd use something like
 *   template <typename T> class either<degenerate<error,0>,T>
 * where degenerate<V,x> "steals" the single representation x from the
 * underlying type of V (as in ksmt::degenerate) and either<L,R> is a sum type
 * with two constructors: left(L) and right(R), ideally specialized when L is
 * some form of degenerate<V,x> in order to make use of this representation.
 *
 * However, this goes too far for kjson. :) */
typedef kjson_impl<detail::opt_ctor<std::optional>> json_opt;

typedef kjson_impl<detail::opt_throw> json;

}

namespace std {
template <typename Opt>
struct iterator_traits<kjson::detail::arr_itr<Opt>>
: iterator_traits<const ::kjson_value *> {};
}

#endif
