rm *.o slirpOSX.a
#gcc -g -O2 -flto -arch i386 -c -I. *.c
clang -g -O2 -arch i386 -c -I. *.c
ar rcs slirpOSX.a *.o
ranlib slirpOSX.a
echo "this number should be 19"
ls -l *.o|wc -l
