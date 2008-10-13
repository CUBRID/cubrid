#if !defined(__ENVIRONMENT)

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// env.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// 

class CEnv 
{
public:
	CString	m_cubrid_driver;
	CString	m_cubrid;

	CString	m_cubrid_databases_driver;
	CString m_cubrid_databases;

	CString	m_unicas_driver;
	CString m_unicas;

	CString	m_errmsg;

public:
	CEnv();
	bool CheckOS();
	bool GetEnviornment();
	CString GetCUBRID();
	CString GetCUBRID_DATABASES();
	CString GetUniCAS();
	bool CheckCUBRID();
	bool CheckUniCAS();
	void SetCUBRIDEnvVar();
	CString GetErrMsg();
};

#endif