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

macro(expose_3rdparty_variable name)
  string(TOUPPER ${name} NAME_UPPER)

  # Names of variables we will set in this function.
  set(TARGET_VAR        ${NAME_UPPER}_TARGET)
  set(LIBS_VAR          ${NAME_UPPER}_LIBS)
  set(INCLUDES_VAR      ${NAME_UPPER}_INCLUDES)

  set(TARGET_DATA     ${${TARGET_VAR}}   )
  set(LIBS_DATA       ${${LIBS_VAR}}     )
  set(INCLUDES_DATA   ${${INCLUDES_VAR}} )

  # Finally, export the above variables to the parent scope.
  set(${TARGET_VAR}    ${TARGET_DATA}   PARENT_SCOPE)
  set(${LIBS_VAR}      ${LIBS_DATA}     PARENT_SCOPE)
  set(${INCLUDES_VAR}  ${INCLUDES_DATA} PARENT_SCOPE)

  # To display variables for test
  # message("${TARGET_VAR} = ${TARGET_DATA}")
  # message("${LIBS_VAR} = ${LIBS_DATA}")
  # message("${INCLUDES_VAR} = ${INCLUDES_DATA}")
endmacro(expose_3rdparty_variable)

# A file that fails the hash, like one a failed or killed fetch left behind, is fetched again.
# The hash is checked here, not by EXPECTED_HASH, which makes a failed fetch fatal and leaves no retry.
function(fetch_verified url dest hash)
  foreach(attempt RANGE 3)
    if(EXISTS "${dest}")
      file(SHA256 "${dest}" got)
    endif()
    if(hash STREQUAL "SHA256=${got}")
      return()
    elseif(attempt EQUAL 3)
      message(FATAL_ERROR "Failed to fetch ${url} (status: ${st}, expected ${hash}, got SHA256=${got})")
    elseif(attempt GREATER 0)
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 5)
    elseif(NOT "$ENV{CUBRID_DOWNLOAD_CACHE}" STREQUAL "")
      message(WARNING "${dest} is missing or stale; fetching it. The build image needs an update.")
    endif()
    file(DOWNLOAD "${url}" "${dest}" INACTIVITY_TIMEOUT 60 STATUS st)
  endforeach()
endfunction()
