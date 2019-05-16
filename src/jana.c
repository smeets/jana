#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#define MAX_PACKETS (5000000)

#include <linux/net_tstamp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MAX_PKT_SIZE (64100)


float rand1() { return ((float)rand())/((float)(RAND_MAX)+1); }

typedef union pdf_cfg_s
{
	struct uniform
	{
		float n;
		float k;
	} uniform;

	struct exp
	{
		float n;
	} exp;

	struct weibull
	{
		float a;
		float b;
	} weibull;
} pdf_cfg_t;

typedef float (*pdf_rv_fn)(pdf_cfg_t*);

float uniform_rvs(pdf_cfg_t *cfg)
{
	return cfg->uniform.n * rand1() + cfg->uniform.k;
}

float exp_rvs(pdf_cfg_t *cfg)
{
	float x = rand1();
	return -cfg->exp.n * log(1 - x);
}
// x = −(1/β)*ln(α∏i=1(Ui))
float weibull_rvs(pdf_cfg_t *cfg)
{
	float x = rand1();
	float i = cfg->weibull.a - 1;
	while (i-- >= 1) {
		x *= rand1();
	}
	return -(1/cfg->weibull.b) * log(x);
}

void usage() {
    fprintf(stderr, "Usage: jana [-s clients|-c host] [options]\n");
    fprintf(stderr, "       jana [-h|--help]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -s X           run in server mode with X clients\n");
    fprintf(stderr, "  -c host        run as client, connect to host (ip addr)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Server or Client:\n");
    fprintf(stderr, "  -p, --port     port to listen on/connect to\n");
    fprintf(stderr, "  -f, --file     name of statistics/data logfile\n");
    fprintf(stderr, "Client specific:\n");
    fprintf(stderr, "  -r, --rate [D] packet transmission rate distribution [us]\n");
    fprintf(stderr, "  -d, --data [D] packet data size distribution [bytes]\n");
    fprintf(stderr, "Server specific:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  jana -s -p 3333\n");
    fprintf(stderr, "  jana -c 192.168.1.5 -p 3333\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[D] indicates options that support a notation for");
    fprintf(stderr, " expressing distribution functions:\n");
    fprintf(stderr, "  exp            -y*ln(1 - rand())\n");
    fprintf(stderr, "  weibull        a*pow(-ln(rand()), 1/b)\n");
    fprintf(stderr, "  uniform        n*rand() + k\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  [fn] [k1=v1,k2=v2,...]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  -r weibull a=33,b=55\n");
    fprintf(stderr, "  -r weibull a=33,b=55 -d uniform n=0,k=100\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Built " __DATE__ " " __TIME__ "\n");
    exit(1);
}

/**
 * hash a client's ip and port: ADDR xor (PORT * 59)
 *
 * 01234567 01234567 01234567 01234567 ADDR
 * 00000000 00000000 01234567 01234567 PORT
 *
 * XOR is decent but there are better options.
 * 59 seems to be a good prime number for this case.
 */
uint32_t chash(struct sockaddr_in *sa)
{
	uint32_t addr = sa->sin_addr.s_addr;
	return addr;
	/* promote port to 32 bits */
	//uint32_t port = sa->sin_port;
	// return addr ^ (port * 59);
}

enum jana_mode { jana_decide, jana_client, jana_server };

struct config
{
	enum     jana_mode 		mode;
	struct   sockaddr_in 	addr;

	pdf_rv_fn				wait_rv;
	pdf_rv_fn				data_rv;

	pdf_cfg_t 				wait_pdf;
	pdf_cfg_t 				data_pdf;

	uint32_t n_clients;
	bool 	 keepalive;

	const char *logfile;
};

uint64_t clock_elapsed_us(clockid_t clk, struct timespec *c)
{
	struct timespec now;
	clock_gettime(clk, &now);
	if (c != NULL) {
		now.tv_sec -= c->tv_sec;
		now.tv_nsec -= c->tv_nsec;
	}
	return (uint64_t)(now.tv_sec) * 1000000 + (now.tv_nsec) / 1000;
}

