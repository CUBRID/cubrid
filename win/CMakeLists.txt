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

if(NOT WIN32)
  return()
endif(NOT WIN32)

set(CTRLSERVICE_SOURCES
  ${WIN_TOOLS_DIR}/ctrlservice/ctrlservice.cpp
  ${WIN_TOOLS_DIR}/ctrlservice/stdafx.cpp
  ${CMAKE_BINARY_DIR}/version.rc
  )
set_source_files_properties(${WIN_TOOLS_DIR}/ctrlservice/stdafx.cpp
  PROPERTIES
  COMPILE_FLAGS "/Ycstdafx.h"
  )
add_executable(ctrlservice ${CTRLSERVICE_SOURCES})
set_target_properties(ctrlservice PROPERTIES COMPILE_FLAGS "/Yustdafx.h")
target_compile_definitions(ctrlservice PRIVATE _CONSOLE)


set(CUBRIDSERVICE_SOURCES
  ${WIN_TOOLS_DIR}/cubridservice/cubridservice.cpp
  ${WIN_TOOLS_DIR}/cubridservice/stdafx.cpp
  ${CMAKE_BINARY_DIR}/version.rc
  )
set_source_files_properties(${WIN_TOOLS_DIR}/cubridservice/stdafx.cpp
  PROPERTIES
  COMPILE_FLAGS "/Ycstdafx.h"
  )
add_executable(cubridservice ${CUBRIDSERVICE_SOURCES})
set_target_properties(cubridservice PROPERTIES COMPILE_FLAGS "/Yustdafx.h")
target_compile_definitions(cubridservice PRIVATE _CONSOLE)


set(CUBRIDTRAY_SOURCES
  ${WIN_TOOLS_DIR}/cubridtray/ARCHIVELOG.CPP
  ${WIN_TOOLS_DIR}/cubridtray/CASManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/COMMONMETHOD.CPP
  ${WIN_TOOLS_DIR}/cubridtray/CUBRIDManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/CUBRIDtray.cpp
  ${WIN_TOOLS_DIR}/cubridtray/DBSTARTUP.CPP
  ${WIN_TOOLS_DIR}/cubridtray/ENV.CPP
  ${WIN_TOOLS_DIR}/cubridtray/FILENAME.CPP
  ${WIN_TOOLS_DIR}/cubridtray/FOLDER_DIALOG.CPP
  ${WIN_TOOLS_DIR}/cubridtray/LANG.CPP
  ${WIN_TOOLS_DIR}/cubridtray/MAINFRM.CPP
  ${WIN_TOOLS_DIR}/cubridtray/Manager.cpp
  ${WIN_TOOLS_DIR}/cubridtray/MANAGEREGISTRY.CPP
  ${WIN_TOOLS_DIR}/cubridtray/NTRAY.CPP
  ${WIN_TOOLS_DIR}/cubridtray/ORDBLIST.CPP
  ${WIN_TOOLS_DIR}/cubridtray/PROCESS.CPP
  ${WIN_TOOLS_DIR}/cubridtray/PROPERTY.CPP
  ${WIN_TOOLS_DIR}/cubridtray/REDIRECT.CPP
  ${WIN_TOOLS_DIR}/cubridtray/SHUTDOWN.CPP
  ${WIN_TOOLS_DIR}/cubridtray/STARTTARGETDB.CPP
  ${WIN_TOOLS_DIR}/cubridtray/STDAFX.CPP
  ${WIN_TOOLS_DIR}/cubridtray/STOPTARGETDB.CPP
  ${WIN_TOOLS_DIR}/cubridtray/ToolManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCCONF.CPP
  ${WIN_TOOLS_DIR}/cubridtray/UCMDETAIL.CPP
  ${WIN_TOOLS_DIR}/cubridtray/UCMINFO.CPP
  ${WIN_TOOLS_DIR}/cubridtray/UCPDETAIL.CPP
  ${WIN_TOOLS_DIR}/cubridtray/UCPROPERTY.CPP
  ${WIN_TOOLS_DIR}/cubridtray/VAS.CPP
  ${WIN_TOOLS_DIR}/cubridtray/WAS.CPP
  ${WIN_TOOLS_DIR}/cubridtray/cubridtray.odl
  ${WIN_TOOLS_DIR}/cubridtray/cubridtray.rc
  ${CMAKE_BINARY_DIR}/version.rc
  )
set_source_files_properties(${WIN_TOOLS_DIR}/cubridtray/STDAFX.CPP
  PROPERTIES
  COMPILE_FLAGS "/YcSTDAFX.H"
  )
set(CMAKE_MFC_FLAG 2)
add_executable(cubridtray WIN32 ${CUBRIDTRAY_SOURCES})
# TODO: remove version.h in win/ directory
target_include_directories(cubridtray PRIVATE ${CMAKE_SOURCE_DIR}/win)
set_target_properties(cubridtray PROPERTIES COMPILE_FLAGS "/GF /YuSTDAFX.H")
set_target_properties(cubridtray PROPERTIES OUTPUT_NAME "CUBRID_Service_Tray")
if(MSVC_VERSION LESS 1600)
  # for VS 2008 (9.0) or less version
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
else(MSVC_VERSION LESS 1600)
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif(MSVC_VERSION LESS 1600)


install(TARGETS
  ctrlservice
  cubridservice
  cubridtray
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Application
  )

install(FILES
  ${CMAKE_SOURCE_DIR}/win/3rdparty/dll/jansson64.dll
  DESTINATION ${CUBRID_BINDIR} COMPONENT Library
  RENAME jansson.dll
  )
install(FILES
  ${CMAKE_SOURCE_DIR}/win/3rdparty/dll/libexpat64.dll
  DESTINATION ${CUBRID_BINDIR} COMPONENT Library
  RENAME libexpat.dll
 )
 install(FILES
   ${CMAKE_SOURCE_DIR}/win/3rdparty/lz4/bin/liblz4.dll
   DESTINATION ${CUBRID_BINDIR} COMPONENT Library
 )

# install pdb files for debugging on windows
install(DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
  DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
  FILES_MATCHING PATTERN "*.pdb"
  )
