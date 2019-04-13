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

#include <unistd.h>

#if defined(__APPLE__)
#define SOCK_NONBLOCK 1
#endif

#include "net.h"

void wait(int us) { usleep(us); }

bool expect_message(net::Socket & sock, const std::string & want)
{
	static char DATA[100];
	net::Address server;
	int len = sock.receive(server, DATA, 100);
	DATA[len] = '\0';

	if (len > 0) {
		if (want.compare(std::string(DATA, len)) == 0)
			return true;
		printf("> expected %s but got %s\n", want.c_str(), DATA);
	}

	return false;
}

bool parse_addr(char const *arg, net::Address & addr)
{
	int a, b, c, d;

	if (!sscanf(arg, "%d.%d.%d.%d", &a, &b, &c, &d))
		return false;

	addr = net::Address(a, b, c, d, 3000);
	return true;
}

void usage() {
    fprintf(stderr, "Usage: ./jana-cli [options]\n");
    fprintf(stderr, "       ./jana-cli -h or --help\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "       ./jana-cli -s 192.168.1.5\n");
    exit(1);
}

// client.exe server.ip.addr.x
int main(int argc, char const *argv[])
{
	static const char *hello_world = "Hello World!";

	net::Address server_addr(127, 0, 0, 1, 3000);

	/* Parse options */
	if (argc > 1) {

		if (std::string("-h").compare(argv[1]) == 0 ||
			std::string("--help").compare(argv[1]) == 0) usage();

		if (std::string("-s").compare(argv[1]) == 0 ||
			std::string("--server").compare(argv[1]) == 0) {

			if (!parse_addr(argv[2], server_addr)) {
				fprintf(stderr, "Malformed ip address: %s\n", argv[2]);
				exit(1);
			} else {
			}
		}
	}

	net::Socket heartbeat;
	if (!heartbeat.open(0, SOCK_NONBLOCK)) {
		fprintf(stderr, "Failed to create or bind heartbeat socket\n");
		exit(1);
	}

	net::Socket client;
	if (!client.open(0)) {
		fprintf(stderr, "Failed to create or bind client socket\n");
		exit(1);
	}

	std::vector<unsigned long> channel_access_delay;
	std::vector<bool> packet_transmit_status;
	channel_access_delay.reserve(100000);
	packet_transmit_status.reserve(100000);

	for (;;) {
		/* Setup phase */
		printf("// INIT PHASE\n");
		{
			using namespace std::chrono;
			unsigned int ctr = 0;
			while (!expect_message(heartbeat, "HELLO")) {
				auto now = system_clock::now();
				auto dtn = now.time_since_epoch();
				auto sec = dtn.count() * system_clock::period::num / system_clock::period::den;
				if (sec % 5 == 0) {
					heartbeat.send(server_addr, "HELLO", 5);
					fprintf(stderr, "\r> sending HELLO to %d.%d.%d.%d:%d [%u]",
						server_addr.a(), server_addr.b(),
						server_addr.c(), server_addr.d(),
						server_addr.port(),
						++ctr);
				}
				wait(1000*1000);
			}
			if (ctr > 0)
				fprintf(stderr, "\n");
		}

		/* Ready phase - wait for go signal */
		printf("// WAIT PHASE\n");
		{
			using namespace std::chrono;
			auto start = system_clock::now();
			auto time_span = duration_cast<duration<double>>(start - start);

			while (!expect_message(heartbeat, "START") &&
					time_span.count() < 10) {
				wait(100*1000);

				auto now = system_clock::now();
				time_span = duration_cast<duration<double>>(now - start);
			}

			if (time_span.count() >= 10) {
				fprintf(stderr, "> did not receive START within 10 s, resetting\n");
				continue;
			}
		}

		/* Main phase - pump it! */
		printf("// WORK PHASE\n");
		{
			using namespace std::chrono;
			auto start = steady_clock::now();
			duration<double> time_span;

			unsigned int pkt = 0;

			do {
				auto data = htonl(pkt);

				auto t1 = steady_clock::now();
				bool transmitted = client.send(server_addr, &data, 4);
				auto t2 = steady_clock::now();
				auto delay = duration_cast<nanoseconds>(t2 - t1).count();

				// std::cout << delay << " ns" << std::endl;

				channel_access_delay.push_back(delay);
				packet_transmit_status.push_back(transmitted);

				pkt += 1;

				auto now = steady_clock::now();
				time_span = duration_cast<duration<double>>(now - start);
			} while (time_span.count() < 30);
		};

		/* Cleanup phase - print data */
		printf("// DUMP PHASE\n");

		const auto [min_delay, max_delay] = std::minmax_element(channel_access_delay.begin(), channel_access_delay.end());
		auto good = std::count(packet_transmit_status.begin(), packet_transmit_status.end(), true);
		auto fail = packet_transmit_status.size() - good;
		auto bw = (4 * packet_transmit_status.size())/30.0f;
		printf("channel access delay min=%lu us, max=%lu us\n", (*min_delay)/1000, (*max_delay)/1000);
		printf("transmission status good=%lu, fail=%lu\n", good, fail);
		printf("estimated bandwidth %f\n", bw);

		channel_access_delay.clear();
		packet_transmit_status.clear();

		wait(10*1000*1000); /// 10s
	}

	return 0;
}