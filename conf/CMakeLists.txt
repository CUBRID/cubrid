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

file(GLOB conf_files "${CMAKE_SOURCE_DIR}/conf/cubrid*.conf*" "${CMAKE_SOURCE_DIR}/conf/shard*.txt")
file(GLOB locale_files "${CMAKE_SOURCE_DIR}/conf/cubrid*.txt")

# no SSL certificate is shipped: the first start of a broker with SSL=ON creates a private key and a self-signed
# certificate for its host
install(FILES ${conf_files} ${locale_files}
  DESTINATION ${CUBRID_CONFDIR})
