; CLW file contains information for the MFC ClassWizard

[General Info]
Version=1
LastClass=CShowRunDB
LastTemplate=CDialog
NewFileInclude1=#include "stdafx.h"
NewFileInclude2=#include "unitray.h"
ODLFile=unitray.odl
LastPage=0

ClassCount=20
Class1=CUnitrayApp
Class2=CUnitrayDoc
Class3=CUnitrayView
Class4=CMainFrame

ResourceCount=18
Resource1=IDD_DIALOGBAR (English (U.S.))
Class5=CAboutDlg
Resource2=IDR_POPUP_ENGLISH
Class6=CDBStartUp
Resource3=IDD_UCM_DETAIL
Class7=CShutdown
Resource4=IDR_POPUP_KOREAN
Class8=CProperty
Resource5=IDD_SHOWRUNDB
Class9=CShowRunDB
Resource6=IDD_UCP_WAS
Resource7=IDD_UC_PROPERTY
Resource8=IDD_UCP_VAS
Resource9=IDD_START_TARGET_DB
Class10=CUCProperty
Class11=CUCPDetail
Class12=CVas
Class13=CWas
Resource10=IDD_STOPSERVER
Class14=CMonitor
Resource11=IDD_UC_MONITOR
Class15=CUCMDetail
Resource12=IDR_POPUP
Resource13=IDD_DIALOG1
Resource14=IDD_PROPERTY
Class16=CEasyManage
Class17=CStart
Class18=CStopTargetDB
Resource15=IDD_UCP_DETAIL
Resource16=IDD_STOP_TARGET_DB
Class19=CStartTargetDB
Resource17=IDD_STARTSERVER
Class20=CAboutKch
Resource18=IDD_ABOUTBOX

[CLS:CUnitrayApp]
Type=0
HeaderFile=unitray.h
ImplementationFile=unitray.cpp
Filter=N
LastObject=CUnitrayApp
BaseClass=CWinApp
VirtualFilter=AC

[CLS:CUnitrayDoc]
Type=0
HeaderFile=unitrayDoc.h
ImplementationFile=unitrayDoc.cpp
Filter=N

[CLS:CUnitrayView]
Type=0
HeaderFile=unitrayView.h
ImplementationFile=unitrayView.cpp
Filter=C


[CLS:CMainFrame]
Type=0
HeaderFile=MainFrm.h
ImplementationFile=MainFrm.cpp
Filter=T
LastObject=PU_ABOUT
BaseClass=CFrameWnd
VirtualFilter=fWC




[CLS:CAboutDlg]
Type=0
HeaderFile=unitray.h
ImplementationFile=unitray.cpp
BaseClass=CDialog
LastObject=CAboutDlg
Filter=D
VirtualFilter=dWC

[DLG:IDD_ABOUTBOX]
Type=1
Class=CAboutDlg
ControlCount=3
Control1=IDOK,button,1342373889
Control2=IDC_STATIC,static,1342177283
Control3=IDC_ABT_MSG,static,1342313089

[DLG:IDD_STARTSERVER]
Type=1
Class=CDBStartUp
ControlCount=3
Control1=IDC_SU_LIST,listbox,1352728841
Control2=ID_SU_OK,button,1342242817
Control3=ID_SU_CANCEL,button,1342242816

[CLS:CDBStartUp]
Type=0
HeaderFile=DBStartUp.h
ImplementationFile=DBStartUp.cpp
BaseClass=CDialog
Filter=D
LastObject=CDBStartUp
VirtualFilter=dWC

[DLG:IDD_STOPSERVER]
Type=1
Class=CShutdown
ControlCount=3
Control1=IDC_SS_LISTDB,listbox,1352728841
Control2=ID_SS_OK,button,1342242817
Control3=ID_SS_CANCEL,button,1342242816

[CLS:CShutdown]
Type=0
HeaderFile=Shutdown.h
ImplementationFile=Shutdown.cpp
BaseClass=CDialog
Filter=D
LastObject=CShutdown
VirtualFilter=dWC

[DLG:IDD_PROPERTY]
Type=1
Class=CProperty
ControlCount=10
Control1=IDC_PRO_LIST,listbox,1352728833
Control2=IDC_PRO_PERMANENT,button,1342242819
Control3=ID_PRO_OK,button,1342242817
Control4=ID_PRO_CANCEL,button,1342242816
Control5=IDC_PRO_DBPATH,edit,1350568064
Control6=IDC_PRO_TXT_DBPATH,static,1342308352
Control7=IDC_PRO_HOST,edit,1350568064
Control8=IDC_PRO_TXT_HOST,static,1342308352
Control9=IDC_PRO_LOGPATH,edit,1350568064
Control10=IDC_PRO_TXT_LOGPATH,static,1342308352

