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

project (test_json_builder)

# json_builder.cpp needs nothing from the engine, so build it into the test
# rather than linking a library for it
set (TEST_JSON_BUILDER_SOURCES
  ${BASE_DIR}/json_builder.cpp
  test_json_builder.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_JSON_BUILDER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_json_builder
  ${TEST_JSON_BUILDER_SOURCES}
  )

target_compile_definitions(test_json_builder PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_json_builder PRIVATE
  ${TEST_INCLUDES}
  ${BASE_DIR}
  ${COMPAT_DIR}
  )

if(UNIX)
  set_source_files_properties(${BASE_DIR}/json_builder.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
endif(UNIX)

add_dependencies(test_json_builder
  ${CATCH2_TARGET}
  ${EP_TARGETS}
  )
