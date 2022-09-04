#pragma once
#include <string>
#include <map>
#include <string>
#include <sstream>
#include <functional>
#include <memory>
#include "zmq.hpp"
#include "Serializer.hpp"


class Serializer;

template<typename T>
struct type_xx { typedef T type; };

template<>
struct type_xx<void> { typedef int8_t type; };

// 打包帮助模板
template<typename Tuple, std::size_t... Is>
void package_params_impl(Serializer& ds, const Tuple& t, std::index_sequence<Is...>)
{
	initializer_list<int>{((ds << std::get<Is>(t)), 0)...};
	//((ds << std::get<Is>(t)), ...);
}

template<typename... Args>
void package_params(Serializer& ds, const std::tuple<Args...>& t)
{
	package_params_impl(ds, t, std::index_sequence_for<Args...>{});
}

// 用tuple做参数调用函数模板类
template<typename Function, typename Tuple, std::size_t... Index>
decltype(auto) invoke_impl(Function&& func, Tuple&& t, std::index_sequence<Index...>)
{
	return func(std::get<Index>(std::forward<Tuple>(t))...);
}

template<typename Function, typename Tuple>
decltype(auto) invoke(Function&& func, Tuple&& t)
{
	constexpr auto size = std::tuple_size<typename std::decay<Tuple>::type>::value;
	return invoke_impl(std::forward<Function>(func), std::forward<Tuple>(t), std::make_index_sequence<size>{});
}

// 调用帮助类，主要用于返回是否void的情况
template<typename R, typename F, typename ArgsTuple>
typename std::enable_if<std::is_same<R, void>::value, typename type_xx<R>::type >::type
call_helper(F f, ArgsTuple args) {
	invoke(f, args);
	return 0;
}

template<typename R, typename F, typename ArgsTuple>
typename std::enable_if<!std::is_same<R, void>::value, typename type_xx<R>::type >::type
call_helper(F f, ArgsTuple args) {
	return invoke(f, args);
}

// rpc 类定义
class rxwRPC
{
public:
	enum rpc_role {
		RPC_CLIENT,
		RPC_SERVER
	};
	enum rpc_err_code {
		RPC_ERR_SUCCESS = 0,
		RPC_ERR_FUNCTIION_NOT_BIND,
		RPC_ERR_RECV_TIMEOUT
	};

	// result 打包
	template<typename T>
	class value_t {
	public:
		typedef typename type_xx<T>::type type;
		typedef std::string msg_type;
		typedef uint16_t code_type;

		value_t() { code_ = 0; msg_.clear(); }
		bool valid() { return (code_ == 0 ? true : false); }
		int error_code() { return code_; }
		std::string error_msg() { return msg_; }
		type val() { return val_; }

		void set_val(const type& val) { val_ = val; }
		void set_code(code_type code) { code_ = code; }
		void set_msg(msg_type msg) { msg_ = msg; }

		friend Serializer& operator >> (Serializer& in, value_t<T>& d) {
			in >> d.code_ >> d.msg_;
			if (d.code_ == 0) {
				in >> d.val_;
			}
			return in;
		}
		friend Serializer& operator << (Serializer& out, value_t<T> d) {
			out << d.code_ << d.msg_ << d.val_;
			return out;
		}
	private:
		code_type code_;
		msg_type msg_;
		type val_;
	};

	rxwRPC();
	~rxwRPC();

	// network
	void as_client(std::string ip, int port);
	void as_server(int port);
	void send(zmq::message_t& data);
	void recv(zmq::message_t& data);
	void set_timeout(uint32_t ms);
	void run();

public:
	// server
	template<typename F>
	void bind(std::string name, F func); //函数名、函数指针

	template<typename F, typename S>
	void bind(std::string name, F func, S* s); //函数名、函数指针、从属类

	// client
	template<typename R, typename... Params>
	value_t<R> call(std::string name, Params... ps) {

		using args_type = std::tuple<typename std::decay<Params>::type...>; //通过元组获取参数类型
		args_type args = std::make_tuple(ps...); //生成对应参数类型的元组

		Serializer ds;
		ds << name;
		package_params(ds, args); //遍历tuple获取参数并存入streambuffer
		return net_call<R>(ds);
	}

	template<typename R>
	value_t<R> call(std::string name) {
		Serializer ds;
		ds << name;
		return net_call<R>(ds);
	}

private:
	Serializer* call_(std::string name, const char* data, int len);

	template<typename R>
	value_t<R> net_call(Serializer& ds);

	template<typename F>
	void callproxy(F fun, Serializer* pr, const char* data, int len);

	template<typename F, typename S>
	void callproxy(F fun, S* s, Serializer* pr, const char* data, int len);

	// 函数指针
	template<typename R, typename... Params>
	void callproxy_(R(*func)(Params...), Serializer* pr, const char* data, int len) {
		callproxy_(std::function<R(Params...)>(func), pr, data, len);
	}

	// 类成员函数指针
	template<typename R, typename C, typename S, typename... Params>
	void callproxy_(R(C::* func)(Params...), S* s, Serializer* pr, const char* data, int len) {

		using args_type = std::tuple<typename std::decay<Params>::type...>;

		Serializer ds(StreamBuffer(data, len));
		constexpr auto N = std::tuple_size<typename std::decay<args_type>::type>::value;
		args_type args = ds.get_tuple < args_type >(std::make_index_sequence<N>{});

		auto ff = [=](Params... ps)->R {
			return (s->*func)(ps...);
		};
		typename type_xx<R>::type r = call_helper<R>(ff, args);

		value_t<R> val;
		val.set_code(RPC_ERR_SUCCESS);
		val.set_val(r);
		(*pr) << val;
	}

