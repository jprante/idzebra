rm -f zebrasrv.pid
rm -f srv.log
../../index/zebrasrv -S -c zebra2.cfg -l srv.log tcp:@:9901 &
sleep 1
test -f zebrasrv.pid || exit 1
