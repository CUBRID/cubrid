// unitrayDlg.h : header file
//

#if !defined(AFX_UNITRAYDLG_H__DE84CD38_DB2D_49F0_A688_65B7168154AA__INCLUDED_)
#define AFX_UNITRAYDLG_H__DE84CD38_DB2D_49F0_A688_65B7168154AA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDlg dialog

class CUnitrayDlg : public CDialog
{
// Construction
public:
	CUnitrayDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	//{{AFX_DATA(CUnitrayDlg)
	enum { IDD = IDD_UNITRAY_DIALOG };
		// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUnitrayDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CUnitrayDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UNITRAYDLG_H__DE84CD38_DB2D_49F0_A688_65B7168154AA__INCLUDED_)
