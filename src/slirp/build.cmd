@echo off
del /Q *.obj
del /Q slirp.lib
cl  /c /O2 /Ob1 /GF /FD /EHsc /MD /Gy /Fd -I. *.c
lib /out:slirp.lib bootp.obj cksum.obj debug.obj if.obj ip_icmp.obj ip_input.obj ip_output.obj mbuf.obj misc.obj queue.obj sbuf.obj slirp.obj socket.obj tcp_input.obj tcp_output.obj tcp_subr.obj tcp_timer.obj tftp.obj udp.obj
del /Q *.obj
echo "tada!?"
dir slirp.lib