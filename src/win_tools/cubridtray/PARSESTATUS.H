/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
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
