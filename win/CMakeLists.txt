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
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS  "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
else(MSVC_VERSION LESS 1600)
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif(MSVC_VERSION LESS 1600)


set(SETUPMANAGE_SOURCES
  ${WIN_TOOLS_DIR}/setupmanage/SETUPMANAGE.CPP
  ${WIN_TOOLS_DIR}/setupmanage/PATHREGULATION.CPP
  ${WIN_TOOLS_DIR}/setupmanage/stdafx.cpp
  ${CMAKE_BINARY_DIR}/version.rc
  )
set_source_files_properties(${WIN_TOOLS_DIR}/setupmanage/stdafx.cpp
  PROPERTIES
  COMPILE_FLAGS "/Ycstdafx.h"
  )
add_executable(setupmanage ${CUBRIDSERVICE_SOURCES})
set_target_properties(setupmanage PROPERTIES COMPILE_FLAGS "/Yustdafx.h")
target_compile_definitions(setupmanage PRIVATE _CONSOLE)
target_link_libraries(setupmanage LINK_PRIVATE ${WIN_TOOLS_DIR}/setupmanage/UTSKEY.LIB)


install(TARGETS
  ctrlservice
  cubridservice
  cubridtray
  setupmanage
  RUNTIME DESTINATION ${CUBRID_BINDIR}
  )

# install external pre-built DLLs
# TODO: add custom target for external dlls? 
# ref. http://stackoverflow.com/questions/14474659/cmake-how-to-have-a-target-for-copying-files
if(TARGET_PLATFORM_BITS EQUAL 32)
  file(GLOB TARGET_DLLS
    ${CMAKE_SOURCE_DIR}/win/external/dll/*.dll
    ${CMAKE_SOURCE_DIR}/win/external/dll/Win32/*.dll
    )
else(TARGET_PLATFORM_BITS EQUAL 32)
  file(GLOB TARGET_DLLS
    ${CMAKE_SOURCE_DIR}/win/external/dll/*.dll
    ${CMAKE_SOURCE_DIR}/win/external/dll/x64/*.dll
    )
endif(TARGET_PLATFORM_BITS EQUAL 32)

install(FILES
  ${TARGET_DLLS}
  DESTINATION ${CUBRID_BINDIR})

# install pdb files for debugging on windows
install(DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/
  DESTINATION ${CUBRID_BINDIR}
  FILES_MATCHING PATTERN "*.pdb"
  )
