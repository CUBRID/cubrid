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

project (test_pgbuf_inspector)

# The wire module needs nothing from the engine, so it is built into the
# executables here rather than linked from an engine library.
set (PGBUF_INSPECTOR_WIRE_SOURCES
  ${STORAGE_DIR}/pgbuf_inspector_wire.cpp
  )

# Shared by the unit test and the corpus generator: hashing for the checksum
# file and decoding of the semantic inputs the corpus streams are made from.
set (PGBUF_INSPECTOR_CORPUS_SUPPORT_SOURCES
  corpus_support.cpp
  sha256.cpp
  )

# The versioned contract directory holds the conformance corpus the test
# reproduces and verifies, and the generator rewrites.
set (PGBUF_INSPECTOR_CONTRACT_DIR ${CMAKE_SOURCE_DIR}/docs/pgbuf-inspector/v1)

# Both executables build the same module and support sources with the same
# definitions and include paths; they differ only in their entry source.
function (pgbuf_inspector_add_executable name entry_source)
  set (sources
    ${PGBUF_INSPECTOR_WIRE_SOURCES}
    ${PGBUF_INSPECTOR_CORPUS_SUPPORT_SOURCES}
    ${entry_source}
    )
  set_source_files_properties (${sources} PROPERTIES LANGUAGE CXX)
  add_executable (${name} ${sources})
  target_compile_definitions (${name} PRIVATE
    ${COMMON_DEFS}
    PGBUF_INSPECTOR_CONTRACT_DIR="${PGBUF_INSPECTOR_CONTRACT_DIR}"
    )
  target_include_directories (${name} PRIVATE
    ${TEST_INCLUDES}
    ${STORAGE_DIR}
    ${COMPAT_DIR}
    )
  add_dependencies (${name} ${EP_TARGETS})
endfunction ()

pgbuf_inspector_add_executable (test_pgbuf_inspector test_pgbuf_inspector_wire.cpp)
add_dependencies (test_pgbuf_inspector ${CATCH2_TARGET})
add_test (NAME test_pgbuf_inspector COMMAND test_pgbuf_inspector)

# The corpus generator rewrites the corpus streams, checksum file and manifest
# hash through the same serializer the test proves. It is a maintenance tool,
# not a test, so it is not registered with ctest.
pgbuf_inspector_add_executable (pgbuf_inspector_corpus_generator pgbuf_inspector_corpus_generator.cpp)
