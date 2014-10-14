rm -f *.o slirp106i386.a
gcc -arch i386 -c -Os -I. *.c
ar rcs slirp106i386.a *.o
ls -l *.o|wc -l
rm -f *.o
