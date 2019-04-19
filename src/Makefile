CFLAGS=-Wall
LDFLAGS=-lm
HOST_CC=$(CC)
ARM_CC=arm-linux-gnueabi-gcc

jana: jana.c
	$(HOST_CC) $< -o $@ -O3

jana-arm: jana.c
	$(ARM_CC) $< -o $@ -static

clean:
	rm -f jana
	rm -f jana-arm
