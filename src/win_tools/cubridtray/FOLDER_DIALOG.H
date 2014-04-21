#if !defined(AFX_MYFD_H__F9CB9441_F91B_11D1_8610_0040055C08D9__INCLUDED_)
#define AFX_MYFD_H__F9CB9441_F91B_11D1_8610_0040055C08D9__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// MyFD.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CFolderDialog dialog

class CFolderDialog : public CFileDialog
{
	DECLARE_DYNAMIC(CFolderDialog)

public:
	static WNDPROC m_wndProc;
	virtual void OnInitDone( );
	CString* m_pPath;
	CFolderDialog(CString* pPath);

protected:
	//{{AFX_MSG(CFolderDialog)
		// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MYFD_H__F9CB9441_F91B_11D1_8610_0040055C08D9__INCLUDED_)
