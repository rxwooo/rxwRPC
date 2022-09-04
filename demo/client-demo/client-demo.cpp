#include <string>
#include <iostream>
#include <ctime>
#include "../../include/rxwRPC.hpp"
#include <Windows.h>

#define buttont_assert(exp) { \
	if (!(exp)) {\
		std::cout << "ERROR: "; \
		std::cout << "function: " << __FUNCTION__  << ", line: " <<  __LINE__ << std::endl; \
		system("pause"); \
	}\
}\

int main()
{
	rxwRPC client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(2000);

	int callcnt = 0;
	while (1) {
		std::cout << "current call count: " << ++callcnt << std::endl;

		client.call<void>("foo_1");

		int foo3r = client.call<int>("foo_3", 10).val();
		buttont_assert(foo3r == 100);

		int foo4r = client.call<int>("foo_4", 10, "rxwRPC", 100, (float)10.8).val();
		buttont_assert(foo4r == 1000);

		int foo6r = client.call<int>("foo_6", 10, "rxwRPC", 100).val();
		buttont_assert(foo6r == 1000);

		Sleep(1000);	
	}

	return 0;
}
