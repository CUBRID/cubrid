#!/bin/bash

export server_list='##memcached server list##'
export behavior='TODO'
export libcubmemc_path='##cubmemc library path##'

function error()
{
  curr_time=$(date '+%Y-%m-%d %H:%M:%S')
  echo "[$curr_time ERROR] $1"
}

function check_error
{
  if [[ $? -ne 0 ]]; then
    error "$1"
  fi
}
