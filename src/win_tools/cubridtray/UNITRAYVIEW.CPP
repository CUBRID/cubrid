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
