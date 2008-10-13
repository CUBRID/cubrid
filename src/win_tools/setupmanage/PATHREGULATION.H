// PathRegulation.h: interface for the CPathRegulation class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PATHREGULATION_H__A3353567_0704_486D_AD65_E6D9CF3A6551__INCLUDED_)
#define AFX_PATHREGULATION_H__A3353567_0704_486D_AD65_E6D9CF3A6551__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <wtypes.h>
#include <winnt.h>
#include <winsvc.h>
#include <winuser.h>


struct _path_list_;

typedef struct _path_list_{
	char* sPath;
	bool  bKeep;
	struct _path_list_* pNext;
} UniPath_t, * UniPathPtr_t;


class CPathRegulation  
{
private :
	char* sCurrPath;					// 현재 저장된 Path 정보
	UniPathPtr_t pRootPath;				// Parsing한 Path 정보
	HKEY hRootKey;

	char* sGetPath();					// Registry로 부터 Path 정보를 가져온다.
	void  vSetPath( char* sPath );		// Registry에 Path 정보를 저장한다.
	void  vDivisionItem();				// Path 정보를 Parsing한다.
	void  vDestroyList();				// Path 정보가 Parsing된 List를 제거한다.
	bool  bOpenTheRegistry();			// Registry를 Open한다.
public:
	char installed_path[1024];
	CPathRegulation( char* spath );
	CPathRegulation();
	virtual ~CPathRegulation();

	void vRemovePathInfo();			// CUBRID관련 Path 정보를 제거하여, Path에 적용한다.
};

#endif // !defined(AFX_PATHREGULATION_H__A3353567_0704_486D_AD65_E6D9CF3A6551__INCLUDED_)
