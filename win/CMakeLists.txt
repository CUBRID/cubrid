
if(NOT WIN32)
  return()
endif(NOT WIN32)


set(CTRLSERVICE_SOURCES
  ${WIN_TOOLS_DIR}/ctrlservice/ctrlservice.cpp
  ${WIN_TOOLS_DIR}/ctrlservice/stdafx.cpp
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
  )
set_source_files_properties(${WIN_TOOLS_DIR}/cubridservice/stdafx.cpp
  PROPERTIES
  COMPILE_FLAGS "/Ycstdafx.h"
  )
add_executable(cubridservice ${CUBRIDSERVICE_SOURCES})
set_target_properties(cubridservice PROPERTIES COMPILE_FLAGS "/Yustdafx.h")
target_compile_definitions(cubridservice PRIVATE _CONSOLE)


set(CUBRIDTRAY_SOURCES
  ${WIN_TOOLS_DIR}/cubridtray/ArchiveLog.cpp
  ${WIN_TOOLS_DIR}/cubridtray/CASManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/CommonMethod.cpp
  ${WIN_TOOLS_DIR}/cubridtray/CUBRIDManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/CUBRIDtray.cpp
  ${WIN_TOOLS_DIR}/cubridtray/DBStartUp.cpp
  ${WIN_TOOLS_DIR}/cubridtray/env.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Filename.cpp
  ${WIN_TOOLS_DIR}/cubridtray/folder_dialog.cpp
  ${WIN_TOOLS_DIR}/cubridtray/lang.cpp
  ${WIN_TOOLS_DIR}/cubridtray/MainFrm.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Manager.cpp
  ${WIN_TOOLS_DIR}/cubridtray/ManageRegistry.cpp
  ${WIN_TOOLS_DIR}/cubridtray/ntray.cpp
  ${WIN_TOOLS_DIR}/cubridtray/ORDBList.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Process.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Property.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Redirect.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Shutdown.cpp
  ${WIN_TOOLS_DIR}/cubridtray/StartTargetDB.cpp
  ${WIN_TOOLS_DIR}/cubridtray/StdAfx.cpp
  ${WIN_TOOLS_DIR}/cubridtray/StopTargetDB.cpp
  ${WIN_TOOLS_DIR}/cubridtray/ToolManage.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCconf.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCMDetail.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCMInfo.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCPDetail.cpp
  ${WIN_TOOLS_DIR}/cubridtray/UCProperty.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Vas.cpp
  ${WIN_TOOLS_DIR}/cubridtray/Was.cpp
  ${WIN_TOOLS_DIR}/cubridtray/cubridtray.odl
  ${WIN_TOOLS_DIR}/cubridtray/cubridtray.rc
  )
set_source_files_properties(${WIN_TOOLS_DIR}/cubridtray/stdafx.cpp
  PROPERTIES
  COMPILE_FLAGS "/Ycstdafx.h"
  )
set(CMAKE_MFC_FLAG 2)
add_executable(cubridtray WIN32 ${CUBRIDTRAY_SOURCES})
# TODO: remove version.h in win/ directory
target_include_directories(cubridtray PRIVATE ${CMAKE_SOURCE_DIR}/win)
set_target_properties(cubridtray PROPERTIES COMPILE_FLAGS "/GF /Yustdafx.h")
set_target_properties(cubridtray PROPERTIES OUTPUT_NAME "CUBRID_Service_Tray")
if(MSVC_VERSION LESS 1600)
  # for VS 2008 (9.0) or less version
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS  "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
else(MSVC_VERSION LESS 1600)
  set_target_properties(cubridtray PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif(MSVC_VERSION LESS 1600)


set(SETUPMANAGE_SOURCES
  ${WIN_TOOLS_DIR}/setupmanage/setupmanage.cpp
  ${WIN_TOOLS_DIR}/setupmanage/PATHREGULATION.CPP
  ${WIN_TOOLS_DIR}/setupmanage/stdafx.cpp
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

# install pdb files for debugging on windows
install(DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/
  DESTINATION ${CUBRID_BINDIR}
  FILES_MATCHING PATTERN "*.pdb"
  )
