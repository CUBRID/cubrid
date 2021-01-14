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
#if !defined(AFX_SHOWRUNDB_H__1B6628D0_717F_4B33_AE02_498C2FD941F9__INCLUDED_)
#define AFX_SHOWRUNDB_H__1B6628D0_717F_4B33_AE02_498C2FD941F9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ShowRunDB.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CShowRunDB dialog
class CTextProgressCtrl;

class CShowRunDB : public CDialog
{
// Construction
public:
	CShowRunDB(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CShowRunDB)
	enum { IDD = IDD_SHOWRUNDB };
	CButton	m_ok;
	CButton	m_cancel;
	CStatic	m_txt_tempvol;
	CStatic	m_txt_indexvol;
	CStatic	m_txt_genericvol;
	CStatic	m_txt_datavol;
	CTextProgressCtrl	m_tempvol;
	CTextProgressCtrl	m_indexvol;
	CTextProgressCtrl	m_genericvol;
	CTextProgressCtrl	m_datavol;
	CListBox	m_list;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CShowRunDB)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CShowRunDB)
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL OnInitDialog();
	afx_msg void OnSelchangeSrdList();
	afx_msg void OnDisplayDBsize(WPARAM wParam, LPARAM lParam);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

#define		SPACEDB_MSG		10000
//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SHOWRUNDB_H__1B6628D0_717F_4B33_AE02_498C2FD941F9__INCLUDED_)
