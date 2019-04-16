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
