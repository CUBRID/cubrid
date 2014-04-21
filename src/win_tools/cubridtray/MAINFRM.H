// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MAINFRM_H__129D7121_1187_495B_AC7F_E9A5C86606EB__INCLUDED_)
#define AFX_MAINFRM_H__129D7121_1187_495B_AC7F_E9A5C86606EB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "ntray.h"
#include "Redirect.h"

class	CAboutDlg;
class   CRedirect;
//class	CDBStartUp;
//class	CShutdown;
class	CProperty;
class	CORDBList;
class	CShowRunDB;
class	CMonitor;
class	CUCProperty;
class	CUCConf;
class	CProcess;
class	CCUBRIDManage;
class	CUniCASManage;
class	CEasyManager;


//class	CUniToolManage;
#include "ToolManage.h"
/////////////////////////////////////////////////////////////////////////////
// children of CRedirect

class CRedir : public CRedirect
{
public:
	void OnChildStarted(LPCSTR lpszCmdLine);
	void OnChildStdOutWrite(LPCSTR lpszBuffer);
	void OnChildStdErrWrite(LPCSTR lpszBuffer);
	void OnChildTerminate();
};

class CListRedirect : public CRedirect
{
public:
	CString	m_data;
	CDialog	*m_dialog;
	short m_gtvol, m_gfvol;
	short m_dtvol, m_dfvol;
	short m_itvol, m_ifvol;
	short m_ttvol, m_tfvol;

public:
	void AnalysisSDB();

protected:
	void OnChildStarted(LPCSTR lpszCmdLine);
	void OnChildStdOutWrite(LPCSTR lpszBuffer);
	void OnChildStdErrWrite(LPCSTR lpszBuffer);
	void OnChildTerminate();
};

class CServerRedirect : public CRedirect
{
public:
	CString	m_DBName;

protected:
	void OnChildStarted(LPCSTR lpszCmdLine);
	void OnChildStdOutWrite(LPCSTR lpszBuffer);
	void OnChildStdErrWrite(LPCSTR lpszBuffer);
	void OnChildTerminate();
};

class CVersionRedirect : public CRedirect
{
public:
	CString m_version;

protected:
	void OnChildStarted(LPCSTR lpszCmdLine);
	void OnChildStdOutWrite(LPCSTR lpszBuffer);
	void OnChildStdErrWrite(LPCSTR lpszBuffer);
	void OnChildTerminate();
};

/////////////////////////////////////////////////////////////////////////////
class CMainFrame : public CFrameWnd
{
public:
	CMainFrame();

protected: // create from serialization only
	DECLARE_DYNCREATE(CMainFrame)

// Attributes
public:
	CProcess		*m_process;
	CTrayNotifyIcon	m_TrayIcon;
	CAboutDlg		*m_AboutDlg;
	CRedirect  		*m_Master;
//	CDBStartUp		*m_DBStartUp;
//	CShutdown		*m_DBShutdown;
	CProperty		*m_Property;
	CORDBList		*m_OrdbList;
	CShowRunDB		*m_ShowRunDB;
	CMonitor		*m_ucmonitor;
	CUCProperty		*m_ucproperty;
	int				m_ServerCnt;
	bool			m_StatusOfUniCAS;
	CUCConf			*m_unicasconf;

	CString			m_CUBRID_version, m_unicas_version;

// Operations
public:
	bool	CheckEnv();

public:
	void	SetTooltipText();
	bool	StartMaster();
	bool	StopMaster();
	void	KillMaster();
	bool	StartServer(char *dbname);
	bool	StopServer(char *dbname);
	bool	StopServer(int index);
	void	KillServer();
	bool	StartUniCAS();
	bool	StopUniCAS();
	void	KillUniCAS();
	void	StartNamed();
	void	StopNamed();
	CString	GetCUBRIDVersion();
	CString	GetUniCASVersion();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMainFrame)
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	//}}AFX_VIRTUAL

// Implementation
public:
	unsigned int m_monitorTimer;
	bool bCUBRIDServiceCheck();
	UINT m_refreshTimer;
	BOOL refreshIcon();
	virtual ~CMainFrame();

/*
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif
*/


private:
	bool bEasyStatus;
	bool bVsqlStatus;
	bool bEasyServerStatus;

	bool bUniCasStartStatus;			// Start할 DB가 있는지
	bool bUniCasStopStatus;				// Stop할 DB가 있는지
	bool bUniCasRestartStatus;

	bool bCUBRIDStatus;
	bool bCUBRIDStartStatus;
	bool bCUBRIDStopStatus;

	bool bEasyServerStartStatus;
	bool bEasyServerStopStatus;

	void vExchangeUniCASStatus( bool bStartStatus );

	CCUBRIDManage* cCUBRID;
	CUniCASManage* cUniCAS;
	CUniToolManage* cTool;
	CEasyManager*   cEasy;

// 2002.10.11 By KingCH
/*
	bool bCheckUniCAS();
	bool bCheckCUBRID();
	bool bCheckUniTool();
	bool bCheckEasyManage();

 */

	
	
// Generated message map functions
protected:
	//{{AFX_MSG(CMainFrame)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnMasterStop();
	afx_msg void OnStopUniTray();
	afx_msg void OnAbout();
	afx_msg void OnStartserver();
	afx_msg void OnStopserver();
	afx_msg void OnUpdateStopserver(CCmdUI* pCmdUI);
	afx_msg void OnUnicasStartup();
	afx_msg void OnUnicasShutdown();
	afx_msg void OnUpdateUnicasStartup(CCmdUI* pCmdUI);
	afx_msg void OnUpdateUnicasShutdown(CCmdUI* pCmdUI);
	afx_msg void OnUpdateStartserver(CCmdUI* pCmdUI);
	afx_msg void OnProperty();
	afx_msg void OnUpdateListserver(CCmdUI* pCmdUI);
	afx_msg void OnUnicasStartNamed();
	afx_msg void OnCreateDB();
	afx_msg void OnListserver();
	afx_msg void OnUnicasProperty();
	afx_msg void OnUnicasMonitor();
	afx_msg void OnUnicasRestartup();
	afx_msg void OnUpdateUnicasRestartup(CCmdUI* pCmdUI);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnUpdateUnicasMonitor(CCmdUI* pCmdUI);
	afx_msg void OnUnitoolEmanager();
	afx_msg void OnUnitoolVsql();
	afx_msg void OnUpdateUnitoolVsql(CCmdUI* pCmdUI);
	afx_msg void OnUpdateUnitoolEmanager(CCmdUI* pCmdUI);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnEasyStop();
	afx_msg void OnUpdateEasyStop(CCmdUI* pCmdUI);
	afx_msg void OnEasyStartup();
	afx_msg void OnUpdateEasyStartup(CCmdUI* pCmdUI);
	afx_msg void OnEasyOption();
	afx_msg void OnUpdateEasyOption(CCmdUI* pCmdUI);
	afx_msg void OnEmsautostartlog();
	afx_msg void OnUpdateEmsautostartlog(CCmdUI* pCmdUI);
	afx_msg void OnEmsjsstartlog();
	afx_msg void OnUpdateEmsjsstartlog(CCmdUI* pCmdUI);
	afx_msg void OnUnicasRestart();
	afx_msg void OnUpdateUnicasRestart(CCmdUI* pCmdUI);
	//}}AFX_MSG
	afx_msg LRESULT OnStopMonitor(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnTrayNotification(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT onReceiveServiceStopMessage(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT onReceiveServiceStartMessage(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};

#define	MAXSERVER	100
/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MAINFRM_H__129D7121_1187_495B_AC7F_E9A5C86606EB__INCLUDED_)
