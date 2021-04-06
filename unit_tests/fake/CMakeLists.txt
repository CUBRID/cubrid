#
# Copyright (C) 2016 Search Solution Corporation. All rights reserved.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

#
# Fake libs
#
project(fake)

#
# fake_worker_tdes
#
set(FAKE_WORKER_TDES_SRC
  thread_utils.cpp
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/packer.cpp
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/lockfree_transaction_system.cpp
  ${BASE_DIR}/error_context.cpp
  ${BASE_DIR}/perf.cpp
  ${TRANSACTION_DIR}/log_page_async_fetcher.cpp
  ${TRANSACTION_DIR}/client_credentials.cpp
  ${THREAD_DIR}/thread_manager.cpp
  ${THREAD_DIR}/thread_daemon.cpp
  ${THREAD_DIR}/thread_entry_task.cpp
  ${THREAD_DIR}/thread_looper.cpp
  ${THREAD_DIR}/thread_waiter.cpp
  ${THREAD_DIR}/thread_worker_pool.cpp
)
SET_SOURCE_FILES_PROPERTIES(
  ${FAKE_WORKER_TDES_SRC}
  PROPERTIES LANGUAGE CXX
)
add_library(fake_worker_tdes
  ${FAKE_WORKER_TDES_SRC}
)
target_compile_options(fake_worker_tdes PRIVATE
  ${compile_warning_flags}
)
target_compile_definitions(fake_worker_tdes PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
)
target_include_directories(fake_worker_tdes PRIVATE
  ${EP_INCLUDES}
)
add_dependencies(fake_worker_tdes
  ${EP_TARGETS}
)
if(WIN32)
  target_link_libraries(fake_worker_tdes PUBLIC ws2_32)
endif(WIN32)
