// ToolManage.h: interface for the CUniToolManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)
#define AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


//#include "unitray_comm.h"


class CUniToolManage  
{
public:
	CUniToolManage();
	virtual ~CUniToolManage();

	char * cubridmanager_path;

	bool bStartVSQL();
	bool bStartEasyManage();

	bool bCheckInstallVSQL();
	bool bCheckInstallEasyManage();
};

#endif // !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)




