#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib")
	#define u64f "I64u"
#else
	#include <unistd.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <time.h>
#define u64f PRIu64
#endif

#define MAGIC_NUMBER      (0xa1b2c3d4)
#define MAGIC_NUMBER_SWAP (0xd4c3b2a1)

enum read_mode {
	read_mode_same,
	read_mode_swap,
	read_mode_shit
};

static const char *MODE_NAME[2] = {
	"identical",
	"swapped"
};

#define	SWAPLONG(y) \
    (((((u_int)(y))&0xff)<<24) | \
     ((((u_int)(y))&0xff00)<<8) | \
     ((((u_int)(y))&0xff0000)>>8) | \
     ((((u_int)(y))>>24)&0xff))

#define	SWAPSHORT(y) \
     ((u_short)(((((u_int)(y))&0xff)<<8) | \
((((u_int)(y))&0xff00)>>8)))

//
// PCAP FILE FORMAT
//
// GLOBAL HEADER (pcap_hdr_t)
// PACKET HEADER (pcaprec_hdr_t)
// PACKET DATA   (bytes)
// PACKET HEADER (pcaprec_hdr_t)
// PACKET DATA   (bytes)
// ...
//

typedef struct pcap_hdr_s {
        uint32_t magic_number;   /* magic number */
        uint16_t version_major;  /* major version number */
        uint16_t version_minor;  /* minor version number */
        int32_t  thiszone;       /* GMT to local correction */
        uint32_t sigfigs;        /* accuracy of timestamps */
        uint32_t snaplen;        /* max length of captured packets, in octets */
        uint32_t network;        /* data link type */
} pcap_hdr_t;

typedef struct pcaprec_hdr_s {
        uint32_t ts_sec;         /* timestamp seconds */
        uint32_t ts_usec;        /* timestamp microseconds */
        uint32_t incl_len;       /* number of octets of packet saved in file */
        uint32_t orig_len;       /* actual length of packet */
} pcaprec_hdr_t;

typedef struct ethernet_hdr_s {
	uint8_t ignored[14];
} ethernet_hdr_t;

typedef struct ip_hdr_s {
	uint8_t ver_len;
	uint8_t  services;
	uint16_t tot_len;
	uint16_t pkt_id;
	uint16_t flags;
	uint8_t  ttl;
	uint8_t  protocol;
	uint16_t checksum;
	uint32_t src_addr;           /* ip v4, network byte order */
	uint32_t dst_addr;           /* ip v4, network byte order */
} ip_hdr_t;

typedef struct udp_hdr_s {
	uint16_t src_port;           /* network byte order */
	uint16_t dst_port;           /* network byte order */
	uint16_t pkt_size;           /* network byte order */
	uint16_t checksum;			 /* network byte order */
} udp_hdr_t;

void usage() {
    fprintf(stderr, "Usage: pcap2csv file -c client -s server [options]\n");
    fprintf(stderr, "       pcap2csv [-h|--help]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  file           path to pcap-encoded (binary) file\n");
    fprintf(stderr, "  -c, --client   ipv4 address of client\n");
    fprintf(stderr, "  -s, --server   ipv4 address of server\n");
    fprintf(stderr, "  -d, --dump     dump pcap global header\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  pcap2csv capture.pcap 192.168.1.24 192.168.1.25\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Built " __DATE__ " " __TIME__ "\n");
    exit(1);
}

