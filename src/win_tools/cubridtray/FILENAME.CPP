#include <StdAfx.h>
#include <afxtempl.h>
#include "Filename.h"

/////////////////////////////////////////////////////////////////////////////////////////
//
//
CFilename::CFilename() : CString() { }
CFilename::CFilename(const CString& stringSrc) 
         :CString(stringSrc ) {}
CFilename::CFilename( TCHAR ch, int nRepeat) : CString(ch, nRepeat) {}
CFilename::CFilename( LPCTSTR lpch, int nLength): CString(lpch, nLength ) {}
CFilename::CFilename( const unsigned char* psz): CString(psz) {}
CFilename::CFilename( LPCWSTR lpsz): CString(lpsz) {}
CFilename::CFilename( LPCSTR lpsz): CString(lpsz) {}

bool CFilename::isIncludeDir()
{
	if (Find('/') != -1 || Find('\\') != -1) {
		return true;
	} else {
		return false;
	}
}

bool CFilename::isFullpath()
{
	if (GetAt(0) == '/' || GetAt(0) == '\\') {
		return true;
	} else {
		return false;
	}
}

bool CFilename::isIncludeDosPath()
{
	if (Find('\\') != -1) {
		return true;
	} else {
		return false;
	}
}

bool CFilename::GetDriver(CString &driver, CString &path)
{
int	pos;

	if ((pos = Find(':')) != -1) {
		driver = Left(pos);
		driver.MakeUpper();
		path = Mid(pos + 1);

		return true;
	} else {
		driver.Empty();
		path = Mid(0);

		return false;
	}
}

CString CFilename::GetNonLeaf()
{
CString	tempstr;
int	pos;

	if ((pos = ReverseFind('\\')) != -1) {
		tempstr = Left(pos);
	} else {
		tempstr.Empty();
	}

	return tempstr;
}

CString CFilename::GetLeaf()
{
CString	tempstr;
int	pos;

	if ((pos = ReverseFind('\\')) != -1) {
		tempstr = Mid(pos + 1);
	} else {
		tempstr.Empty();
	}

	return tempstr;
}


void CFilename::Dos2Unix()
{
	Replace('\\', '/');
}

void CFilename::Unix2Dos()
{
	Replace('/', '\\');
}