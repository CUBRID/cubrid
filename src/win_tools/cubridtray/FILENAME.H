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