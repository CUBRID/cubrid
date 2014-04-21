// ParseDB.h: interface for the CParseDB class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_)
#define AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000




class CParseDB  
{
private :
	bool bReflashDBList();
	bool bReflashStartDB();
public:
	CParseDB();
	virtual ~CParseDB();

};

#endif // !defined(AFX_PARSEDB_H__C92FAD3D_2BE2_47B8_97E6_E2C3CE5C839C__INCLUDED_)
