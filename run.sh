#!/bin/sh

work_path=$(dirname $0)
cd $work_path
work_path=$(pwd)

server=co_kimserver
server_file=$work_path/bin/$server
work_src=$work_path/src/
core_proto_path=$work_path/src/core/protobuf

kill_process() {
    echo '<------------------'
    name=$1
    # '_w_' --> subprocess, kill parent will ok!
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

gen_proto() {
    cd $core_proto_path
    [ ! -x gen_proto.sh ] && chmod +x gen_proto.sh
    ./gen_proto.sh
    [ $? -ne 0 ] && echo 'gen protobuf file failed!' && exit 1
}

run() {
    cd $work_path/src/modules
    find . -name '*.so' -type f -exec cp -f {} $work_path/bin/modules \;
    cd $work_path/bin
    ./$server config.json
    echo '<------------------'
}

if [ $1x = 'help'x ]; then
    echo './run.sh'
    echo './run.sh all'
    echo './run.sh kill <process name>'
    echo './run.sh compile'
    echo './run.sh compile all'
    exit 1
elif [ $1x = 'kill'x ]; then
    kill_process $2
    exit 1
elif [ $1x = 'compile'x ]; then
    if [ $2x = 'all'x ]; then
        gen_proto
        cd $work_src
        make clean && make
    else
        cd $work_src
        make
    fi
    exit 1
elif [ $1x = 'all'x ]; then
    gen_proto
    cd $work_src
    make clean && make
else
    if [ $# -gt 0 ]; then
        echo 'invalid param. (./run.sh help)'
        exit 1
    fi
fi

kill_process 'kim-'
sleep 1
run
cat_process $server
