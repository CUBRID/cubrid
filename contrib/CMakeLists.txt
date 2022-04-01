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

if(UNIX)
  # scripts for init
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/init.d/cubrid-ha
    ${CMAKE_SOURCE_DIR}/contrib/init.d/cubrid
    DESTINATION ${CUBRID_DATADIR}/init.d)

  # scripts for RPM
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/rpm/cubrid.csh
    ${CMAKE_SOURCE_DIR}/contrib/rpm/cubrid.sh
    DESTINATION ${CUBRID_DATADIR}/rpm)
endif(UNIX)


install(FILES
  ${CMAKE_SOURCE_DIR}/contrib/scripts/check_reserved.sql
  DESTINATION ${CUBRID_DATADIR}/scripts)
install(PROGRAMS
  ${CMAKE_SOURCE_DIR}/contrib/scripts/broker_log_top.py
  ${CMAKE_SOURCE_DIR}/contrib/scripts/brokerstatus_to_csv.py
  ${CMAKE_SOURCE_DIR}/contrib/scripts/statdump_to_csv.py
  DESTINATION ${CUBRID_DATADIR}/scripts)


# scripts for HA
if(UNIX)
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/ha_make_slavedb.sh
    DESTINATION ${CUBRID_DATADIR}/scripts/ha)
  install(FILES
    ${CMAKE_SOURCE_DIR}/${VERSION_FILE}
    DESTINATION ${CUBRID_DATADIR}/scripts/ha
    RENAME VERSION)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/README
    DESTINATION ${CUBRID_DATADIR}/scripts/ha)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/functions/ha_check_environment.sh
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/functions/ha_check_script.sh
    DESTINATION ${CUBRID_DATADIR}/scripts/ha/functions)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/expect/scp_from.exp
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/expect/scp_to.exp
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/expect/ssh.exp
    DESTINATION ${CUBRID_DATADIR}/scripts/ha/expect)
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/scripts/ha/common/common.sh
    DESTINATION ${CUBRID_DATADIR}/scripts/ha/common)
endif(UNIX)


# files for systemtap
if(ENABLE_SYSTEMTAP)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/cubrid_func.stp
    DESTINATION ${CUBRID_DATADIR}/systemtap/tapset)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/buffer_access.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/global_time.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/io_stat.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/lock_time.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/obj_access.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/query_time.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/transaction.stp
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/topcallstack/topcallstack_helper.stp
    DESTINATION ${CUBRID_DATADIR}/systemtap/tapset/scripts)
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/topcallstack/get_file_line.sh
    ${CMAKE_SOURCE_DIR}/contrib/systemtap/scripts/topcallstack/topcallstack
    DESTINATION ${CUBRID_DATADIR}/systemtap/tapset/scripts)
endif(ENABLE_SYSTEMTAP)

if(WIN32)
  install(FILES
    ${CMAKE_SOURCE_DIR}/contrib/windows_scripts/cubrid_env.bat
    DESTINATION ${CUBRID_DATADIR}/windows_scripts/)
endif(WIN32)

# scripts for installation
if(UNIX)
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/install/install.sh
    DESTINATION ${CUBRID_DATADIR}/install/)
endif(UNIX)

# multi-process unloaddb scripts
if(UNIX)
  install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/contrib/scripts/unloaddb.sh
    DESTINATION ${CUBRID_DATADIR}/scripts/)
endif(UNIX)
