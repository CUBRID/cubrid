# Microsoft Developer Studio Project File - Name="unitray" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=unitray - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "unitray.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "unitray.mak" CFG="unitray - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "unitray - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "unitray - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "unitray - Win32 Release"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /Yu"stdafx.h" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x412 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x412 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 /nologo /subsystem:windows /machine:I386 /out:"Release/CUBRID_Service_Tray.exe"

!ELSEIF  "$(CFG)" == "unitray - Win32 Debug"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /D "_X86_" /FR /Yu"stdafx.h" /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x412 /d "_DEBUG" /d "_AFXDLL"
# ADD RSC /l 0x412 /d "_DEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 /nologo /subsystem:windows /debug /machine:I386 /out:"Debug/CUBRID_Service_Tray.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "unitray - Win32 Release"
# Name "unitray - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\ArchiveLog.cpp
# End Source File
# Begin Source File

SOURCE=.\CommonMethod.cpp
# End Source File
# Begin Source File

SOURCE=.\DBStartUp.cpp
# End Source File
# Begin Source File

SOURCE=.\EasyManager.cpp
# End Source File
# Begin Source File

SOURCE=.\env.cpp
# End Source File
# Begin Source File

SOURCE=.\Filename.cpp
# End Source File
# Begin Source File

SOURCE=.\folder_dialog.cpp
# End Source File
# Begin Source File

SOURCE=.\lang.cpp
# End Source File
# Begin Source File

SOURCE=.\MainFrm.cpp
# End Source File
# Begin Source File

SOURCE=.\ManageRegistry.cpp
# End Source File
# Begin Source File

SOURCE=.\Monitor.cpp
# End Source File
# Begin Source File

SOURCE=.\ntray.cpp
# End Source File
# Begin Source File

SOURCE=.\ORDBList.cpp
# End Source File
# Begin Source File

SOURCE=.\Process.cpp
# End Source File
# Begin Source File

SOURCE=.\Property.cpp
# End Source File
# Begin Source File

SOURCE=.\Redirect.cpp
# End Source File
# Begin Source File

SOURCE=.\ShowRunDB.cpp
# End Source File
# Begin Source File

SOURCE=.\Shutdown.cpp
# End Source File
# Begin Source File

SOURCE=.\StartTargetDB.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# Begin Source File

SOURCE=.\StopTargetDB.cpp
# End Source File
# Begin Source File

SOURCE=.\TextProgressCtrl.cpp
# End Source File
# Begin Source File

SOURCE=.\UCconf.cpp
# End Source File
# Begin Source File

SOURCE=.\UCMDetail.cpp
# End Source File
# Begin Source File

SOURCE=.\UCMInfo.cpp
# End Source File
# Begin Source File

SOURCE=.\UCPDetail.cpp
# End Source File
# Begin Source File

SOURCE=.\UCProperty.cpp
# End Source File
# Begin Source File

SOURCE=.\UniCASManage.cpp
# End Source File
# Begin Source File

SOURCE=.\CUBRIDManage.cpp
# End Source File
# Begin Source File

SOURCE=.\UniToolManage.cpp
# End Source File
# Begin Source File

SOURCE=.\unitray.cpp
# End Source File
# Begin Source File

SOURCE=.\unitray.odl
# End Source File
# Begin Source File

SOURCE=.\unitray.rc
# End Source File
# Begin Source File

SOURCE=.\Vas.cpp
# End Source File
# Begin Source File

SOURCE=.\Was.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\ArchiveLog.h
# End Source File
# Begin Source File

SOURCE=.\CommonMethod.h
# End Source File
# Begin Source File

SOURCE=.\DBStartUp.h
# End Source File
# Begin Source File

SOURCE=.\EasyManager.h
# End Source File
# Begin Source File

SOURCE=.\env.h
# End Source File
# Begin Source File

SOURCE=.\Filename.h
# End Source File
# Begin Source File

SOURCE=.\folder_dialog.h
# End Source File
# Begin Source File

SOURCE=.\lang.h
# End Source File
# Begin Source File

SOURCE=.\MainFrm.h
# End Source File
# Begin Source File

SOURCE=.\ManageRegistry.h
# End Source File
# Begin Source File

SOURCE=.\message.h
# End Source File
# Begin Source File

SOURCE=.\Monitor.h
# End Source File
# Begin Source File

SOURCE=.\ntray.h
# End Source File
# Begin Source File

SOURCE=.\ORDBList.h
# End Source File
# Begin Source File

SOURCE=.\Process.h
# End Source File
# Begin Source File

SOURCE=.\Property.h
# End Source File
# Begin Source File

SOURCE=.\Redirect.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\ShowRunDB.h
# End Source File
# Begin Source File

SOURCE=.\Shutdown.h
# End Source File
# Begin Source File

SOURCE=.\StartTargetDB.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# Begin Source File

SOURCE=.\StopTargetDB.h
# End Source File
# Begin Source File

SOURCE=.\TextProgressCtrl.h
# End Source File
# Begin Source File

SOURCE=.\uc_admin.h
# End Source File
# Begin Source File

SOURCE=.\UCconf.h
# End Source File
# Begin Source File

SOURCE=.\UCMDetail.h
# End Source File
# Begin Source File

SOURCE=.\UCMInfo.h
# End Source File
# Begin Source File

SOURCE=.\UCPDetail.h
# End Source File
# Begin Source File

SOURCE=.\UCProperty.h
# End Source File
# Begin Source File

SOURCE=.\UniCASManage.h
# End Source File
# Begin Source File

SOURCE=.\CUBRIDManage.h
# End Source File
# Begin Source File

SOURCE=.\UniToolManage.h
# End Source File
# Begin Source File

SOURCE=.\unitray.h
# End Source File
# Begin Source File

SOURCE=.\Vas.h
# End Source File
# Begin Source File

SOURCE=.\Was.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\res\bitmap1.bmp
# End Source File
# Begin Source File

SOURCE=.\res\cursor1.cur
# End Source File
# Begin Source File

SOURCE=.\res\ico00001.ico
# End Source File
# Begin Source File

SOURCE=.\res\ico00002.ico
# End Source File
# Begin Source File

SOURCE=.\res\ico00003.ico
# End Source File
# Begin Source File

SOURCE=.\res\icon1.ico
# End Source File
# Begin Source File

SOURCE=.\RES\icon2.ico
# End Source File
# Begin Source File

SOURCE=.\res\idr_main.ico
# End Source File
# Begin Source File

SOURCE=.\res\network.ico
# End Source File
# Begin Source File

SOURCE=.\res\smile1.ico
# End Source File
# Begin Source File

SOURCE=.\res\unitray.ico
# End Source File
# Begin Source File

SOURCE=.\res\unitray.rc2
# End Source File
# Begin Source File

SOURCE=.\res\unitrayDoc.ico
# End Source File
# End Group
# Begin Group "ems Source"

# PROP Default_Filter "*.cpp"
# End Group
# Begin Source File

SOURCE=.\RES\32_off.PNG
# End Source File
# Begin Source File

SOURCE=.\ReadMe.txt
# End Source File
# Begin Source File

SOURCE=.\unitray.reg
# End Source File
# End Target
# End Project
