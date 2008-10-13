// Manager.h: interface for the CEasyManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)
#define AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#include "ems.h"
//#include "OptionDialog.h"	// Added by ClassView
//#include "LogDialog.h"
//#include "resource.h"       // main symbols

class CEasyManager  
{
private:
	bool bCheckEMSAuto();
	bool bCheckEMSJS();

public:
	bool bEasyManagerServerCheckOnly();
	CEasyManager();
	virtual ~CEasyManager();

	bool bInstallStatus();

	bool bCheckEasyManagerServer();

	bool bStartEasyManagerServer();
	bool bStopEasyManagerServer();
};

#endif // !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)



