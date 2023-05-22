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

// cubridtray.h : main header file for the UNITRAY application
//

#if !defined(AFX_UNITRAY_H__0286B4FE_E66C_4EF1_BB4E_E3BDA1EE38C1__INCLUDED_)
#define AFX_UNITRAY_H__0286B4FE_E66C_4EF1_BB4E_E3BDA1EE38C1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CUnitrayApp:
// See unitray.cpp for the implementation of this class
//

class CUnitrayApp:public CWinApp
{
public:
  CUnitrayApp ();
  ~CUnitrayApp ();

public:
  BOOL FirstInstance ();
  BOOL bClassRegistered;

public:


// Overrides
  // ClassWizard generated virtual function overrides
  // {{AFX_VIRTUAL(CUnitrayApp)
public:
    virtual BOOL InitInstance ();
  virtual int ExitInstance ();
  // }}AFX_VIRTUAL

// Implementation
  COleTemplateServer m_server;
  // Server object for document creation
  // {{AFX_MSG(CUnitrayApp)
  afx_msg void OnAppAbout ();
  // NOTE - the ClassWizard will add and remove member functions here.
  // DO NOT EDIT what you see in these blocks of generated code !
  // }}AFX_MSG
  DECLARE_MESSAGE_MAP ()};

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg:
//
//
class CAboutDlg:public CDialog
{
private:
  char *sCUBRIDInfo;
  char *sUniCASInfo;
  char *sUniTrayInfo;

  char *sGetReleaseString (char *sPath);
  char *sGetFileInfo (char *sCmd);
public:
    CAboutDlg ();
   ~CAboutDlg ();
  void SetVersion (CString CUBRID_version, CString unicas_version);
  void SetVersion ();
  CString m_CUBRID_version;
  CString m_unicas_version;

// Dialog Data
  // {{AFX_DATA(CAboutDlg)
  enum
  { IDD = IDD_ABOUTBOX };
  CButton m_ok;
//      CButton m_hidden_kingch;
//      CStatic m_txt_CUBRIDversion;
//      CStatic m_txt_unicasversion;
//      CButton m_txt_verlabel;
//      CStatic m_txt_copyrights;
  CStatic m_txt_msg;
  CStatic m_CUBRIDversion;
  CStatic m_unicasversion;
  CStatic m_unitray_Info;
  // }}AFX_DATA

  // ClassWizard generated virtual function overrides
  // {{AFX_VIRTUAL(CAboutDlg)
protected:
    virtual void DoDataExchange (CDataExchange * pDX);	// DDX/DDV support
  // }}AFX_VIRTUAL

// Implementation
protected:
  // {{AFX_MSG(CAboutDlg)
    virtual BOOL OnInitDialog ();
  // }}AFX_MSG
  DECLARE_MESSAGE_MAP ()};
/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UNITRAY_H__0286B4FE_E66C_4EF1_BB4E_E3BDA1EE38C1__INCLUDED_)
