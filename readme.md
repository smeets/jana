## building
host target

 - on linux/bsd: `make jana`
 - on windows: `cd src && cl jana.c`

### cross-compiling on ubuntu
arm target - `armv7l-linux-gnueabi`

```bash
$ sudo apt-get install gcc make gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi
$ make jana-arm
```

### cross-compiling on windows
using docker

```bash
$ docker pull ev3dev/debian-stretch-cross
$ docker tag ev3dev/debian-stretch-cross ev3cc
$ # cd path\to\jana
$ docker run --rm -it -v %cd%\src:/src -w /src ev3cc
$ # in container
compiler@xyz:/src$ make jana-arm
compiler@xyz:/src$ exit
```

## notes

port 3000 in hex is `0x0BB8`

 - `cat /proc/sys/net/core/rmem_max`
 - `cat /proc/sys/net/core/rmem_default`
 - `cat /proc/sys/net/core/wmem_max`
 - `cat /proc/sys/net/core/wmem_default`

print *minimum*, *initial* and *maxmimum* sizes
```bash
cat /proc/sys/net/ipv4/udp_mem
5400    7200    10800
```

```bash
$ cat /proc/net/udp | grep '0BB8'
  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops
 184: 00000000:0BB8 00000000:0000 07 00000000:00000000 00:00000000 00000000     0        0 71535230 2 c652b9e0 6215
```

## wireshark

easier to use wireshark. save captured logs in `pcap` format, then use included `pcap2csv` program to translate into csv, and use included `calc` program to diff output from `jana` with wireshark capture .

 - `make pcap2csv`
 - `make calc`

### tshark
very good. very feel. no qt in sight.

```bash
$ # list interfaces
$ tshark -D
...
$ tshark -i 2 -F pcap -w capture.pcap -f "udp"
```