#!/bin/sh

work_path=$(dirname $0)
cd $work_path
work_path=$(pwd)
subdirs=$(ls $work_path)

protoc --version >/dev/null 2>&1
[ $? -ne 0 ] && echo 'pls install protobufs.' && exit 1

for dir in $subdirs; do
    if [ -d $work_path/$dir ]; then
        cd $work_path/$dir
        protoc -I. --cpp_out=. *.proto
    fi
done
