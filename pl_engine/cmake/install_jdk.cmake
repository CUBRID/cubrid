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

# JDK_SHA256 is the archive's SHA-256, from the <archive>.sha256.txt next to it on the release page.
if(UNIX)
  set(JDK_URL "https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u452-b09/OpenJDK8U-jdk_x64_linux_hotspot_8u452b09.tar.gz")
  set(JDK_ARCHIVE "${JDK_DIR}/jdk8.tar.gz")
  set(JDK_SHA256 "9448308a21841960a591b47927cf2d44fdc4c0533a5f8111a4b243a6bafb5d27")
else(UNIX)
  set(JDK_URL "https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u442-b06/OpenJDK8U-jdk_x64_windows_hotspot_8u442b06.zip")
  set(JDK_ARCHIVE "${JDK_DIR}/jdk8.zip")
  set(JDK_SHA256 "ffe22d16a1ea6fe709a089f3472fedb783470b879634b26b32424e40bbd00b09")
endif(UNIX)

if(DEFINED ENV{CUBRID_DOWNLOAD_CACHE})
  get_filename_component(JDK_FILE "${JDK_URL}" NAME)
  set(JDK_ARCHIVE "$ENV{CUBRID_DOWNLOAD_CACHE}/jdk/${JDK_FILE}")
  file(LOCK "$ENV{CUBRID_DOWNLOAD_CACHE}/jdk" DIRECTORY)
endif()

set(JDK_DEST_DIR "${JDK_DIR}/jdk8")

if(EXISTS ${JDK_ARCHIVE})
  file(SHA256 ${JDK_ARCHIVE} JDK_GOT)
endif()
if(NOT JDK_GOT STREQUAL JDK_SHA256)
    message(STATUS "[INFO] Downloading Temurin JDK 8...")
    file(DOWNLOAD ${JDK_URL} ${JDK_ARCHIVE} EXPECTED_HASH SHA256=${JDK_SHA256} INACTIVITY_TIMEOUT 60)
endif()

if(NOT EXISTS ${JDK_DEST_DIR})
    message(STATUS "[INFO] Extracting JDK...")
    
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${JDK_ARCHIVE}
        WORKING_DIRECTORY ${JDK_DIR}
    )
    if(NOT DEFINED ENV{CUBRID_DOWNLOAD_CACHE})
      file(REMOVE ${JDK_ARCHIVE})
    endif()

    file(GLOB EXTRACTED_DIR "${JDK_DIR}/jdk8u*")
    if(EXTRACTED_DIR)
      file(RENAME ${EXTRACTED_DIR} ${JDK_DEST_DIR})
      file(REMOVE_RECURSE ${JDK_DEST_DIR}/man)
      file(REMOVE_RECURSE ${JDK_DEST_DIR}/sample)
      file(REMOVE_RECURSE ${JDK_DEST_DIR}/src.zip)
    endif()

    message(STATUS "[INFO] JDK 8 is downloaded and extracted to ${JDK_DEST_DIR}")
endif()

