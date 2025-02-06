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

message ("[INFO] Install JDK 8")

if(UNIX)
  set(JDK_URL "https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u442-b06/OpenJDK8U-jdk_x64_linux_hotspot_8u442b06.tar.gz")
  set(JDK_ARCHIVE "${JDK_DIR}/jdk8.tar.gz")
else(UNIX)
  set(JDK_URL "https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u442-b06/OpenJDK8U-jdk_x64_windows_hotspot_8u442b06.zip")
  set(JDK_ARCHIVE "${JDK_DIR}/jdk8.zip")
endif(UNIX)

set(JDK_DEST_DIR "${JDK_DIR}/jdk8")

if(NOT EXISTS ${JDK_ARCHIVE})
    message(STATUS "[INFO] Downloading Temurin JDK 8...")
    file(DOWNLOAD ${JDK_URL} ${JDK_ARCHIVE})
endif()

if(NOT EXISTS ${JDK_DEST_DIR})
    message(STATUS "[INFO] Extracting JDK...")
    
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${JDK_ARCHIVE}
        WORKING_DIRECTORY ${JDK_DIR}
    )
    file(REMOVE ${JDK_ARCHIVE})

    file(GLOB EXTRACTED_DIR "${JDK_DIR}/jdk8u*")
    if(EXTRACTED_DIR)
      file(RENAME ${EXTRACTED_DIR} ${JDK_DEST_DIR})
      file(REMOVE_RECURSE ${JDK_DEST_DIR}/man)
      file(REMOVE_RECURSE ${JDK_DEST_DIR}/sample)
    endif()
endif()

message(STATUS "[INFO] JDK 8 downloaded and extracted to ${JDK_DEST_DIR}")
