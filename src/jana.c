#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* poor man's platform selector */
#define PLATFORM_WIN  1
#define PLATFORM_MAC  2
#define PLATFORM_UNIX 3

#if defined(_WIN32)
#define PLATFORM PLATFORM_WIN
#elif defined(__APPLE__)
#define PLATFORM PLATFORM_MAC
#else
#define PLATFORM PLATFORM_UNIX
#endif

#if PLATFORM == PLATFORM_WIN
	#include <winsock2.h>
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib")

	void usleep(uint32_t us)
	{
		Sleep((int)(us/1000));
	}

	typedef int socklen_t;
	#define INET_ADDRSTRLEN  (16)

	#ifndef _CRT_NO_TIME_T
	struct timespec
	{
	time_t tv_sec; // Seconds - >= 0
	long tv_nsec; // Nanoseconds - [0, 999999999]
	};
	#endif

#else

	#include <unistd.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <time.h>

#endif

void usage() {
    fprintf(stderr, "Usage: jana [-s clients|-c host] [options]\n");
    fprintf(stderr, "       jana [-h|--help]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Server or Client:\n");
    fprintf(stderr, "  -p, --port     port to listen on/connect to\n");
    fprintf(stderr, "  -f, --file     name of statistics/data logfile\n");
    fprintf(stderr, "Client specific:\n");
    fprintf(stderr, "  -r, --rate [D] packet transmission rate distribution\n");
    fprintf(stderr, "  -d, --data [D] packet data size distribution (in bytes)\n");
    fprintf(stderr, "Server specific:\n");
    fprintf(stderr, "  -s, --server   run in server mode\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  jana -s -p 3333\n");
    fprintf(stderr, "  jana -c 192.168.1.5 -p 3333\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[D] indicates options that support a notation for");
    fprintf(stderr, " expressing distribution functions:\n");
    fprintf(stderr, "  exp(a)         F(x) = 1 - e^{ax}\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Built " __DATE__ " " __TIME__ "\n");
    exit(1);
}

uint32_t chash(struct sockaddr_in *sa)
{
	return sa->sin_addr.s_addr ^ (sa->sin_port * 59);
}

enum jana_mode { jana_decide, jana_client, jana_server };

struct config
{
	enum     jana_mode 		mode;
	struct   sockaddr_in 	addr;

	uint32_t n_clients;
	bool 	 keepalive;

	const char *logfile;
};

struct xclock
{
#if PLATFORM == PLATFORM_WIN
	double freq;
	int64_t ctr;
#else
	struct timespec tp;
#endif
};

void xclock_mark(struct xclock *c)
{
#if PLATFORM == PLATFORM_WIN
	LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    c->ctr = li.QuadPart;
#else
	clock_gettime(CLOCK_MONOTONIC, &c->tp);
#endif
}

void xclock_init(struct xclock *c)
{
#if PLATFORM == PLATFORM_WIN
	LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li)) {
    	fprintf(stderr, "xclock_init: QueryPerformanceFrequency error\n");
    	exit(1);
    }
    c->freq = (double)li.QuadPart;
#endif
    xclock_mark(c);
}



uint64_t xclock_us(struct xclock *c)
{
#if PLATFORM == PLATFORM_WIN
	LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	return (uint64_t)(li.QuadPart - c->ctr) * 1000000.0 / c->freq;
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)(now.tv_sec - c->tp.tv_sec) * 1000000 +
		(now.tv_nsec - c->tp.tv_nsec) / 1000;
#endif
}

double xclock_elapsed(struct xclock *c)
{
#if PLATFORM == PLATFORM_WIN
	LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	return (double)(li.QuadPart - c->ctr) / c->freq;
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - c->tp.tv_sec) +
		   (double)(now.tv_nsec - c->tp.tv_nsec) / 1000000000;
#endif
}