double clock_elapsed_sec(struct timespec *c)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (c != NULL)
		now.tv_sec -= c->tv_sec;
	return (double)(now.tv_sec);
}


/**
 * poor man's socket wrapper
 */
int init_socket(struct sockaddr_in *addr, bool nonblock)
{
	int sockfd;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sockfd <= 0) {
		perror("init_socket: failed to create socket");
		exit(1);
	}

	if (bind(sockfd, (const struct sockaddr*)addr, sizeof(struct sockaddr_in)) < 0) {
		perror("init_socket: failed to bind socket");
		close(sockfd);
		exit(1);
	}

	if (nonblock) {
		fcntl(sockfd, F_SETFL, O_NONBLOCK);
	}

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
	// TODO: use bitset instead to avoid allocation
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

const char * SPINNER[] = { "/", "-", "\\", "|" };

void run_client(struct config *cfg)
{
	static uint8_t zero_bytes[MAX_PKT_SIZE] = {0};

	struct sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = INADDR_ANY;
	local_addr.sin_port = 0;

	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(cfg->addr.sin_addr), str, INET_ADDRSTRLEN);
	printf("> using %s:%d\n", str, ntohs(cfg->addr.sin_port));

	int heartfd = init_socket(&local_addr, true);
	int sockfd = init_socket(&local_addr, false);

	uint32_t packet_id;
	uint64_t *packet_ttime;
	uint64_t *packet_delay;
	uint32_t *wait_rvs;
	uint32_t *data_rvs;

	packet_delay = calloc(MAX_PACKETS, sizeof(uint64_t));
	packet_ttime = calloc(MAX_PACKETS, sizeof(uint64_t));

	wait_rvs = calloc(MAX_PACKETS, sizeof(uint32_t));
	data_rvs = calloc(MAX_PACKETS, sizeof(uint32_t));

	if (cfg->wait_rv != NULL) {
		printf("> generating delay distribution...");
		for (size_t i = 0; i < MAX_PACKETS; ++i)
			wait_rvs[i] = cfg->wait_rv(&cfg->wait_pdf);
		printf("\r> generated packet delay distribution\n");
	}

	if (cfg->data_rv != NULL) {
		printf("> generating data distribution...");
		for (size_t i = 0; i < MAX_PACKETS; ++i)
			data_rvs[i] = cfg->data_rv(&cfg->data_pdf);
		printf("\r> generated packet payload distribution\n");
	}

init_phase:
	{
		uint32_t i = 0;
		do {
			fprintf(stderr, "\r> registering ");
			fprintf(stderr, "%s", SPINNER[i % 4]);

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
			fprintf(stderr, "%s", SPINNER[i++ % 4]);

			usleep(120*1000);
		} while (!read_message(heartfd, "READY", 0));
		fprintf(stderr, "\r> got the ready signal\n");
	}

	{
		static const char *MSG = "SETGO";
		sendto(sockfd, MSG, strlen(MSG)+1, 0,
					(struct sockaddr*)(&cfg->addr),
					sizeof(struct sockaddr_in));
	}

	{
		struct timespec tp_start;
		struct timespec tp_now;

		packet_id = 0;

		clock_gettime(CLOCK_MONOTONIC, &tp_start);
		fprintf(stderr, "\r> running test ...");
		do {
			uint32_t *data = (uint32_t*)zero_bytes;
			*data = htonl(packet_id);

			usleep(wait_rvs[packet_id]);

			uint64_t t = clock_elapsed_us(CLOCK_REALTIME, NULL);
			clock_gettime(CLOCK_MONOTONIC, &tp_now);

			sendto(sockfd,
				zero_bytes,
				sizeof(uint32_t) + data_rvs[packet_id],
				0, (struct sockaddr*)(&cfg->addr), sizeof(struct sockaddr_in));

			uint64_t us = clock_elapsed_us(CLOCK_MONOTONIC, &tp_now);

			assert (packet_id < MAX_PACKETS);
			packet_ttime[packet_id] = t;
			packet_delay[packet_id] = us;
			++packet_id;
		} while (clock_elapsed_sec(&tp_start) < 10);
		fprintf(stderr, "\r> network test is done (%u packets sent)\n", packet_id);
	}

	{
		fprintf(stderr, "> %s ...", cfg->logfile);
		FILE *logfd = fopen(cfg->logfile, "w");
		fprintf(logfd, "packet,time,sendto_us\n");
		for (uint64_t i = 0; i < packet_id; i++) {
			fprintf(logfd, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i, packet_ttime[i], packet_delay[i]);
		}
		fprintf(stderr, "\r> %s...DONE\n", cfg->logfile);
	}

	if (cfg->keepalive)
		goto init_phase;

	close(sockfd);
	close(heartfd);
	free(packet_delay);
	free(packet_ttime);
}

