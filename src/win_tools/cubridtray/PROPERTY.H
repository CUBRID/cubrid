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
#if !defined(AFX_PROPERTY_H__EE55259A_D681_433A_855E_495212F3D683__INCLUDED_)
#define AFX_PROPERTY_H__EE55259A_D681_433A_855E_495212F3D683__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Property.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CProperty dialog
class CORDBList;

class CProperty : public CDialog
{
public:
	CORDBList	*m_OrdbList;

// Construction
public:
	CProperty(CORDBList *ordblist, CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CProperty)
	enum { IDD = IDD_PROPERTY };
	CStatic	m_txt_logpath;
	CStatic	m_txt_host;
	CStatic	m_txt_dbpath;
	CButton	m_pro_cancel;
	CListBox	m_list;
	CButton	m_pro_ok;
	CButton	m_permanent;
	CEdit	m_logpath;
	CEdit	m_host;
	CEdit	m_dbpath;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CProperty)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CProperty)
	afx_msg void OnProCancel();
	afx_msg void OnProOk();
	virtual BOOL OnInitDialog();
	afx_msg void OnProPermanent();
	afx_msg void OnSelchangeProList();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPERTY_H__EE55259A_D681_433A_855E_495212F3D683__INCLUDED_)