int init_socket(struct sockaddr_in *addr, bool nonblock)
{
	int sockfd;

#ifdef SOCK_NONBLOCK
	int flags = nonblock ? SOCK_NONBLOCK : 0;
	sockfd = socket(AF_INET, SOCK_DGRAM | flags, IPPROTO_UDP);
#else
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif

	if (sockfd <= 0) {
		perror("init_socket: failed to create socket");
		exit(1);
	}

	if (bind(sockfd, (const struct sockaddr*)addr, sizeof(struct sockaddr_in)) < 0) {
		perror("init_socket: failed to bind socket");
#if PLATFORM == PLATFORM_WIN
		closesocket(sockfd);
#else
		close(sockfd);
#endif
		exit(1);
	}

#ifndef SOCK_NONBLOCK
	if (nonblock) {
	#if PLATFORM == PLATFORM_WIN
		DWORD nonBlocking = 1;
		if (ioctlsocket(sockfd, FIONBIO, &nonBlocking) != NO_ERROR) {
			perror("ioctlsocket failed");
#if PLATFORM == PLATFORM_WIN
			closesocket(sockfd);
#else
			close(sockfd);
#endif
		}
	#else
		fcntl(sockfd, F_SETFL, O_NONBLOCK);
	#endif
	}
#endif

	return sockfd;
}

bool read_message(int sockfd, const char *want, struct sockaddr_in *from)
{
	static char storage[100];
	struct sockaddr addr;
	socklen_t fromlen = sizeof addr;

	int len = recvfrom(sockfd, storage, 100, 0, &addr, &fromlen);

	if (len <= 0) {
		return false;
	}

	if (from != 0)
		*from = *((struct sockaddr_in*)&addr);

	storage[len] = '\0';
	return strcmp(storage, want) == 0;
}

bool cmpaddr(struct sockaddr_in *a, struct sockaddr_in *b)
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr &&
		   a->sin_port == b->sin_port;
}

/**
 *  Very naive implementation of a minimal perfect hash function,
 *
 * 	g(x) = (kx mod p) mod n
 *
 * where p is a large prime, n is set size and k the magic factor.
 */
uint32_t compute_mph_k(struct sockaddr_in *clients, uint32_t N)
{
	uint32_t *hashes = calloc(N, sizeof(uint32_t));
	uint32_t *table  = calloc(N, sizeof(uint32_t));

	if (hashes == 0 || table == 0) {
		fprintf(stderr, "compute_mph_k: out-of-memory\n");
		exit(-1);
	}

	for (uint32_t i = 0; i < N; ++i) {
		hashes[i] = chash(clients + i);
	}
	for (uint32_t k = 1; k != 0; ++k) {
		bool found = true;

		for (uint32_t i = 0; i < N; ++i) {
			uint32_t f = (k * hashes[i] % 479001599) % N;

			if (table[f] == 0) {
				table[f] = hashes[i];
			} else {
				found = false;
				break;
			}
		}

		if (found) {
			free(hashes);
			free(table);
			return k;
		}
		memset(table, 0, N * sizeof(uint32_t));
	}

	fprintf(stderr, "compute_mph_k: not possible\n");
	free(hashes);
	free(table);
	exit(1);
}

const char * SPINNER[] = { "/", "-", "\\", "|", "/", "-", "\\", "|" };

void run_client(struct config *cfg)
{
	struct sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = INADDR_ANY;
	local_addr.sin_port = 0;

	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(cfg->addr.sin_addr), str, INET_ADDRSTRLEN);
	printf("> using %s:%d\n", str, ntohs(cfg->addr.sin_port));

	int heartfd = init_socket(&local_addr, true);
	int sockfd = init_socket(&local_addr, false);

