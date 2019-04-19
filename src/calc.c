#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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

	fscanf(pcap, "packet,time\n");
	fscanf(call, "packet,time,sendto_us\n"); // bytes?

	printf("packet,delta\n");
	while (true) {
		uint32_t pcap_packet, call_packet;
		uint64_t pcap_time, call_time, call_extra;

		fscanf(pcap, "%u,%llu\n", &pcap_packet, &pcap_time);

		do {
			fscanf(call, "%u,%llu,%llu\n", &call_packet, &call_time, &call_extra);
			if (call_packet != pcap_packet)
				fprintf(stderr, "packet %u dropped\n", call_packet);
		} while (call_packet != pcap_packet);

		printf("%u,%llu\n", pcap_packet, pcap_time - call_time - call_extra);
		if (feof(call) || feof(pcap)) break;
	}

	fclose(pcap);
	fclose(call);

	return 0;
}