#include <assert.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

// #include <vector>
// #include <map>
// #include <stack>
// #include <list>
// #include <functional>

#include "net.h"

#if defined(__APPLE__)
#define SOCK_NONBLOCK 1
#endif

#include <unistd.h>

void wait(int us) { usleep(us); }

bool expect_message(net::Socket & sock, const std::string & want)
{
	static char DATA[100];
	net::Address addr;
	int len = sock.Receive(addr, DATA, 100);
	DATA[len] = '\0';

	if (len > 0) {
		return want.compare(std::string(DATA, len)) == 0;
	}

	return false;
}

void usage() {
    fprintf(stderr, "Usage: ./jana-server clients [options]\n");
    fprintf(stderr, "       ./jana-server -h or --help\n");
    exit(1);
}

int main(int argc, char const *argv[])
{
	net::Socket s;
	int N = 0;

	if (argc < 2) {
		usage();
	}

	if (std::string("-h").compare(argv[1]) == 0 ||
		std::string("--help").compare(argv[1]) == 0) usage();

	if (!sscanf(argv[1], "%d", &N)) {
		fprintf(stderr, "Invalid number of clients %s\n", argv[1]);
		exit(1);
	}

	if (!s.Open(3000, SOCK_NONBLOCK)) {
		fprintf(stderr, "Failed to create or bind socket\n");
		exit(1);
	}

	std::vector<net::Address> clients;
	clients.reserve(N);
	for (;;) {
		net::Address client_addr;

		fprintf(stderr, "// INIT PHASE\n");
		{
			fprintf(stderr, "> registering clients [%d/%d]", clients.size(), N);
			while (clients.size() < N) {
				char hello_world[100];

				int len = s.Receive(client_addr, hello_world, 100);
				hello_world[len] = '\0';

				if (len > 0) {
					if (std::string("HELLO").compare(std::string(hello_world, len)) == 0) {
						bool known = std::find(clients.begin(), clients.end(), client_addr) != clients.end();
						if (known) {
							continue;
						}

						clients.push_back(client_addr);
						s.Send(client_addr, "HELLO", 5);
						fprintf(stderr, "\r> registering clients [%d/%d]", clients.size(), N);
						continue;
					}
				}
			}
			fprintf(stderr, "\n");
		}

		fprintf(stderr, "// WAIT PHASE\n");
		{
			int late = 0;
			fprintf(stderr, "> consuming late HELLO ... [%d]", late);
			while (expect_message(s, "HELLO")) {
				fprintf(stderr, "\r> consuming late HELLO ... [%d]", ++late);
			}
			fprintf(stderr, "\n", late);
		}

		wait(1000*1000);
		for (auto & client : clients) {
			bool ok = s.Send(client, "START", 5);
			fprintf(stderr, "> sending START to %d.%d.%d.%d:%d %s\n",
						client.GetA(), client.GetB(),
						client.GetC(), client.GetD(),
						client.GetPort(),
						ok ? "OK" : "FAIL");
		}

		fprintf(stderr, "// WORK PHASE\n");
		{
			using namespace std::chrono;
			auto start = high_resolution_clock::now();
			duration<double> time_span;
			char hello_world[100];

			do {
				unsigned int data;
				int len = s.Receive(client_addr, &data, 4);
				if (len == 4) {
					unsigned int pkt = ntohl(data);
					// fprintf(stderr, "> got %d from %d.%d.%d.%d:%d\n", pkt,
					// 	client_addr.GetA(), client_addr.GetB(),
					// 	client_addr.GetC(), client_addr.GetD(),
					// 	client_addr.GetPort());
				}

				auto now = high_resolution_clock::now();
				time_span = duration_cast<duration<double>>(now - start);
			} while (time_span.count() < 30);
		}

		fprintf(stderr, "// DUMP PHASE\n");
		char hello_world[100];
		int ignored_pkts = 0;
		while (s.Receive(client_addr, hello_world, 100) > 0) {
			ignored_pkts++;
			wait(100*1000);
		}
		fprintf(stderr, "> consumed %d extra packets\n", ignored_pkts);
		clients.clear();
	}

	return 0;
}