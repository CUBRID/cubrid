// ManageRegistry.h: interface for the CManageRegistry class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_)
#define AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CManageRegistry  
{
private:
	char	sKeyPath[1024];
	HKEY	hKeyReg;
	char*	sResultName;

	bool bOpenRegistry();
public:
	bool GetJavaRootPath(char *path);
	CManageRegistry( char* sprodname );
	virtual ~CManageRegistry();

	char* sGetItem( char* sItemName );

	char* sGetString( char* sItemName, char* sCmdName );
	int   dGetString( char* sItemName, char* sCmdName );
	bool  bGetString( char* sString );

};


#endif // !defined(AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_)
