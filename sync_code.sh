#!/bin/sh
# copy src from mac to linux.

work_path=$(dirname $0)
cd $work_path

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
    ~/src/other/coroutine/co_kimserver/ root@wenfh2020_sgx.com:/home/other/coroutine/co_kimserver/
