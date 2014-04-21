#include "resource.h"       // main symbols

class CTrayApp : public CWinApp
{
public:
	bool Get_DisplayLog();
	bool Get_AutoStop();
	bool Get_AutoStart();
	void Set_DisplayLog(bool);
	void Set_AutoStop(bool);
	void Set_AutoStart(bool);
	CTrayApp();

protected:
	bool m_bDisplayLog;
	bool m_bSvrAutostop;
	bool m_bSvrAutostart;

protected:
	//{{AFX_VIRTUAL(CTrayApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

	//{{AFX_MSG(CTrayApp)
	afx_msg void OnAppAbout();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  BOOL bDisplayingAboutBox;
};

