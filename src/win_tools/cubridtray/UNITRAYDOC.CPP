// unitrayDoc.cpp : implementation of the CUnitrayDoc class
//

#include "stdafx.h"
#include "unitray.h"

#include "unitrayDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDoc

IMPLEMENT_DYNCREATE(CUnitrayDoc, CDocument)

BEGIN_MESSAGE_MAP(CUnitrayDoc, CDocument)
	//{{AFX_MSG_MAP(CUnitrayDoc)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

BEGIN_DISPATCH_MAP(CUnitrayDoc, CDocument)
	//{{AFX_DISPATCH_MAP(CUnitrayDoc)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//      DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_DISPATCH_MAP
END_DISPATCH_MAP()

// Note: we add support for IID_IUnitray to support typesafe binding
//  from VBA.  This IID must match the GUID that is attached to the 
//  dispinterface in the .ODL file.

// {461D7CD0-C8D6-4102-8261-8F1944084A24}
static const IID IID_IUnitray =
{ 0x461d7cd0, 0xc8d6, 0x4102, { 0x82, 0x61, 0x8f, 0x19, 0x44, 0x8, 0x4a, 0x24 } };

BEGIN_INTERFACE_MAP(CUnitrayDoc, CDocument)
	INTERFACE_PART(CUnitrayDoc, IID_IUnitray, Dispatch)
END_INTERFACE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDoc construction/destruction

CUnitrayDoc::CUnitrayDoc()
{
	// TODO: add one-time construction code here

	EnableAutomation();

	AfxOleLockApp();
}

CUnitrayDoc::~CUnitrayDoc()
{
	AfxOleUnlockApp();
}

BOOL CUnitrayDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)

	return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CUnitrayDoc serialization

void CUnitrayDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDoc diagnostics

#ifdef _DEBUG
void CUnitrayDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CUnitrayDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDoc commands
