#!/usr/bin/env bash

process_file(){
    echo "$2 with $1"
    cat "$2" | $1 > /tmp/typedef_after_decl.tmp
    mv "/tmp/typedef_after_decl.tmp" 
}

export -f process_file

EXE_FILE=$2

find $1 -type f -name "*.c" -exec bash -c "process_file ${EXE_FILE} {}" \;