void run_server(struct config *cfg)
{
	static char ip[INET_ADDRSTRLEN];
	static uint8_t zero_bytes[MAX_PKT_SIZE] = {0};

	int sockfd = init_socket(&cfg->addr, true);
	inet_ntop(AF_INET, &(cfg->addr.sin_addr), ip, INET_ADDRSTRLEN);
	printf("> using %s:%d\n", ip, ntohs(cfg->addr.sin_port));

	uint32_t           registered;
	uint64_t           mphk;

	struct sockaddr_in *clients;
	uint32_t           *counters;
	uint64_t           *recvdata;

	clients  = calloc(cfg->n_clients, sizeof(struct sockaddr_in));
	counters = calloc(cfg->n_clients, sizeof(uint32_t));
	recvdata = calloc(cfg->n_clients, sizeof(uint64_t));

	if (clients == NULL || counters == NULL || recvdata == NULL) {
		perror("run_server: failed to allocate buffers");
		exit(1);
	}

init_phase:
	{
		struct sockaddr_in client;
		static const char *MSG = "HELLO";
		uint32_t i = 0;

		registered = 0;
		while (registered < cfg->n_clients) {
			fprintf(stderr, "\r> registering ");
			fprintf(stderr, "%s [%u/%u]", SPINNER[i++ % 4], registered, cfg->n_clients);

			if (read_message(sockfd, "HELLO", &client)) {
				inet_ntop(AF_INET, &(client.sin_addr), ip, INET_ADDRSTRLEN);

				fprintf(stderr, "\r> HELLO from %s\n", ip);
				sendto(sockfd, MSG, strlen(MSG)+1, 0, (struct sockaddr*)&client, sizeof(struct sockaddr_in));

				uint32_t idx;
				for (idx = 0; idx < registered &&
					!cmpaddr(clients + idx, &client);
					++idx);

				if (idx == registered) {
					counters[registered] = 0;
					recvdata[registered] = 0;
					clients[registered++] = client;
				}
			}

			usleep(100*1000);
		}
	}

	usleep(500*1000);
	{
		fprintf(stderr, "> computing perfect hash");
		mphk = compute_mph_k(clients, registered);
		fprintf(stderr, "\r> g(x) = (%" PRIu64 "* x mod %u) mod %u\n", mphk, 479001599, registered);
		for (int i = 0; i < registered; ++i) {
			struct sockaddr_in *addr = clients + i;
			uint64_t f = (mphk * chash(addr) % 479001599) % registered;
			inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
			printf("> [%d/%u (%" PRIu64 ")] %s:%u %u\n", i+1, registered, f,
				ip, ntohs(addr->sin_port), counters[f]);
		}
	}

	{
		static const char *MSG = "READY";
		for (uint32_t i = 0; i < registered; ++i) {
			sendto(sockfd, MSG, strlen(MSG)+1, 0, (struct sockaddr*)(clients + i), sizeof(struct sockaddr_in));
		}
	}

	{
		struct timespec tp_start;
		struct sockaddr addr;
		socklen_t       fromlen = sizeof addr;

		clock_gettime(CLOCK_MONOTONIC, &tp_start);
		fprintf(stderr, "> running network test");
		do {
			int len = recvfrom(sockfd, zero_bytes, MAX_PKT_SIZE, 0, &addr, &fromlen);
			if (len > 0) {
				uint64_t x = chash((struct sockaddr_in *)&addr);
				uint64_t f = (mphk * x % 479001599) % registered;
				counters[f] = counters[f] + 1; //ntohl(packet);
				recvdata[f] = recvdata[f] + len;
			}
		} while (clock_elapsed_sec(&tp_start) < 15);
		fprintf(stderr, "\r> network test completed\n");
	}

	usleep(500*1000);

	{
		for (int i = 0; i < registered; ++i) {
			struct sockaddr_in *addr = clients + i;
			uint64_t f = (mphk * chash(addr) % 479001599) % registered;
			inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
			printf("> [%d/%u (%" PRIu64 ")] %s:%u %u pkts (%" PRIu64 " B)\n", i+1, registered, f,
				ip, ntohs(addr->sin_port), counters[f], recvdata[f]);
		}
	}

	{
		static char     data[100];
		struct sockaddr addr;
		int             len;
		socklen_t       fromlen = sizeof addr;
		uint32_t        consumed = 0;
		do {
			len = recvfrom(sockfd, data, 100, 0, &addr, &fromlen);
			if (len > 0) {
				consumed++;
			}
		} while (len > 0);
		printf("> consumed %u late packets\n", consumed);
	}

	if (cfg->keepalive) {
		memset(clients, 0, registered * sizeof(struct sockaddr_in));
		memset(counters, 0, registered * sizeof(uint32_t));
		memset(recvdata, 0, registered * sizeof(uint32_t));
		goto init_phase;
	}

	free(counters);
	free(clients);
	close(sockfd);
}

