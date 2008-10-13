// unitrayDoc.h : interface of the CUnitrayDoc class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNITRAYDOC_H__8655D5EC_A8C4_4D96_8DB7_8912A6C198EE__INCLUDED_)
#define AFX_UNITRAYDOC_H__8655D5EC_A8C4_4D96_8DB7_8912A6C198EE__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CUnitrayDoc : public CDocument
{
protected: // create from serialization only
	CUnitrayDoc();
	DECLARE_DYNCREATE(CUnitrayDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUnitrayDoc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CUnitrayDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CUnitrayDoc)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	// Generated OLE dispatch map functions
	//{{AFX_DISPATCH(CUnitrayDoc)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_DISPATCH
	DECLARE_DISPATCH_MAP()
	DECLARE_INTERFACE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UNITRAYDOC_H__8655D5EC_A8C4_4D96_8DB7_8912A6C198EE__INCLUDED_)
