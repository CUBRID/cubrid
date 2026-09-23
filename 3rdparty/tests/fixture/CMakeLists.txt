#
# Copyright 2026 CUBRID Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.21)

project(cubrid_thirdparty_contract_fixture C)

add_library(fixture_static STATIC src/fixture.c)
set_target_properties(fixture_static PROPERTIES OUTPUT_NAME fixture)
target_include_directories(fixture_static PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(fixture_shared SHARED src/fixture.c)
set_target_properties(fixture_shared PROPERTIES OUTPUT_NAME fixture SOVERSION 2 VERSION 2.0.0)
target_include_directories(fixture_shared PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")

install(TARGETS fixture_static fixture_shared
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  )
install(FILES
  include/fixture/header_only.hpp
  include/fixture/shared.h
  DESTINATION include/fixture
  )
install(FILES licenses/fixture.txt DESTINATION licenses)
