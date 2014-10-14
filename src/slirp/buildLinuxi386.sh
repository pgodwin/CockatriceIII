rm -f *.o slirplinuxi386.a
gcc -c -Os -I. *.c
ar rcs slirplinuxi386.a *.o
ls -l *.o|wc -l
rm -f *.o