	// functional
	template<typename R, typename... Params>
	void callproxy_(std::function<R(Params... ps)> func, Serializer* pr, const char* data, int len) {

		//元组保存参数类型
		using args_type = std::tuple<typename std::decay<Params>::type...>;

		Serializer ds(StreamBuffer(data, len));
		constexpr auto N = std::tuple_size<typename std::decay<args_type>::type>::value; //通过元组获取参数应分配大小
		args_type args = ds.get_tuple < args_type >(std::make_index_sequence<N>{});

		typename type_xx<R>::type r = call_helper<R>(func, args); //获取函数返回值类型

		value_t<R> val;
		val.set_code(RPC_ERR_SUCCESS);
		val.set_val(r);
		(*pr) << val;
	}

private:
	//保存注册的函数
	std::map<std::string, std::function<void(Serializer*, const char*, int)>> m_handlers;

	zmq::context_t m_context;
	std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>> m_socket;

	rpc_err_code m_error_code;

	int m_role;
};

inline rxwRPC::rxwRPC() : m_context(1) {
	m_error_code = RPC_ERR_SUCCESS;
}

inline rxwRPC::~rxwRPC() {
	m_context.close();
}

// network
inline void rxwRPC::as_client(std::string ip, int port)
{
	m_role = RPC_CLIENT;
	m_socket = std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>>(new zmq::socket_t(m_context, ZMQ_REQ), [](zmq::socket_t* sock) { sock->close(); delete sock; sock = nullptr; });
	ostringstream os;
	os << "tcp://" << ip << ":" << port;
	m_socket->connect(os.str());
}

inline void rxwRPC::as_server(int port)
{
	m_role = RPC_SERVER;
	m_socket = std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>>(new zmq::socket_t(m_context, ZMQ_REP), [](zmq::socket_t* sock) { sock->close(); delete sock; sock = nullptr; });
	ostringstream os;
	os << "tcp://*:" << port;
	m_socket->bind(os.str());
}

inline void rxwRPC::send(zmq::message_t& data)
{
	m_socket->send(data);
}

inline void rxwRPC::recv(zmq::message_t& data)
{
	m_socket->recv(&data);
}

inline void rxwRPC::set_timeout(uint32_t ms)
{
	// only client can set
	if (m_role == RPC_CLIENT) {
		m_socket->setsockopt(ZMQ_RCVTIMEO, ms);
	}
}

inline void rxwRPC::run()
{
	// only server can call
	if (m_role != RPC_SERVER) {
		return;
	}
	while (1) {
		zmq::message_t data;
		recv(data);
		StreamBuffer iodev((char*)data.data(), data.size());
		Serializer ds(iodev);

		std::string funname;
		ds >> funname;
		Serializer* r = call_(funname, ds.current(), ds.size() - funname.size());

		zmq::message_t retmsg(r->size());
		memcpy(retmsg.data(), r->data(), r->size());
		int size = retmsg.size();
		send(retmsg);
		delete r;
	}
}

// 处理函数相关

inline Serializer* rxwRPC::call_(std::string name, const char* data, int len)
{
	Serializer* ds = new Serializer();
	if (m_handlers.find(name) == m_handlers.end()) {
		(*ds) << value_t<int>::code_type(RPC_ERR_FUNCTIION_NOT_BIND);
		(*ds) << value_t<int>::msg_type("function not bind: " + name);
		return ds;
	}
	auto fun = m_handlers[name];
	fun(ds, data, len);
	ds->reset();
	return ds;
}

//注册并绑定函数
template<typename F>
inline void rxwRPC::bind(std::string name, F func)
{
	m_handlers[name] = std::bind(&rxwRPC::callproxy<F>, this, func, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

//注册并绑定函数
template<typename F, typename S>
inline void rxwRPC::bind(std::string name, F func, S* s)
{
	m_handlers[name] = std::bind(&rxwRPC::callproxy<F, S>, this, func, s, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

template<typename F>
inline void rxwRPC::callproxy(F fun, Serializer* pr, const char* data, int len)
{
	callproxy_(fun, pr, data, len);
}

template<typename F, typename S>
inline void rxwRPC::callproxy(F fun, S* s, Serializer* pr, const char* data, int len)
{
	callproxy_(fun, s, pr, data, len);
}

template<typename R>
inline rxwRPC::value_t<R> rxwRPC::net_call(Serializer& ds)
{
	//生成并发送请求
	zmq::message_t request(ds.size() + 1);
	memcpy(request.data(), ds.data(), ds.size());
	if (m_error_code != RPC_ERR_RECV_TIMEOUT) {
		send(request);
	}
	
	//生成并接受返回
	zmq::message_t reply;
	recv(reply);
	value_t<R> res;
	if (reply.size() == 0) {
		// timeout
		m_error_code = RPC_ERR_RECV_TIMEOUT;
		res.set_code(RPC_ERR_RECV_TIMEOUT);
		res.set_msg("recv timeout");
		return res;
	}
	m_error_code = RPC_ERR_SUCCESS;
	ds.clear();
	ds.write_raw_data((char*)reply.data(), reply.size());
	ds.reset();

	ds >> res;
	return res;
}
