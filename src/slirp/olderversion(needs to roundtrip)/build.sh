#!/bin/sh
rm *.o slirplinux.a
gcc -g -Os -c -I. *.c
ar rcs slirplinux.a *.o
ranlib slirplinux.a
echo "this number should be 19"
ls -l *.o|wc -l
