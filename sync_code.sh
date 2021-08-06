#!/bin/sh
# copy src from mac to linux.
SRC=~/src/other/coroutine/co_kimserver/
LAN_DST=root@192.168.0.200:/root/src/other/coroutine/co_kimserver/
REMOTE_DST=root@wenfh2020_sgx.com:/home/other/coroutine/co_kimserver/
DST=$LAN_DST

work_path=$(dirname $0)
cd $work_path

[ $1x == 'remote'x ] && DST=$REMOTE_DST

echo "send files to: $DST"

rsync -avz --exclude="*.o" \
    --exclude=".git" \
    --exclude=".vscode" \
    --exclude="*.so" \
    --exclude="*.a" \
    --exclude="*.log" \
    --exclude="config.json" \
    --exclude="*.pb.h" \
    --exclude="*.pb.cc" \
    --exclude="sync_code.sh" \
    --exclude="co_kimserver" \
    --exclude="test/test_hiredis/test_hiredis" \
    --exclude="test/test_log/test_log" \
    --exclude="test/test_mysql/test_mysql" \
    --exclude="test/test_mysql_mgr/test_mysql_mgr" \
    --exclude="test/test_redis_mgr/test_redis_mgr" \
    --exclude="test/test_tcp/test_tcp" \
    --exclude="test/test_tcp_pressure/test_tcp_pressure" \
    --exclude="test/test_timer/test_timer" \
    --exclude="test/test_session/test_session" \
    --exclude="test/test_timers/test_timers" \
    --exclude="test/test_json/test_json" \
    $SRC $DST
