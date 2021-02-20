#!/bin/sh

work_path=$(dirname $0)
cd $work_path
work_path=$(pwd)

server=co_kimserver
server_file=$work_path/bin/$server
core_path=$work_path/src/core/
so_path=$work_path/src/modules/
core_proto_path=$core_path/protobuf

kill_process() {
    echo '<------------------'
    name=$1
    # _w_ --> subprocess, kill parent will ok!
    local processes=$(ps -ef | grep $name | grep -v 'grep\|\.log\|vim\|_w_' | awk '{if ($2 > 1) print $2;}')
    for p in $processes; do
        echo "kill pid: $p."
        kill $p
        [ $? -ne 0 ] && echo "kill pid: $p failed!"
    done
}

cat_process() {
    sleep 1
    local name=$1
    ps -ef | grep -i $name | grep -v 'grep\|log\|vim' | awk '{ print $2, $8 }'
}

compile_core() {
    cd $core_path
    [ $1x == 'all'x ] && make clean
    make
    [ $? -ne 0 ] && exit 1
}

compile_so() {
    cd $so_path
    [ $1x == 'all'x ] && make clean
    make
    [ $? -ne 0 ] && exit 1
}

gen_proto() {
    cd $core_proto_path
    if [ ! -f http.pb.cc ]; then
        [ ! -x gen_proto.sh ] && chmod +x gen_proto.sh
        ./gen_proto.sh
        [ $? -ne 0 ] && echo 'gen protobuf file failed!' && exit 1
    fi
}

run() {
    cd $work_path/bin
    ./$server config.json
    echo '<------------------'
}

if [ $1x == 'help'x ]; then
    echo './run.sh'
    echo './run.sh all'
    echo './run.sh kill'
    echo './run.sh compile'
    echo './run.sh compile so'
    echo './run.sh compile so all'
    echo './run.sh compile core'
    echo './run.sh compile core all'
    exit 1
elif [ $1x == 'kill'x ]; then
    kill_process $2
    exit 1
elif [ $1x == 'compile'x ]; then
    if [ $2x == 'so'x ]; then
        gen_proto
        compile_so $3
    elif [ $2x == 'core'x ]; then
        gen_proto
        compile_core $3
    else
        compile_core $2
        compile_so $2
    fi
    exit 1
elif [ $1x == 'all'x ]; then
    gen_proto
    compile_core $1
    compile_so $1
else
    if [ $# -gt 0 ]; then
        echo 'invalid param. (./run.sh help)'
        exit 1
    fi
    compile_core
    compile_so
fi

kill_process 'kim-'
sleep 1
run
cat_process $server
