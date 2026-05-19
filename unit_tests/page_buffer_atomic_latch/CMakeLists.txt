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

# page_buffer_atomic_latch unit tests — reproduce the infinite-wait holes
# identified by .omc/research/atomic-latch-audit/REPORT.md.
#
# Pattern mirrors feature/oos-m2 unit_tests/oos/: tests link the real
# SERVER_MODE cubrid library and boot a cub_server in-process via the
# unittestdb CTest fixture.

project(test_page_buffer_atomic_latch_project)

set(TEST_SOURCES
  test_pgbuf_atomic_latch_main.cpp
  test_abandoned_insert_flush_infinite_wait.cpp
  test_force_grab_through_safe_flush.cpp
)

add_executable(test_page_buffer_atomic_latch ${TEST_SOURCES})

target_compile_definitions(test_page_buffer_atomic_latch PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
)

target_include_directories(test_page_buffer_atomic_latch PRIVATE
  ${TEST_INCLUDES}
)

target_link_libraries(test_page_buffer_atomic_latch PRIVATE
  ${EP_LIBS}
  cubrid
  GTest::gtest
)

# =============================================================================
# CTest fixture — share unittestdb with the OOS test suite when both run.
# Idempotent createdb / deletedb so order-independent.
# =============================================================================
add_test(NAME pgbuf_atomic_latch_setup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "
      if [ ! -f \"$ENV{CUBRID_DATABASES}/databases.txt\" ] || ! grep -q unittestdb \"$ENV{CUBRID_DATABASES}/databases.txt\" 2>/dev/null; then
        mkdir -p \"$ENV{CUBRID_DATABASES}/unittestdb\" && \
        cubrid createdb --db-volume-size=20M --log-volume-size=20M unittestdb en_US.utf8 \
          -F \"$ENV{CUBRID_DATABASES}/unittestdb\"
      fi
    "
)
set_tests_properties(pgbuf_atomic_latch_setup_db PROPERTIES FIXTURES_SETUP PGBUF_ATOMIC_LATCH_DB)

add_test(NAME pgbuf_atomic_latch_cleanup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "
      cubrid server stop unittestdb 2>/dev/null || true
      if grep -q unittestdb \"$ENV{CUBRID_DATABASES}/databases.txt\" 2>/dev/null; then
        cubrid deletedb unittestdb
      fi
    "
)
set_tests_properties(pgbuf_atomic_latch_cleanup_db PROPERTIES FIXTURES_CLEANUP PGBUF_ATOMIC_LATCH_DB TIMEOUT 30)

add_test(NAME test_page_buffer_atomic_latch COMMAND test_page_buffer_atomic_latch)
set_tests_properties(test_page_buffer_atomic_latch
  PROPERTIES FIXTURES_REQUIRED PGBUF_ATOMIC_LATCH_DB RUN_SERIAL TRUE TIMEOUT 120
)
