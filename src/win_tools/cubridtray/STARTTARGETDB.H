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
#if !defined(AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_)
#define AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// StartTargetDB.h : header file
//


#include "CUBRIDManage.h"


/////////////////////////////////////////////////////////////////////////////
// CStartTargetDB dialog

class CStartTargetDB : public CDialog
{
private:
	CCUBRIDManage* cCUBRID;
// Construction
public:
	CStartTargetDB(CWnd* pParent = NULL);   // standard constructor
	CStartTargetDB( CCUBRIDManage* cCUBRID, CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStartTargetDB)
	enum { IDD = IDD_START_TARGET_DB };
	CButton	m_su_cancel;
	CListBox	m_ListDB;
	CButton	m_su_ok;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStartTargetDB)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStartTargetDB)
	virtual BOOL OnInitDialog();
	afx_msg void OnSuOk();
	afx_msg void OnSuCancel();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_)