void guard(const char *program, bool ensure, const char *msg)
{
	if (!ensure) {
		fprintf(stderr, "%s: parameter error - %s\n", program, msg);
		exit(1);
	}
}

bool parse_pdf(const char *pdf_arg, const char *cfg_arg, pdf_rv_fn *rv, pdf_cfg_t *cfg)
{
	if (strcmp("exp", pdf_arg) == 0) {
		*rv = &exp_rvs;
		if (!sscanf(cfg_arg, "n=%f", &cfg->exp.n))
			return false;
		return true;
	}

	if (strcmp("uniform", pdf_arg) == 0) {
		*rv = &uniform_rvs;

		if (!sscanf(cfg_arg, "n=%f,k=%f", &cfg->uniform.n, &cfg->uniform.k))
			return false;

		return true;
	}

	if (strcmp("weibull", pdf_arg) == 0) {
		*rv = &exp_rvs;
		if (!sscanf(cfg_arg, "a=%f,b=%f", &cfg->weibull.a, &cfg->weibull.b))
			return false;
		return true;
	}

	return false;
}

// client.exe server.ip.addr.x
int main(int argc, char const *argv[])
{
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
			} else if (strcmp("-f", argv[j]) == 0 ||
				strcmp("--file", argv[j]) == 0) {

				guard(argv[0], (j = j + 1) < argc, "must specify path");
				cfg.logfile = argv[j];
			} else if (strcmp("-r", argv[j]) == 0 ||
				strcmp("--rate", argv[j]) == 0) {

				guard(argv[0], (j = j + 1) + 1 < argc, "must specify distribution and cfg");
				if (!parse_pdf(argv[j], argv[j+1], &cfg.wait_rv, &cfg.wait_pdf)) {
					fprintf(stderr, "unknown distribution %s or config %s\n", argv[j], argv[j+1]);
					exit(1);
				}
				j = j + 1;
			} else if (strcmp("-d", argv[j]) == 0 ||
				strcmp("--data", argv[j]) == 0) {

				guard(argv[0], (j = j + 1) + 1 < argc, "must specify distribution and cfg");
				if (!parse_pdf(argv[j], argv[j+1], &cfg.data_rv, &cfg.data_pdf)) {
					fprintf(stderr, "unknown distribution %s or config %s\n", argv[j], argv[j+1]);
					exit(1);
				}
				j = j + 1;
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

	srand(time(NULL));

	if (cfg.mode == jana_client) {
		run_client(&cfg);
	}

	if (cfg.mode == jana_server) {
		run_server(&cfg);
	}

	return 0;
}
