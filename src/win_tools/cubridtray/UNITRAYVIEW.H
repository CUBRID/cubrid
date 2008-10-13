// unitrayView.h : interface of the CUnitrayView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNITRAYVIEW_H__7E3AB095_5EC2_4F31_835F_8B7DDAB0AD8D__INCLUDED_)
#define AFX_UNITRAYVIEW_H__7E3AB095_5EC2_4F31_835F_8B7DDAB0AD8D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CUnitrayView : public CView
{
protected: // create from serialization only
	CUnitrayView();
	DECLARE_DYNCREATE(CUnitrayView)

// Attributes
public:
	CUnitrayDoc* GetDocument();

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUnitrayView)
	public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CUnitrayView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CUnitrayView)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in unitrayView.cpp
inline CUnitrayDoc* CUnitrayView::GetDocument()
   { return (CUnitrayDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UNITRAYVIEW_H__7E3AB095_5EC2_4F31_835F_8B7DDAB0AD8D__INCLUDED_)
