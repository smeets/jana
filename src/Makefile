
CFLAGS=-Wall
JANA_LDFLAGS=-lm
HOST_CC=$(CC)
ARM_CC=arm-linux-gnueabi-gcc

jana: jana.c
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3 $(JANA_LDFLAGS)

jana-arm: jana.c
	$(ARM_CC) $< -o $@ -static

pcap2csv: pcap2csv.h
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3

calc: calc.h
	$(HOST_CC) $(CFLAGS) $< -o $@ -O3

clean:
	rm -f jana
	rm -f jana-arm
	rm -f pcap2csv
	rm -f calc

