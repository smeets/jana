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
	int len = sock.Receive(server, DATA, 100);
	DATA[len] = '\0';

	if (len > 0) {
		return want.compare(std::string(DATA, len)) == 0;
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
	if (!heartbeat.Open(0, SOCK_NONBLOCK)) {
		fprintf(stderr, "Failed to create or bind heartbeat socket\n");
		exit(1);
	}

	net::Socket client;
	if (!client.Open(0)) {
		fprintf(stderr, "Failed to create or bind client socket\n");
		exit(1);
	}

	std::vector<double> channel_access_delay;
	std::vector<bool> packet_transmit_status;
	channel_access_delay.reserve(100000);
	packet_transmit_status.reserve(100000);
	for (;;) {
		/* Setup phase */
		printf("// INIT PHASE\n");
		heartbeat.Send(server_addr, "HELLO", 5);
		while (!expect_message(heartbeat, "HELLO")) {
			{
				using namespace std::chrono;
				auto now = system_clock::now();
				auto dtn = now.time_since_epoch();
				auto sec = dtn.count() * system_clock::period::num / system_clock::period::den;
				if (sec % 5 == 0) {
					bool ok = heartbeat.Send(server_addr, "HELLO", 5);
					printf("> sending HELLO to %d.%d.%d.%d:%d [%s]\n",
						server_addr.GetA(), server_addr.GetB(),
						server_addr.GetC(), server_addr.GetD(), 
						server_addr.GetPort(), ok ? "ok" : "??");
				}
			}
			wait(1000*1000);
		}

		/* Ready phase - wait for go signal */
		printf("// WAIT PHASE\n");
		while (!expect_message(heartbeat, "START")) {
			wait(100*1000);
		}

		/* Main phase - pump it! */
		printf("// WORK PHASE\n");
		{
			using namespace std::chrono;
			auto start = high_resolution_clock::now();
			duration<double> time_span;

			unsigned int pkt = 0;

			do {
				auto data = htonl(pkt);

				auto t1 = high_resolution_clock::now();
				bool transmitted = client.Send(server_addr, &data, 4);
				auto t2 = high_resolution_clock::now();
				auto cal_d = duration_cast<duration<double>>(t2-t1);

				channel_access_delay.emplace_back(cal_d.count());
				packet_transmit_status.emplace_back(transmitted);

				wait(1000);
				pkt += 1;

				auto now = high_resolution_clock::now();
				time_span = duration_cast<duration<double>>(now - start);
			} while (time_span.count() < 30);
		};

		/* Cleanup phase - print data */
		printf("// DUMP PHASE\n");

		auto [cal_min, cal_max] = std::minmax_element(channel_access_delay.begin(), channel_access_delay.end());
		auto good = std::count(packet_transmit_status.begin(), packet_transmit_status.end(), true);
		auto fail = packet_transmit_status.size() - good;
		auto bw = (4 * packet_transmit_status.size())/30.0f;

		printf("CAD: max=%lf, min=%lf\n", cal_max, cal_min);
		printf("PTS: good=%d, fail=%d\n", good, fail);
		printf("BW: %f\n", bw);

		channel_access_delay.clear();
		packet_transmit_status.clear();

		wait(10*1000*1000); /// 10s
	}

	return 0;
}