[CLS:CProperty]
Type=0
HeaderFile=Property.h
ImplementationFile=Property.cpp
BaseClass=CDialog
Filter=D
LastObject=IDC_PRO_LIST
VirtualFilter=dWC

[DLG:IDD_SHOWRUNDB]
Type=1
Class=CShowRunDB
ControlCount=11
Control1=IDC_SRD_LIST,listbox,1352728833
Control2=IDOK,button,1342242817
Control3=IDCANCEL,button,1342242816
Control4=IDC_SRD_TXT_GPRO,static,1342308352
Control5=IDC_SRD_TXT_DPRO,static,1342308352
Control6=IDC_SRD_TXT_IPRO,static,1342308352
Control7=IDC_SRD_TXT_TPRO,static,1342308352
Control8=IDC_SRD_DPRO,msctls_progress32,1350565888
Control9=IDC_SRD_GPRO,msctls_progress32,1350565888
Control10=IDC_SRD_IPRO,msctls_progress32,1350565888
Control11=IDC_SRD_TPRO,msctls_progress32,1350565888

[CLS:CShowRunDB]
Type=0
HeaderFile=ShowRunDB.h
ImplementationFile=ShowRunDB.cpp
BaseClass=CDialog
Filter=D
VirtualFilter=dWC
LastObject=IDC_SRD_DPRO

[DLG:IDD_UC_PROPERTY]
Type=1
Class=CUCProperty
ControlCount=11
Control1=IDC_UCP_LIST,SysListView32,1350664197
Control2=IDC_UCP_SMID,edit,1350631552
Control3=IDC_UCP_NEW,button,1342242816
Control4=IDC_UCP_UPDATE,button,1342242816
Control5=IDC_UCP_DELETE,button,1342242816
Control6=IDC_UCP_APPLY,button,1342242816
Control7=IDC_UCP_CANCEL,button,1342242817
Control8=IDC_UCP_TXT_SMID,static,1342308352
Control9=IDC_UCP_TXT_LOGFILE,static,1342308352
Control10=IDC_UCP_LOGFILE,edit,1350566016
Control11=IDC_UCP_MASTERINFO,button,1342177287

[DLG:IDD_UCP_DETAIL]
Type=1
Class=CUCPDetail
ControlCount=42
Control1=IDC_UCPD_BNAME,edit,1350631552
Control2=IDC_UCPD_S_ONOFF,button,1342246915
Control3=IDC_UCPD_STYPE,combobox,1344339970
Control4=IDC_UCPD_MORE,button,1342242816
Control5=IDC_UCPD_PORT,edit,1350631552
Control6=IDC_UCPD_SMID,edit,1350631552
Control7=IDC_UCPD_AUTOADD,button,1342242819
Control8=IDC_UCPD_MIN,edit,1350631552
Control9=IDC_UCPD_MAX,edit,1350631552
Control10=IDC_UCPD_SIZELIMIT,edit,1350631552
Control11=IDC_UCPD_LOGBACKUP_ONOFF,button,1342246915
Control12=IDC_UCPD_SQLT_ONOFF,button,1342246915
Control13=IDC_UCPD_SQLTIME,edit,1350631552
Control14=IDC_UCPD_ALOG_ONOFF,button,1342246915
Control15=IDC_UCPD_SECURITY_ONOFF,button,1342246915
Control16=IDC_UCPD_TIMETOKILL,edit,1350631552
Control17=IDC_UCPD_STIMEOUT,edit,1350631552
Control18=IDC_UCPD_QUEUESIZE,edit,1350631552
Control19=IDC_UCPD_OK,button,1342242816
Control20=IDC_UCPD_CANCEL,button,1342242817
Control21=IDC_UCPD_TXT_BNAME,static,1342308352
Control22=IDC_UCPD_TXT_PORT,static,1342308352
Control23=IDC_UCPD_TXT_STYPE,static,1342308352
Control24=IDC_UCPD_TXT_CP,button,1342177287
Control25=IDC_UCPD_TXT_SMID,static,1342308352
Control26=IDC_UCPD_TXT_AS,button,1342177287
Control27=IDC_UCPD_TXT_MIN,static,1342308352
Control28=IDC_UCPD_TXT_MAX,static,1342308352
Control29=IDC_UCPD_TXT_SIZELIMIT,static,1342308352
Control30=IDC_UCPD_TXT_LI,button,1342177287
Control31=IDC_UCPD_TXT_LOGDIR,static,1342308352
Control32=IDC_UCPD_LOGDIR,edit,1350566016
Control33=IDC_UCPD_TXT_SQLTIME,static,1342308352
Control34=IDC_UCPD_TXT_SC,button,1342177287
Control35=IDC_UCPD_TXT_SECURIY_FILE,static,1342308352
Control36=IDC_UCPD_SECURIY_FILE,edit,1350566016
Control37=IDC_UCPD_TXT_ETC,button,1342177287
Control38=IDC_UCPD_TXT_ENV_FILE,static,1342308352
Control39=IDC_UCPD_ENV_FILE,edit,1350566016
Control40=IDC_UCPD_TXT_TIMETOKILL,static,1342308352
Control41=IDC_UCPD_TXT_STIMEOUT,static,1342308352
Control42=IDC_UCPD_TXT_QUEUESIZE,static,1342308352

