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
#if !defined(AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_)
#define AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// StopTargetDB.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CStopTargetDB dialog

#include "CUBRIDManage.h"


class CStopTargetDB : public CDialog
{
private:
	CCUBRIDManage* cCUBRID;

// Construction
public:
	CStopTargetDB( CCUBRIDManage* cCUBRID, CWnd* pParent = NULL);   // standard constructor
	CStopTargetDB( CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStopTargetDB)
	enum { IDD = IDD_STOP_TARGET_DB };
	CButton	m_ss_cancel;
	CListBox	m_ListDB;
	CButton	m_ss_ok;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStopTargetDB)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStopTargetDB)
	afx_msg void OnSsOk();
	afx_msg void OnSsCancel();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_)
