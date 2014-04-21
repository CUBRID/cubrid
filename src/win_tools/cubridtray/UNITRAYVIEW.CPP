// unitrayView.cpp : implementation of the CUnitrayView class
//

#include "stdafx.h"
#include "unitray.h"

#include "unitrayDoc.h"
#include "unitrayView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CUnitrayView

IMPLEMENT_DYNCREATE(CUnitrayView, CView)

BEGIN_MESSAGE_MAP(CUnitrayView, CView)
	//{{AFX_MSG_MAP(CUnitrayView)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnitrayView construction/destruction

CUnitrayView::CUnitrayView()
{
	// TODO: add construction code here

}

CUnitrayView::~CUnitrayView()
{
}

BOOL CUnitrayView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CView::PreCreateWindow(cs);
}

/////////////////////////////////////////////////////////////////////////////
// CUnitrayView drawing

void CUnitrayView::OnDraw(CDC* pDC)
{
	CUnitrayDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	// TODO: add draw code for native data here
}

/////////////////////////////////////////////////////////////////////////////
// CUnitrayView diagnostics

#ifdef _DEBUG
void CUnitrayView::AssertValid() const
{
	CView::AssertValid();
}

void CUnitrayView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CUnitrayDoc* CUnitrayView::GetDocument() // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CUnitrayDoc)));
	return (CUnitrayDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CUnitrayView message handlers