init_phase:
	{
		uint32_t i = 0;
		do {
			fprintf(stderr, "\r> registering ");
			fprintf(stderr, "%s", SPINNER[i % 8]);

			if (i++ % 50 == 0) {
				static const char *MSG = "HELLO";
				sendto(heartfd, MSG, strlen(MSG)+1, 0, (struct sockaddr*)&cfg->addr, sizeof(struct sockaddr_in));
			}

			usleep(120*1000);
		} while (!read_message(heartfd, "HELLO", 0));
		fprintf(stderr, "\r> registered on server\n");
	}

	{
		uint32_t i = 0;
		do {
			if (i > 150) {
				fprintf(stderr, "\r> timeout after 18 s\n");
				goto init_phase;
			}

			fprintf(stderr, "\r> waiting to start ");
			fprintf(stderr, "%s", SPINNER[i++ % 8]);

			usleep(120*1000);
		} while (!read_message(heartfd, "START", 0));
		fprintf(stderr, "\r> got the start signal\n");
	}

	{
		struct xclock tp_start;
		struct xclock tp_now;

		uint32_t packet = 0;

		xclock_init(&tp_start);
		fprintf(stderr, "\r> running test ");
		do {
			struct sockaddr addr;
			socklen_t fromlen = sizeof addr;

			uint32_t data = htonl(++packet);

			xclock_mark(&tp_now);
			sendto(sockfd, &data, sizeof(uint32_t), 0,
				(struct sockaddr*)(&cfg->addr),
				sizeof(struct sockaddr_in));
			uint64_t us = xclock_us(&tp_now);
		} while (xclock_elapsed(&tp_start) < 10);
		fprintf(stderr, "\r> network test is done\n");
	}

	{
		uint32_t i = 0;
		fprintf(stderr, "> %s (%u B)\n", cfg->logfile, 3);
	}

	if (cfg->keepalive)
		goto init_phase;

cleanup:
#if PLATFORM == PLATFORM_WIN
	closesocket(sockfd);
	closesocket(heartfd);
#else
	close(heartfd);
	close(sockfd);
#endif
}

void run_server(struct config *cfg)
{
	int sockfd = init_socket(&cfg->addr, true);
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(cfg->addr.sin_addr), ip, INET_ADDRSTRLEN);
	printf("> using %s:%d\n", ip, ntohs(cfg->addr.sin_port));

	uint32_t           registered;
	uint32_t           mphk;

	struct sockaddr_in *clients;
	uint32_t           *counters;

	clients  = calloc(cfg->n_clients, sizeof(struct sockaddr_in));
	counters = calloc(cfg->n_clients, sizeof(uint32_t));

init_phase:
	{
		struct sockaddr_in client;
		static const char *MSG = "HELLO";
		uint32_t i = 0;

		registered = 0;
		while (registered < cfg->n_clients) {
			fprintf(stderr, "\r> registering ");
			fprintf(stderr, "%s [%u/%u]", SPINNER[i++ % 8], registered, cfg->n_clients);

			if (read_message(sockfd, "HELLO", &client)) {
				inet_ntop(AF_INET, &(client.sin_addr), ip, INET_ADDRSTRLEN);

				fprintf(stderr, "\r> HELLO from %s:%d\n",
					ip, ntohs(client.sin_port));
				sendto(sockfd, MSG, strlen(MSG)+1, 0, (struct sockaddr*)&client, sizeof(struct sockaddr_in));

				uint32_t idx;
				for (idx = 0; idx < registered &&
					!cmpaddr(clients + idx, &client);
					++idx);

				if (idx == registered) {
					clients[registered++] = client;
				}
			}

			usleep(100*1000);
		}
	}

	usleep(500*1000);
	fprintf(stderr, "> computing perfect hash");
	mphk = compute_mph_k(clients, registered);
	fprintf(stderr, "\r> g(x) = (%u * x mod %u) mod %u\n", mphk, 479001599, registered);

	{
		static const char *MSG = "START";
		for (uint32_t i = 0; i < registered; ++i) {
			sendto(sockfd, MSG, strlen(MSG)+1, 0, (struct sockaddr*)(clients + i), sizeof(struct sockaddr_in));
		}
	}

	{
		struct xclock   tp_start;
		uint32_t        packet;
		struct sockaddr addr;
		socklen_t       fromlen = sizeof addr;

		xclock_init(&tp_start);
		fprintf(stderr, "> running network test");
		do {
			int len = recvfrom(sockfd, &packet, sizeof(packet), 0, &addr, &fromlen);
			if (len > 0) {
				uint32_t x = chash((struct sockaddr_in *)&addr);
				uint32_t i = (mphk * x % 479001599) % registered;
				counters[i] = ntohl(packet);
			}
		} while (xclock_elapsed(&tp_start) < 15);
		fprintf(stderr, "\r> network test completed\n");
	}

	usleep(500*1000);

	{
		for (int i = 0; i < registered; ++i) {
			struct sockaddr_in *addr = clients + i;
			uint32_t x = (mphk * chash(addr) % 479001599) % registered;
			inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
			printf("> [%d/%u] %s:%u %u\n", i, x,
				ip, ntohs(addr->sin_port), counters[x]);
		}
	}

	{
		static uint8_t  data[100];
		struct sockaddr addr;
		int             len;
		socklen_t       fromlen = sizeof addr;
		uint32_t        consumed = 0;
		do {
			len = recvfrom(sockfd, &data, 100, 0, &addr, &fromlen);
			if (len > 0) {
				consumed++;
			}
		} while (len > 0);
		printf("> consumed %u late packets\n", consumed);
	}

	if (cfg->keepalive) {
		memset(clients, 0, registered * sizeof(struct sockaddr_in));
		memset(counters, 0, registered * sizeof(uint32_t));
		goto init_phase;
	}

