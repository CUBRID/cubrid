// CASManage.h: interface for the CUniCASManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
#define AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CUniCASManage  
{
private:
	bool bUNICAS;
public:
	CUniCASManage();
	virtual ~CUniCASManage();

	bool bStatusUniCAS();
	bool bCheckUniCAS();

	bool bStopUniCAS();
	bool bStartUniCAS();

	bool bRestartUniCAS();

	bool bInstallStatus();
};

#endif // !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
