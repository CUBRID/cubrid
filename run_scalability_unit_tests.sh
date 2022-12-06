#!/bin/bash
#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
#

declare -i global_error_code=0

function run_test() {
  ${@} || global_error_code=$?
}

run_test ./test_checkpoint_info
run_test ./test_log_lsa_utils
run_test ./test_log_recovery_parallel [ci]
run_test ./test_meta_log
run_test ./test_prior_list_serialize
run_test ./test_prior_sendrecv
run_test ./test_request_cs
run_test ./test_server_request_responder
run_test ./test_log_repl_atomic_helper

exit $global_error_code

