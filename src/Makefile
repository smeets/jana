
CFLAGS=-Wall
JANA_LDFLAGS=-lm
HOST_CC=$(CC)
ARM_CC=arm-linux-gnueabi-gcc

.PHONY: all

all: jana pcap2csv calc

jana: jana.c
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3 $(JANA_LDFLAGS)

jana-arm: jana.c
	$(ARM_CC) $< -o $@ -static -O3  $(JANA_LDFLAGS)

pcap2csv: pcap2csv.c
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3

calc: calc.c
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3

clean:
	rm -f jana
	rm -f jana-arm
	rm -f pcap2csv
	rm -f calc

