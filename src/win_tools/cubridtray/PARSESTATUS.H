// ParseStatus.h: interface for the CParseStatus class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PARSESTATUS_H__D301FBB6_198D_4C9D_B0A7_36164523A485__INCLUDED_)
#define AFX_PARSESTATUS_H__D301FBB6_198D_4C9D_B0A7_36164523A485__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#include "unitray_comm.h"

class CParseStatus  
{
private:
//	char* sCatchResult( char* sCmd );

	DBNAME_Ptr pMakeList( unsigned int dNum, char* sName );
	char* sGetName( char* sStr );
public:
	CParseStatus();
	virtual ~CParseStatus();

//	bool bStatusProcess( DREGNAME_t dProdNum );
	bool bStatusMaster();
	bool bStatusServer();
	bool bStatusUniCAS();
	bool bEasyManagerServer();

	DBNAME_Ptr pGetDBList( );
};

#endif // !defined(AFX_PARSESTATUS_H__D301FBB6_198D_4C9D_B0A7_36164523A485__INCLUDED_)
