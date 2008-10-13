#include <StdAfx.h>
#include <afxtempl.h>
#include "UCMInfo.h"
#include "message.h"

/////////////////////////////////////////////////////////////////////////////////////////
//
//
CUCMInfos::CUCMInfos()
{
}

CUCMInfos::~CUCMInfos()
{
CUCMInfo	*ucminfo;

	while (!m_List.IsEmpty()) {
		ucminfo = (CUCMInfo *)m_List.RemoveHead();
		delete ucminfo;
	}

}

void CUCMInfos::Parse(CString data)
{
CUCMInfo	*ucminfo;
CString		tmpStr;
char		*tmp;
int			index;

	ucminfo = NULL;

	data.TrimLeft();
	data.TrimRight();
	data.MakeLower();

	for (int i = 0; i < 5; i++) {
		index = data.Find('\n');
		tmpStr = data.Left(index);
		data = data.Mid(index+1);
		if (i == 1) {
			tmp = strtok((char *)LPCSTR(tmpStr), ":");
			tmp = strtok(NULL, ",");
			m_job_queue = tmp;
			m_job_queue.TrimLeft(); m_job_queue.TrimRight();
		}
	}

	do {
		data.TrimLeft(); data.TrimRight();
		if (data.IsEmpty()) break;
		index = data.Find('\n');
		if (index == -1) {
			tmpStr = data;
		} else {
			tmpStr = data.Left(index);
			data = data.Mid(index+1);
		}
		ucminfo = new CUCMInfo();
		ucminfo->Parse(tmpStr);
		m_List.AddTail(ucminfo);
	} while (index != -1);
}


/////////////////////////////////////////////////////////////////////////////////////////
//
//
CUCMInfo::CUCMInfo()
{
	m_id.Empty();
	m_pid.Empty();
	m_c.Empty();
	m_port.Empty();
	m_status.Empty();
	m_lastaccesstime.Empty();
	m_action.Empty();
}

CUCMInfo::~CUCMInfo()
{
}

void CUCMInfo::Parse(CString data)
{
char	*tmp;

	tmp = strtok((char *)LPCSTR(data), " ");
	if (!tmp) return;
	m_id = tmp;
	m_id.TrimLeft(); m_id.TrimRight();
	
	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_pid = tmp;
	m_pid.TrimLeft(); m_pid.TrimRight();

	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_c = tmp;
	m_c.TrimLeft(); m_c.TrimRight();

	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_port = tmp;
	m_port.TrimLeft(); m_port.TrimRight();

	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_status = tmp;
	m_status.TrimLeft(); m_status.TrimRight();

	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_lastaccesstime = tmp;
	m_lastaccesstime.TrimLeft(); m_lastaccesstime.TrimRight();

	tmp = strtok(NULL, " ");
	if (!tmp) return;
	m_action = tmp;
	m_action.TrimLeft(); m_action.TrimRight();
}