[DLG:IDD_UCP_WAS]
Type=1
Class=CWas
ControlCount=15
Control1=ID_WAS_OK,button,1342242817
Control2=IDC_WAS_CANCEL,button,1342242816
Control3=IDC_WAS_SESSION,button,1342242819
Control4=IDC_WAS_COOKIE,button,1342242819
Control5=IDC_WAS_TXT_SROOT,static,1342308352
Control6=IDC_WAS_SROOT,edit,1350631552
Control7=IDC_WAS_TXT_ERRLOG,static,1342308352
Control8=IDC_WAS_ERRLOG,combobox,1344339970
Control9=IDC_WAS_TXT_FU,static,1342308352
Control10=IDC_WAS_FU,edit,1350631552
Control11=IDC_WAS_TXT_DELIMITER,static,1342308352
Control12=IDC_WAS_DELIMITER,edit,1350631552
Control13=IDC_WAS_TRIM,button,1342242819
Control14=IDC_WAS_CHECKOID,button,1342242819
Control15=IDC_WAS_ENC_ONOFF,button,1342242819

[DLG:IDD_UCP_VAS]
Type=1
Class=CVas
ControlCount=15
Control1=IDC_VAS_OK,button,1342242817
Control2=IDC_VAS_CANCEL,button,1342242816
Control3=IDC_VAS_SESSION,button,1342242819
Control4=IDC_VAS_TXT_SROOT,static,1342308352
Control5=IDC_VAS_SROOT,edit,1350631552
Control6=IDC_VAS_TXT_ERRLOG,static,1342308352
Control7=IDC_VAS_ERRLOG,combobox,1344339970
Control8=IDC_VAS_TXT_FU,static,1342308352
Control9=IDC_VAS_FU,edit,1350631552
Control10=IDC_VAS_CHECKOID,button,1342242819
Control11=IDC_VAS_TXT_CSIZE,static,1342308352
Control12=IDC_VAS_CSIZE,edit,1350631552
Control13=IDC_VAS_TXT_PGAP,static,1342308352
Control14=IDC_VAS_PGAP,edit,1350631552
Control15=IDC_VAS_ENC_ONOFF,button,1342242819

[CLS:CUCProperty]
Type=0
HeaderFile=UCProperty.h
ImplementationFile=UCProperty.cpp
BaseClass=CDialog
Filter=D
VirtualFilter=dWC
LastObject=IDC_UCP_CANCEL

[CLS:CUCPDetail]
Type=0
HeaderFile=UCPDetail.h
ImplementationFile=UCPDetail.cpp
BaseClass=CDialog
Filter=D
LastObject=CUCPDetail
VirtualFilter=dWC

[CLS:CVas]
Type=0
HeaderFile=Vas.h
ImplementationFile=Vas.cpp
BaseClass=CDialog
Filter=D
VirtualFilter=dWC
LastObject=CVas

[CLS:CWas]
Type=0
HeaderFile=Was.h
ImplementationFile=Was.cpp
BaseClass=CDialog
Filter=D
VirtualFilter=dWC
LastObject=CWas

[DLG:IDD_UC_MONITOR]
Type=1
Class=CMonitor
ControlCount=4
Control1=IDC_UCM_LIST,SysListView32,1350664197
Control2=ID_UCM_END,button,1342242817
Control3=ID_UCM_MONITOR,button,1342242816
Control4=ID_UCM_SERVICE_ONOFF,button,1342242816

[MNU:IDR_POPUP]
Type=1
Class=?
Command1=PU_ABOUT
Command2=PU_STARTSERVER
Command3=PU_STOPSERVER
Command4=PU_CREATEDB
Command5=PU_LISTRUNDB
Command6=PU_PROPERTY
Command7=PU_UNICAS_STARTUP
Command8=PU_UNICAS_RESTARTUP
Command9=PU_UNICAS_SHUTDOWN
Command10=PU_UNICAS_STARTNAMED
Command11=PU_UNICAS_MONITOR
Command12=PU_UNICASPRO
Command13=PU_STOP
CommandCount=13

