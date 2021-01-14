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
#if !defined(AFX_ORDBLIST_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_)
#define AFX_ORDBLIST_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ORDBList.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// ORDBList

class CDBInfo {
public:
	CDBInfo();
	~CDBInfo();

public:
	CString		m_dbname;
	CString		m_dbpath;
	CString		m_host;
	CString		m_logpath;
	bool		m_permanent;
};

class CORDBList {
public:
	CORDBList();
	~CORDBList();
	bool ReadDBInfo();
	bool ReReadDBInfo();
	bool WriteDBInfo();
	bool GetPermanentField(CString dbname);
	void SetPermanentField(CString dbname, bool permanent);

public:
	CTime		m_LastTime;
	CPtrList	m_List;
	char	m_ordblist[100];
	char	m_ordbinfo[100];
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ORDBLIST_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_)
