#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#if defined(_WIN32)
	#define u64f "I64u"
#else
	#define u64f PRIu64
#endif

void usage() {
    fprintf(stderr, "Usage: calc pcap log\n");
    fprintf(stderr, "       calc [-h|--help]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  pcap           path to pcap-parsed csv file\n");
    fprintf(stderr, "  log            path to csv logfile from jana\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  calc capture.csv logfile.csv\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Built " __DATE__ " " __TIME__ "\n");
    exit(1);
}

int main(int argc, char const *argv[])
{
	if (argc == 1 ||
		strcmp("-h", argv[1]) == 0 ||
		strcmp("--help", argv[1]) == 0 ||
		argc != 3) usage();


	FILE *pcap = fopen(argv[1], "r");
	if (pcap == NULL) {
		perror("error reading file");
		exit(1);
	}

	FILE *call = fopen(argv[2], "r");
	if (call == NULL) {
		perror("error reading file");
		exit(1);
	}

	fscanf(pcap, "packet,time,bytes\n");
	fscanf(call, "packet,time,sendto_us\n");

	printf("packet,send,syscall,pcap,bytes\n");
	uint64_t dropped = 0;
	while (!feof(call) && !feof(pcap)) {
		uint32_t pcap_packet, call_packet;
		uint64_t pcap_time, call_time, call_extra;
		uint32_t pcap_bytes;

		fscanf(pcap, "%u,%" u64f ",%u\n", &pcap_packet, &pcap_time, &pcap_bytes);

		do {
			fscanf(call, "%u,%" u64f ",%" u64f "\n", &call_packet, &call_time, &call_extra);
			if (call_packet == pcap_packet) {
				printf("%u,%" u64f ",%" u64f ",%" u64f ",%u\n",
					pcap_packet,
					call_time,
					call_extra,
					pcap_time,
					pcap_bytes);
			} else {
				dropped++;
			}
		} while (call_packet != pcap_packet && !feof(call));
	}

	fprintf(stderr, "dropped %" u64f " packets\n", dropped);
	fclose(pcap);
	fclose(call);

	return 0;
}