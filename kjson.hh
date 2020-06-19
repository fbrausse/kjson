
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

template <typename Opt> struct base0 : ::kjson_value {
	Opt opt;
	base0(Opt opt) : ::kjson_value KJSON_VALUE_INIT, opt(opt) {}
	~base0() { kjson_value_fini(this); }
};

template <typename T> struct base1 {
	T str;
	char * data();
};

template <typename T, typename Opt>
struct base : base0<Opt>, base1<T> {
	base(T str, Opt opt)
	: base0<Opt>(std::move(opt))
	, base1<T> { std::move(str) }
	{}
};

template <> char * base1<char *>::data() { return str; }
template <> char * base1<std::string>::data() { return str.data(); }

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
		std::shared_ptr<detail::base<T,Opt>> ptr
	) {
		::kjson_parser p { ptr->data() };
		::kjson_value *v = ptr.get();
		if (!kjson_parse2(&p, v, read_other, store_leaf))
			return ptr->opt.template none<kjson_impl<Opt>>();
		kjson_impl<Opt> r { std::move(ptr), v };
		return r.b->opt.some(std::move(r));
	}

protected:
	std::shared_ptr<detail::base0<Opt>> b;
	const ::kjson_value *v;

	kjson_impl(std::shared_ptr<detail::base0<Opt>> b,
	           const kjson_value *v)
	: b(std::move(b))
	, v(v)
	{}

public:
	static opt_t<kjson_impl<Opt>> parse(char *s, Opt opt = {})
	{
		return parse(std::make_shared<detail::base<char *,Opt>>(s, opt));
	}

	static opt_t<kjson_impl<Opt>> parse(std::string s, Opt opt = {})
	{
		return parse(std::make_shared<detail::base<std::string,Opt>>(std::move(s), opt));
	}

	static opt_t<kjson_impl<Opt>> parse(std::istream &i, Opt opt = {})
	{
		std::stringstream ss;
		ss << i.rdbuf();
		return parse(ss.str(), opt);
	}

	static opt_t<kjson_impl<Opt>> parse(std::istream &&i, Opt opt = {})
	{
		std::stringstream ss;
		ss << i.rdbuf();
		return parse(ss.str(), opt);
	}

	opt_t<size_t> count(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return b->opt.template none<size_t>();
		size_t r = 0;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin,
			            sv.length()))
				r++;
		return b->opt.some(r);
	}

	opt_t<bool> contains(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return b->opt.template none<bool>();
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin,
			            sv.length()))
				return b->opt.some(true);
		return b->opt.some(false);
	}

	opt_t<std::vector<kjson_impl<Opt>>> get(std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return b->opt.template none<std::vector<kjson_impl<Opt>>>();
		std::vector<kjson_impl<Opt>> r;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin, sv.length()))
				r.push_back({ b, &v->o.data[i].value });
		return b->opt.some(std::move(r));
	}

	opt_t<kjson_impl<Opt>>  operator[](std::string_view sv) const
	{
		if (v->type != KJSON_VALUE_OBJECT)
			return b->opt.template none<kjson_impl<Opt>>();
		::kjson_value *found = nullptr;
		for (size_t i=0; i < v->o.n; i++)
			if (sv.length() == v->o.data[i].key.len &&
			    !memcmp(sv.data(), v->o.data[i].key.begin, sv.length())) {
				if (found)
					return b->opt.template none<kjson_impl<Opt>>();
				found = &v->o.data[i].value;
			}
		if (found)
			return b->opt.some(kjson_impl<Opt> { b, found });
		return b->opt.template none<kjson_impl<Opt>>();
	}

	opt_t<size_t> size() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return b->opt.template none<size_t>();
		return b->opt.some(v->a.n);
	}

	opt_t<kjson_impl<Opt>> operator[](size_t i) const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return b->opt.template none<kjson_impl<Opt>>();
		if (v->a.n <= i)
			return b->opt.template none<kjson_impl<Opt>>();
		return b->opt.some(kjson_impl<Opt> { b, &v->a.data[i] });
	}

	opt_t<detail::arr_itr<Opt>> begin() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return b->opt.template none<detail::arr_itr<Opt>>();
		return b->opt.some(detail::arr_itr<Opt> { b, &v->a.data[0] });
	}

	opt_t<detail::arr_itr<Opt>> end() const
	{
		if (v->type != KJSON_VALUE_ARRAY)
			return b->opt.template none<detail::arr_itr<Opt>>();
		return b->opt.some(detail::arr_itr<Opt> { b, &v->a.data[v->a.n] });
	}

	friend auto begin(const kjson_impl &a) { return a.begin(); }
	friend auto end  (const kjson_impl &a) { return a.end(); }

	opt_t<std::string_view> get_string() const
	{
		if (v->type == KJSON_VALUE_STRING)
			return b->opt.some(std::string_view { v->s.begin, v->s.len });
		return b->opt.template none<std::string_view>();
	}

	opt_t<std::string_view> get_number_rep() const
	{
		if (v->type != NUMERIC)
			return b->opt.template none<std::string_view>();
		return b->opt.some(std::string_view { v->s.begin, v->s.len });
	}

	opt_t<bool> get_bool() const
	{
		if (v->type != KJSON_VALUE_BOOLEAN)
			return b->opt.template none<bool>();
		return b->opt.some(v->b);
	}

	opt_t<std::nullptr_t> get_null() const
	{
		if (v->type != KJSON_VALUE_NULL)
			return b->opt.template none<std::nullptr_t>();
		return b->opt.some(nullptr);
	}

	friend opt_t<bool> operator==(const kjson_impl &a,
	                              const std::string_view &b)
	{
		if (a.v->type != KJSON_VALUE_STRING)
			return a.b->opt.template none<bool>();
		return a.b->opt.some(std::string_view { a.v->s.begin, a.v->s.len } == b);
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
		if (v->type == KJSON_VALUE_STRING)
			return b->opt.some(T(std::string_view { v->s.begin, v->s.len }));
		return b->opt.template none<T>();
	}

	template <typename T>
	std::enable_if_t<requests_number<T>::value,opt_t<T>> get() const
	{
		if (v->type == NUMERIC) {
			using std::from_chars;
			T r;
			const char *end = v->s.begin + v->s.len;
			if (auto [p,ec] = from_chars(v->s.begin, end, r);
			    ec != std::errc() || p != end)
				return b->opt.template none<T>();
			return b->opt.some(std::move(r));
		}
		return b->opt.template none<T>();
	}
};

namespace detail {
template <typename Opt>
class arr_itr : kjson_impl<Opt> {

	using kjson_impl<Opt>::kjson_impl;
	friend class kjson_impl<Opt>;

public:
	arr_itr & operator++() { ++this->v; return *this; }
	arr_itr & operator--() { --this->v; return *this; }

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
template <template <typename> typename O>
struct opt_ctor {

	template <typename R> using type = O<R>;
	template <typename R> O<R> none() { return {}; }
	template <typename R> O<R> some(const R &v) { return { v }; }
};

struct opt_throw {

	template <typename R> using type = R;
	template <typename R> R    none() { throw std::bad_optional_access {}; }
	template <typename R> auto some(R &&v) { return std::forward<R>(v); }
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
