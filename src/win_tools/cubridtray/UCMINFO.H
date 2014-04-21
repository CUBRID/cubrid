#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UCMInfo.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// Unicas.conf

class CUCMInfo {
public:
	CUCMInfo();
	~CUCMInfo();
	void Parse(CString data);


public:		// common
	CString		m_id;
	CString		m_pid;
	CString		m_c;
	CString		m_port;
	CString		m_status;
	CString		m_lastaccesstime;
	CString		m_action;

};

class CUCMInfos {
public:
	CUCMInfos();
	~CUCMInfos();
	void	Parse(CString data);

public:
//	CString		m_broker;
	CString		m_job_queue;
	CPtrList	m_List;
};
