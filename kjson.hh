
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
	char * data();
};

template <> char * base<char *>::data() { return str; }
template <> char * base<std::string>::data() { return str.data(); }

template <typename Opt> class arr_itr;

}

template <typename T> struct requests_string : std::false_type {};
template <> struct requests_string<std::string> : std::true_type {};
template <> struct requests_string<std::string_view> : std::true_type {};

template <typename T> struct requests_number : std::false_type {};

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
			return Opt::template none<kjson_impl<Opt>>();
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
			return Opt::template none<size_t>();
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
			return Opt::template none<bool>();
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
			return Opt::template none<std::vector<kjson_impl<Opt>>>();
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
			return Opt::template none<kjson_impl<Opt>>();
		::kjson_value *found = nullptr;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin, sv.length())) {
				if (found)
					return Opt::template none<kjson_impl<Opt>>();
				found = &v->o.data[i].value;
			}
		if (found)
			return Opt::some(kjson_impl<Opt> { b, found });
		return Opt::template none<kjson_impl<Opt>>();
	}

	opt_t<size_t> size() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<size_t>();
		return Opt::some(v->a.n);
	}

	opt_t<kjson_impl<Opt>> operator[](size_t i) const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<kjson_impl<Opt>>();
		if (v->a.n <= i)
			return Opt::template none<kjson_impl<Opt>>();
		return Opt::some(kjson_impl<Opt> { b, &v->a.data[i] });
	}

	opt_t<detail::arr_itr<Opt>> begin() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<detail::arr_itr<Opt>>();
		return Opt::some(detail::arr_itr<Opt> { b, &v->a.data[0] });
	}

	opt_t<detail::arr_itr<Opt>> end() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return Opt::template none<detail::arr_itr<Opt>>();
		return Opt::some(detail::arr_itr<Opt> { b, &v->a.data[v->a.n] });
	}

	friend auto begin(const kjson_impl &a) { return a.begin(); }
	friend auto end  (const kjson_impl &a) { return a.end(); }

	opt_t<std::string_view> get_string() const
	{
		if (v->type != KJSON_VALUE_STRING)
			return Opt::template none<std::string_view>();
		return Opt::some(std::string_view { v->s.begin, v->s.len });
	}

	opt_t<std::string_view> get_number_rep() const
	{
		if (v->type != NUMERIC)
			return Opt::template none<std::string_view>();
		return Opt::some(std::string_view { v->s.begin, v->s.len });
	}

	opt_t<bool> get_bool() const
	{
		if (v->type != KJSON_VALUE_BOOLEAN)
			return Opt::template none<bool>();
		return Opt::some(v->b);
	}

	opt_t<std::nullptr_t> get_null() const
	{
		if (v->type != KJSON_VALUE_NULL)
			return Opt::template none<std::nullptr_t>();
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
				return Opt::template none<T>();
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
	template <typename R> O<R> static none() { return {}; }
	template <typename R> O<R> static some(const R &v) { return { v }; }

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T &&>> bind(O<T> &&a, F &&f)
	{
		if (a)
			return std::forward<F>(f)(std::move(*a));
		else
			return none<std::invoke_result_t<F,T &&>>();
	}

	template <typename F, typename T>
	static type<std::invoke_result_t<F,const T &>> bind(const O<T> &a, F &&f)
	{
		if (a)
			return std::forward<F>(f)(*a);
		else
			return none<std::invoke_result_t<F,const T &>>();
	}

	template <typename F, typename T>
	static type<std::invoke_result_t<F,T &&>> fmap(F &&f, O<T> &&x)
	{
		if (x)
			return some(std::forward<F>(f)(std::move(*x)));
		else
			return none<std::invoke_result_t<F,T &&>>();
	}
};

struct opt_throw {

	template <typename R> using type = R;
	template <typename R> static R    none() { throw std::bad_optional_access {}; }
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

typedef kjson_impl<detail::opt_ctor<std::optional>> json_opt;
typedef kjson_impl<detail::opt_throw> json;

}

namespace std {
template <typename Opt>
struct iterator_traits<kjson::detail::arr_itr<Opt>>
: iterator_traits<const ::kjson_value *> {};
}

#endif
