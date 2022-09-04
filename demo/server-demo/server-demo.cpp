#include <string>
#include <iostream>
#include "rxwRPC.hpp"


void foo_1() {
	std::cout << "foo_1 is called" << std::endl;
}

string foo_2() {
	return "foo_1 is called";
}

int foo_3(int arg1) {
	std::cout << "foo_3 is called" << std::endl;
	return arg1 * arg1;
}

int foo_4(int arg1, std::string arg2, int arg3, float arg4) {
	std::cout << "foo_4 is called" << std::endl;
	std::cout << arg1 << std::endl;
	std::cout << arg2 << std::endl;
	return arg1 * arg3;
}

class ClassMem
{
public:
	int bar(int arg1, std::string arg2, int arg3) {
		return arg1 * arg3;
	}
};


int main()
{
	rxwRPC server;
	server.as_server(5555);

	server.bind("foo_1", foo_1);
	server.bind("foo_3", std::function<int(int)>(foo_3));
	server.bind("foo_4", foo_4);

	ClassMem s;
	server.bind("foo_6", &ClassMem::bar, &s);

	std::cout << "run rpc server on: " << 5555 << std::endl;
	server.run();

	return 0;
}