[CLS:CMonitor]
Type=0
HeaderFile=monitor.h
ImplementationFile=monitor.cpp
BaseClass=CDialog
LastObject=CMonitor
Filter=D
VirtualFilter=dWC

[DLG:IDD_UCM_DETAIL]
Type=1
Class=CUCMDetail
ControlCount=5
Control1=ID_UCMD_END,button,1342242817
Control2=IDC_UCMD_LIST,SysListView32,1350631429
Control3=ID_UCMD_ADD,button,1342242816
Control4=ID_UCMD_DROP,button,1342242816
Control5=ID_UCMD_RESTART,button,1342242816

[CLS:CUCMDetail]
Type=0
HeaderFile=UCMDetail.h
ImplementationFile=UCMDetail.cpp
BaseClass=CDialog
Filter=D
LastObject=CUCMDetail
VirtualFilter=dWC

[MNU:IDR_POPUP_ENGLISH]
Type=1
Class=?
Command1=PU_ABOUT
Command2=PU_EASY_STARTUP
Command3=PU_EASY_STOP
Command4=PU_UNITOOL_EMANAGER
Command5=PU_STOP
CommandCount=5

[MNU:IDR_POPUP_KOREAN]
Type=1
Class=?
Command1=PU_ABOUT
Command2=PU_EASY_STARTUP
Command3=PU_EASY_STOP
Command4=PU_UNITOOL_EMANAGER
Command5=PU_STOP
CommandCount=5

[DLG:IDD_DIALOG1]
Type=1
Class=CEasyManage
ControlCount=24
Control1=IDCANCEL,button,1342242816
Control2=IDC_STATIC,button,1342177287
Control3=IDC_STATIC,button,1342177287
Control4=IDC_BUTTON1,button,1342242816
Control5=IDC_BUTTON2,button,1342242816
Control6=IDC_LIST1,SysListView32,1350631425
Control7=IDC_LIST2,SysListView32,1350631425
Control8=IDC_BUTTON4,button,1342242816
Control9=IDC_BUTTON5,button,1342242816
Control10=IDC_BUTTON6,button,1342242816
Control11=IDC_BUTTON7,button,1342242816
Control12=IDC_BUTTON8,button,1342242816
Control13=IDC_BUTTON9,button,1342242816
Control14=IDC_BUTTON10,button,1342242816
Control15=IDC_BUTTON11,button,1342242816
Control16=IDC_BUTTON12,button,1342242816
Control17=IDC_BUTTON13,button,1342242816
Control18=IDC_BUTTON14,button,1342242816
Control19=IDC_BUTTON15,button,1342242816
Control20=IDC_BUTTON16,button,1342242816
Control21=IDC_BUTTON17,button,1342242816
Control22=IDC_BUTTON18,button,1342242816
Control23=IDC_BUTTON19,button,1342242816
Control24=IDC_EDIT1,edit,1350633668

[CLS:CEasyManage]
Type=0
HeaderFile=EasyManage.h
ImplementationFile=EasyManage.cpp
BaseClass=CDialog
Filter=D
LastObject=PU_STARTSERVER

[CLS:CStart]
Type=0
HeaderFile=Start.h
ImplementationFile=Start.cpp
BaseClass=CDialog
Filter=D
LastObject=CStart

[DLG:IDD_STOP_TARGET_DB]
Type=1
Class=CStopTargetDB
ControlCount=3
Control1=IDC_SS_LISTDB,listbox,1352728841
Control2=ID_SS_OK,button,1342242817
Control3=ID_SS_CANCEL,button,1342242816

[CLS:CStopTargetDB]
Type=0
HeaderFile=StopTargetDB.h
ImplementationFile=StopTargetDB.cpp
BaseClass=CDialog
Filter=D
LastObject=CStopTargetDB
VirtualFilter=dWC

[DLG:IDD_START_TARGET_DB]
Type=1
Class=CStartTargetDB
ControlCount=3
Control1=IDC_SU_LIST,listbox,1352728841
Control2=ID_SU_OK,button,1342242817
Control3=ID_SU_CANCEL,button,1342242816

[CLS:CStartTargetDB]
Type=0
HeaderFile=StartTargetDB.h
ImplementationFile=StartTargetDB.cpp
BaseClass=CDialog
Filter=D
LastObject=CStartTargetDB
VirtualFilter=dWC

[DLG:IDD_DIALOGBAR (English (U.S.))]
Type=1
Class=?
ControlCount=1
Control1=IDC_STATIC,static,1342308352

[CLS:CAboutKch]
Type=0
HeaderFile=AboutKch.h
ImplementationFile=AboutKch.cpp
BaseClass=CDialog
Filter=D
LastObject=CAboutKch

