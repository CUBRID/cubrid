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
// ParseDB.h: interface for the CParseDB class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_)
#define AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000




class CParseDB  
{
private :
	bool bReflashDBList();
	bool bReflashStartDB();
public:
	CParseDB();
	virtual ~CParseDB();

};

#endif // !defined(AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_)
