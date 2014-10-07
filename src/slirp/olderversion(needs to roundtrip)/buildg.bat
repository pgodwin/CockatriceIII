del slirp.a
del *.o
rem gcc -O2 -g -flto -mno-ms-bitfields -c -DDEBUG -I. *.c
rem gcc -O2 -g -flto -mno-ms-bitfields -c -I. *.c
gcc -O0 -g -flto -mno-ms-bitfields -c -I. *.c
rem gcc -O2 -flto -mno-ms-bitfields -c -I. *.c

ar rcs slirp.a *.o
ranlib slirp.a