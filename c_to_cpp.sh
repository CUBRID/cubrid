#!/usr/bin/env bash
process_file(){
  for fullpath in "$@"
  do
    base="${fullpath%.[^.]*}" 
    git mv "${fullpath}" "${base}.cpp"
  done
}

export -f process_file

find $1 -type f -name "*.c" -exec bash -c 'process_file {}' \;
