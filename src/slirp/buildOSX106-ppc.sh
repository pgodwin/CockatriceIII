rm -f *.o slirp106PPC.a
gcc -arch ppc -c -Os -I. *.c
ar rcs slirp106PPC.a *.o
ranlib slibp106PPC.a
ls -l *.o|wc -l
rm -f *.o
