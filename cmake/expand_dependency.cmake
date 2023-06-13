#
#
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

macro(_EXPAND_EXTERNAL_DEPENDENCY depender prefix_dependee)
  message(STATUS "${depender} depends on external project ${prefix_dependee}")

  set(DEPENDEE_INCLUDE_DIR ${${prefix_dependee}_INCLUDES})
  set(DEPENDEE_LIBS_DIR ${${prefix_dependee}_LIBS})
  set(DEPENDEE_TARGET ${${prefix_dependee}_TARGET})

  if(DEFINED DEPENDEE_INCLUDE_DIR)
    target_include_directories(${depender} PRIVATE ${DEPENDEE_INCLUDE_DIR})
  else()
    message(WARNING "include directory variable(${prefix_dependee}_INCLUDES) is not set for target ${prefix_dependee}")
  endif()
  
  if(DEFINED DEPENDEE_LIBS_DIR)
    target_link_libraries(${depender} LINK_PRIVATE ${DEPENDEE_LIBS_DIR})
  else()
    message(WARNING "libraries directory variable(${prefix_dependee}_LIBS) is not set for target ${prefix_dependee}")
  endif()

  if (TARGET ${DEPENDEE_TARGET})
    add_dependencies(${depender} ${DEPENDEE_TARGET})
  else(TARGET ${DEPENDEE_TARGET})
    message(STATUS "target ${prefix_dependee}_TARGET=${${prefix_dependee}_TARGET} doesn't exist")
  endif(TARGET ${DEPENDEE_TARGET})
endmacro(_EXPAND_EXTERNAL_DEPENDENCY)

function(target_external_dependencies depender)
  foreach (arg IN LISTS ARGN)
    _EXPAND_EXTERNAL_DEPENDENCY(${depender} ${arg})
  endforeach()
endfunction(target_external_dependencies)
