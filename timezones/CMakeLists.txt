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

message (STATUS ".. timezone shared library")

# create an executable that can generate timezones.c file
message (STATUS ".... gen_timezone executable")
set (GEN_TIMEZONES_SOURCES
  ${EXECUTABLES_DIR}/generate_timezone.cpp
  )
message (STATUS "...... source files: ${GEN_TIMEZONES_SOURCES}")
set_source_files_properties (
  ${GEN_TIMEZONES_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(gen_timezone
  ${GEN_TIMEZONES_SOURCES}
  )

target_link_libraries (gen_timezone LINK_PRIVATE cubridsa)
target_compile_definitions (gen_timezone PRIVATE SA_MODE)

# run executable
message (STATUS ".... generate timezone command")
add_custom_command(
  OUTPUT tzlib/timezones.c
  COMMENT "generating tzlib/timezones.c from ${CMAKE_CURRENT_SOURCE_DIR}/tzdata"
  COMMAND ${CMAKE_COMMAND} -E make_directory tzlib
  COMMAND gen_timezone ${CMAKE_CURRENT_SOURCE_DIR}/tzdata tzlib/timezones.c
)
set_source_files_properties(tzlib/timezones.c PROPERTIES GENERATED true)

# create library
message (STATUS ".... cubrid_timezones lib")
add_library(cubrid_timezones SHARED tzlib/timezones.c)
set_target_properties(cubrid_timezones PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
if(WIN32)
  set_target_properties(cubrid_timezones PROPERTIES OUTPUT_NAME libcubrid_timezones)
endif(WIN32)
add_dependencies(cubrid_timezones gen_timezone)

install(TARGETS cubrid_timezones
  RUNTIME DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  )

if(UNIX)
  install(PROGRAMS
    make_tz.sh
    DESTINATION ${CUBRID_BINDIR})
  install(PROGRAMS
    tzlib/build_tz.sh
    DESTINATION ${CUBRID_TZDIR}/tzlib)
else(UNIX)
  set(MAKE_TZ_SCRIPT make_tz_x64.bat)
  set(MAKE_TZLIB_DIR tzlib_win_x64)
  install(PROGRAMS
    ${MAKE_TZ_SCRIPT}
    DESTINATION ${CUBRID_BINDIR}
    RENAME make_tz.bat)
  install(FILES
    ${MAKE_TZLIB_DIR}/makefile
    DESTINATION ${CUBRID_TZDIR}/tzlib)
endif(UNIX)

install(DIRECTORY
  tzdata
  DESTINATION ${CUBRID_TZDIR}
  PATTERN "Makefile.am" EXCLUDE)

install(FILES
  ${CMAKE_SOURCE_DIR}/src/base/timezone_lib_common.h
  DESTINATION ${CUBRID_TZDIR}/tzlib COMPONENT Header)

# install pdb files for debugging on windows
# TODO: move the pdb files to CUBRID/symbols directory
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