cleanup:
	free(counters);
	free(clients);
#if PLATFORM == PLATFORM_WIN
	closesocket(sockfd);
#else
	close(sockfd);
#endif
}

void guard(const char *program, bool ensure, const char *msg)
{
	if (!ensure) {
		fprintf(stderr, "%s: parameter error - %s\n", program, msg);
		exit(1);
	}
}

// client.exe server.ip.addr.x
int main(int argc, char const *argv[])
{
	#if PLATFORM == PLATFORM_WIN
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2, 2), &WsaData) != NO_ERROR) {
		fprintf(stderr, "error starting WSAStartup\n");
		exit(1);
	}
	#endif

	static const char *DEFAULT_LOGFILE = "logdata.csv";

	struct config cfg;
	memset(&cfg, 0, sizeof cfg);

	cfg.logfile = DEFAULT_LOGFILE;
	cfg.addr.sin_family = AF_INET;
	cfg.addr.sin_port = htons(3000);

	/* Parse options */
	if (argc > 1) {

		if (strcmp("-h", argv[1]) == 0 ||
			strcmp("--help", argv[1]) == 0) usage();

		int j = 1;
		if (strcmp("-s", argv[j]) == 0 ||
			strcmp("--server", argv[j]) == 0) {
			cfg.mode = jana_server;
			cfg.keepalive = true;
			cfg.addr.sin_addr.s_addr = INADDR_ANY;

			guard(argv[0], (j = j + 1) < argc, "must specify number of clients");
			if (!sscanf(argv[j], "%u", &cfg.n_clients)) {
				fprintf(stderr, "invalid number: %s\n", argv[j]);
				exit(1);
			}
		} else if (strcmp("-c", argv[j]) == 0 ||
			strcmp("--client", argv[j]) == 0) {
			cfg.mode = jana_client;
			cfg.keepalive = false;

			guard(argv[0], (j = j + 1) < argc, "must specify host");

			if (inet_pton(AF_INET, argv[j], &(cfg.addr.sin_addr)) <= 0) {
				fprintf(stderr, "invalid ipv4 address: %s\n", argv[j]);
				exit(1);
			}
			// cfg.addr.sin_addr.s_addr = htonl(cfg.addr.sin_addr.s_addr);
		} else {
			fprintf(stderr, "%s: must either be a client (-c) or server (-s)\n", argv[0]);
			fprintf(stderr, "\n");
			usage();
		}

		while (++j < argc) {
			if (strcmp("-p", argv[j]) == 0 ||
				strcmp("--port", argv[j]) == 0) {

				guard(argv[0], (j = j + 1) < argc, "must specify port");
				uint16_t port;
				if (!sscanf(argv[j], "%hu", &port)) {
					fprintf(stderr, "invalid port: %s\n", argv[j]);
					exit(1);
				}
				cfg.addr.sin_port = htons(port);
			} else {
				fprintf(stderr, "unknown option %s\n", argv[j]);
				exit(1);
			}
		};
	}

	if (cfg.mode == jana_decide) {
		fprintf(stderr, "%s: must either be a client (-c) or server (-s)\n", argv[0]);
		fprintf(stderr, "\n");
		usage();
	}

	if (cfg.mode == jana_client) {
		run_client(&cfg);
	}

	if (cfg.mode == jana_server) {
		run_server(&cfg);
	}

	#if PLATFORM == PLATFORM_WIN
	WSACleanup();
	#endif

	return 0;
}