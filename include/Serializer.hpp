#pragma once
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <tuple>
using namespace std;

//字节流类，继承vector<char>，便于按字节提取数据
class StreamBuffer : public vector<char>
{
public:

	StreamBuffer(){ m_curpos = 0; }
	StreamBuffer(const char* in, size_t len){
		m_curpos = 0;
		insert(begin(), in, in+len);
	}
    ~StreamBuffer(){ }

	void reset(){ m_curpos = 0; }
	const char* data(){ return &(*this)[0]; }
	const char* current(){ return&(*this)[m_curpos]; }
	void offset(int k){ m_curpos += k; }
	bool is_eof(){ return (m_curpos >= size()); }              
	void input( char* in, size_t len){ insert(end(), in, in+len); }
	int findc(char c){
		iterator itr = find(begin()+m_curpos, end(), c);		
		if (itr != end())
		{
			return itr - (begin()+m_curpos);
		}
		return -1;
	}

private:
	unsigned int m_curpos ;// 当前字节流位置
};

//序列化类，按照特定字节流顺序组织结构，根据输入参数的类型，将其转换为char *结构
class Serializer
{
public:
	//默认使用小端模式
    Serializer() { m_byteorder = LittleEndian; }
    ~Serializer(){ }

	Serializer(StreamBuffer dev, int byteorder=LittleEndian){
		m_byteorder = byteorder;
		m_iodevice = dev;
	}

public:
	enum ByteOrder {
		BigEndian,
		LittleEndian
	};

public:
	void reset(){
		m_iodevice.reset();
	}
	int size(){
		return m_iodevice.size();
	}
	void skip_raw_date(int k){
		m_iodevice.offset(k);
	}
	const char* data(){
		return m_iodevice.data();
	}
	void byte_orser(char* in, int len){
		if (m_byteorder == BigEndian){
			reverse(in, in+len);
		}
	}
	void write_raw_data(char* in, int len){
		m_iodevice.input(in, len);
		m_iodevice.offset(len);
	}
	const char* current(){
		return m_iodevice.current();
	}
	void clear(){
		m_iodevice.clear();
		reset();
	}

	template<typename T>
	void output_type(T& t);

	template<typename T>
	void input_type(T t);

	// 直接给一个长度， 返回当前位置以后x个字节数据
	void get_length_mem(char* p, int len){
		memcpy(p, m_iodevice.current(), len);
		m_iodevice.offset(len);
	}

public:
	template<typename Tuple, std::size_t Id>
	void getv(Serializer& ds, Tuple& t) {
		ds >> std::get<Id>(t);
	}

	template<typename Tuple, std::size_t... I>
	Tuple get_tuple(std::index_sequence<I...>) {
		Tuple t;
		initializer_list<int>{((getv<Tuple, I>(*this, t)), 0)...};
		return t;
	}

	template<typename T>
	Serializer &operator >> (T& i){
		output_type(i); 
		return *this;
	}

	template<typename T>
	Serializer &operator << (T i){
		input_type(i);
		return *this;
	}

private:
	int  m_byteorder;
	StreamBuffer m_iodevice;
};

template<typename T>
inline void Serializer::output_type(T& t)
{
	int len = sizeof(T);
	char* d = new char[len];
	if (!m_iodevice.is_eof()){
		memcpy(d, m_iodevice.current(), len);
		m_iodevice.offset(len);
		byte_orser(d, len);
		t = *reinterpret_cast<T*>(&d[0]);
	}
	delete [] d;
}

template<>
inline void Serializer::output_type(std::string& in)
{
	int marklen = sizeof(uint16_t);
	char* d = new char[marklen];
	memcpy(d, m_iodevice.current(), marklen);
	byte_orser(d, marklen);
	int len = *reinterpret_cast<uint16_t*>(&d[0]);
	m_iodevice.offset(marklen);
	delete [] d;
	if (len == 0) return;
	in.insert(in.begin(), m_iodevice.current(), m_iodevice.current() + len);
	m_iodevice.offset(len);
}

template<typename T>
inline void Serializer::input_type(T t)
{
	int len = sizeof(T);      //获取变量类型长度
	char* d = new char[len];
	const char* p = reinterpret_cast<const char*>(&t);  //转换为字符串
	memcpy(d, p, len);        
	byte_orser(d, len);
	m_iodevice.input(d, len); //将变量存入streambuffer
	delete [] d;
}

template<>
inline void Serializer::input_type(std::string in)
{
	// 先存入字符串长度
	uint16_t len = in.size();
	char* p = reinterpret_cast< char*>(&len); //将uint16_t字符串长度转化为char* 字符串
	byte_orser(p, sizeof(uint16_t));          //如果是大端就翻转
	m_iodevice.input(p, sizeof(uint16_t));    //存入streambuffer

	// 存入字符串
	if (len == 0) return;
	char* d = new char[len];
	memcpy(d, in.c_str(), len);
	m_iodevice.input(d, len);
	delete [] d;
}

template<>
inline void Serializer::input_type(const char* in)
{
	input_type<std::string>(std::string(in));
}
