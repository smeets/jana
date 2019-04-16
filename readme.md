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
