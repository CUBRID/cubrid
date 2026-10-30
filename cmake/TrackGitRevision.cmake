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
# HEAD names a branch in an attached checkout. Watch its ref as well as HEAD.
# Git resolves shared and per-worktree paths, including relative gitdir files.
function(cubrid_watch_git_revision)
  execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE head_ref OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(git_paths HEAD logs/HEAD packed-refs reftable/tables.list)
  if(head_ref)
    list(APPEND git_paths "${head_ref}")
  endif()
  foreach(git_path IN LISTS git_paths)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${git_path}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE dependency RESULT_VARIABLE git_result
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT git_result EQUAL 0)
      message(FATAL_ERROR "Could not locate Git metadata: ${git_path}")
    endif()
    get_filename_component(dependency "${dependency}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    # A packed ref has no loose file yet. Its parent changes when a commit creates
    # that file, even with reflogs disabled. Missing metadata cannot be a Make input.
    if(git_path STREQUAL head_ref)
      while(NOT EXISTS "${dependency}")
        get_filename_component(dependency "${dependency}" DIRECTORY)
      endwhile()
    endif()
    if(EXISTS "${dependency}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dependency}")
    endif()
  endforeach()
endfunction()