int main(int argc, char *argv[])
{
	char     *pcap_file;
	bool     dump_pcap_header = false;
	uint32_t self_addr = 0;
	uint32_t server_addr = 0;

	if (argc == 1 ||
		strcmp("-h", argv[1]) == 0 ||
		strcmp("--help", argv[1]) == 0) usage();

	pcap_file = argv[1];

	/* Parse options */
	{
		int j = 1;
		while (++j < argc) {
			if (strcmp("-s", argv[j]) == 0 ||
				strcmp("--server", argv[j]) == 0) {
				j++;
				if (inet_pton(AF_INET, argv[j], &server_addr) <= 0) {
					fprintf(stderr, "server: invalid ipv4 address: %s\n", argv[j]);
					exit(1);
				}
			} else if (strcmp("-c", argv[j]) == 0 ||
				strcmp("--client", argv[j]) == 0) {
				j++;
				if (inet_pton(AF_INET, argv[j], &self_addr) <= 0) {
					fprintf(stderr, "client: invalid ipv4 address: %s\n", argv[j]);
					exit(1);
				}
			} else if (strcmp("-d", argv[j]) == 0 ||
				strcmp("--dump", argv[j]) == 0) {
				dump_pcap_header = true;
			} else {
				fprintf(stderr, "unknown option %s\n", argv[j]);
				exit(1);
			}
		};
	}

	FILE *pcap = fopen(pcap_file, "rb");
	if (pcap == NULL) {
		perror("error reading file");
		exit(1);
	}

	pcap_hdr_t pcap_hdr;
	fread((void *)&pcap_hdr, 20, 1, pcap);

	enum read_mode mode = read_mode_shit;
	{
		if (pcap_hdr.magic_number == MAGIC_NUMBER)
			mode = read_mode_same;
		else if (pcap_hdr.magic_number == MAGIC_NUMBER_SWAP)
			mode = read_mode_swap;
		else {
			fprintf(stderr, "magic number is not valid\n");
			exit(1);
		}
	}

	if (dump_pcap_header) {
		fprintf(stderr, "magic_number : %#010x (%s)\n", pcap_hdr.magic_number, MODE_NAME[mode]);
		fprintf(stderr, "version_major: %u\n", pcap_hdr.version_major);
		fprintf(stderr, "version_minor: %u\n", pcap_hdr.version_minor);
	}

	if (mode != read_mode_same) {
		fprintf(stderr, "only identical read mode supported\n");
		exit(1);
	}

	if (server_addr == 0) {
		fprintf(stderr, "must specify server (-s)\n");
		exit(1);
	}

	if (self_addr == 0) {
		fprintf(stderr, "must specify client (-c)\n");
		exit(1);
	}

	fseek(pcap, 4, SEEK_CUR);

	{
		uint8_t  *memory;
		size_t   begin, mem_len;

		begin = ftell(pcap);
		fseek(pcap, 0, SEEK_END);
		mem_len = ftell(pcap) - begin;
		fseek(pcap, begin, SEEK_SET);

		memory = calloc(mem_len, sizeof(uint8_t));
		if (memory == NULL) {
			perror("could not get memory for pcap file");
			exit(1);
		}

		fread((void*)memory, sizeof(uint8_t), mem_len, pcap);

		printf("packet,time,bytes\n");

		bool seen_go = false;

		while (mem_len > 0) {
			pcaprec_hdr_t *rec_hdr = (pcaprec_hdr_t*)memory;
			memory += sizeof(pcaprec_hdr_t);

			memory += 14; // ethernet header
			ip_hdr_t *ip_hdr = (ip_hdr_t*)(memory);
			memory += sizeof(ip_hdr_t);

			// version is encoded in leftmost four bits
			uint8_t version = (ip_hdr->ver_len >> 4);

			// version must be ip v4
			if (version != 4) goto skip;

			// protocol must be udp

			if (ip_hdr->protocol != 17) goto skip;

			// packets from myself to server
			if (ip_hdr->src_addr != self_addr) goto skip;
			if (ip_hdr->dst_addr != server_addr) goto skip;

			udp_hdr_t *udp_hdr = (udp_hdr_t*)memory;
			memory += sizeof(udp_hdr_t);

			uint8_t *pktdata = memory;
			uint16_t datalen = ntohs(udp_hdr->pkt_size) - sizeof(udp_hdr);

			uint64_t usec = ((uint64_t)rec_hdr->ts_sec) * 1000 * 1000 + rec_hdr->ts_usec;

			if (seen_go) {
				uint32_t data = ntohl(*((uint32_t*)pktdata));
				printf("%u,%" u64f ",%u\n", data, usec, datalen);
			} else if (strncmp("SETGO", pktdata, 5) == 0) {
				seen_go = true;
			}

skip:
			memory = ((uint8_t*)rec_hdr);
			memory += sizeof(pcaprec_hdr_t);
			memory += rec_hdr->incl_len;

			mem_len -= sizeof(pcaprec_hdr_t);
			mem_len -= rec_hdr->incl_len;
		}
	}

	fclose(pcap);
	return(0);
}