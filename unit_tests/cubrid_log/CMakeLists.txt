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

project(test_cubrid_log)

add_executable(test_cubrid_log
  test_cubrid_log.cpp
  )

target_compile_definitions(test_cubrid_log PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_cubrid_log PRIVATE
  ${API_DIR}
  )

target_link_libraries(test_cubrid_log PRIVATE
  cubridcs
  )
