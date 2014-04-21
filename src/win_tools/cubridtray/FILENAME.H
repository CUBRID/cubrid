#if !defined(__FILENAME)
#define	__FILENAME

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FileName.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// 

class CFilename : public CString
{
public:
	CFilename();
	CFilename(const CString& stringSrc );
	CFilename( TCHAR ch, int nRepeat = 1 );
	CFilename( LPCTSTR lpch, int nLength );
	CFilename( const unsigned char* psz );
	CFilename( LPCWSTR lpsz );
	CFilename( LPCSTR lpsz );
	bool isIncludeDir();
	bool isFullpath();
	bool isIncludeDosPath();
	bool GetDriver(CString &driver, CString &path);
	CString GetNonLeaf();
	CString GetLeaf();
	void Dos2Unix();
	void Unix2Dos();
};

#endif