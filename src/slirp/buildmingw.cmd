@echo off
del /Q *.o
del /Q slirpmingw.a
gcc -c -Os -I. *.c
ar rcs slirpmingw.a *.o
del /Q *.o
echo "tada!?"
dir slirpmingw.a