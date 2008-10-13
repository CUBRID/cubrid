// CommonMethod.h: interface for the CCommonMethod class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_COMMONMETHOD_H__7DE6482B_4645_4E3E_A66A_FF6BEA916D30__INCLUDED_)
#define AFX_COMMONMETHOD_H__7DE6482B_4645_4E3E_A66A_FF6BEA916D30__INCLUDED_


#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

typedef enum{
	PAR_MASTER,
	PAR_CUBRID,
	PAR_UNICAS,
	PAR_EAYSSERVER,
	PAR_EASYCLIENT,
	PAR_VSQL,
	PAR_CUBRID_DOWN
} DREGNAME_t;


class CCommonMethod  
{
public:
	CCommonMethod();
	virtual ~CCommonMethod();

	char* sCatchResult( char* sCmd );
	bool bStatusProcess( DREGNAME_t dProdNum );

};

#endif // !defined(AFX_COMMONMETHOD_H__7DE6482B_4645_4E3E_A66A_FF6BEA916D30__INCLUDED_)
