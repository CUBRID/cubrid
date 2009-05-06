
//////////////////////////////////////////////////////////////////////
//
// PathRegulation.cpp: implementation of the CPathRegulation class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "PathRegulation.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#define		REG_PATH_PATH	"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"

CPathRegulation::CPathRegulation()
{
	pRootPath = NULL;
	bOpenTheRegistry();

	sCurrPath = sGetPath();
}

CPathRegulation::CPathRegulation( char* spath )
{
	pRootPath = NULL;
	sCurrPath = spath;

	bOpenTheRegistry();
}

CPathRegulation::~CPathRegulation()
{
	if( pRootPath )
		vDestroyList( );
}


// Main Function;
void CPathRegulation::vRemovePathInfo( void )
{
	if( !sCurrPath ) return;

	vDivisionItem();

	if( !pRootPath ) return;

	UniPathPtr_t pCurr = pRootPath;

	char* sSetPath = new char[1024];
	memset( sSetPath, 0x00, 1024 );

	while( pCurr )
	{
		if( pCurr->bKeep && strlen( pCurr->sPath ) > 0 )
			if( strlen( sSetPath ) <= 0 )
				sprintf_s( sSetPath, (size_t)1024, "%s", pCurr->sPath );
			else
				sprintf_s( sSetPath, (size_t)1024, "%s;%s", sSetPath, pCurr->sPath );

		pCurr = pCurr->pNext;
	}

	if( strlen( sSetPath ) > 0 )
		vSetPath( sSetPath );

	vDestroyList( );

	return;
}





// Registry 관련 Function

// Registry 를 Open한다.( SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment )
bool CPathRegulation::bOpenTheRegistry( void )
{
	LONG lResult = RegOpenKeyExA( HKEY_LOCAL_MACHINE, REG_PATH_PATH, 0, KEY_ALL_ACCESS, &hRootKey );

    if(lResult != ERROR_SUCCESS)
        return false;  //#define에서 전달인자로 정의

	return true;
}


// Open한 Registry로 부터 값을 가져온다.
char* CPathRegulation::sGetPath( void )
{
	LPBYTE sTmpPath = (LPBYTE)malloc(sizeof(BYTE) * 4096);

	if (!sTmpPath) return NULL;

    DWORD dwType = REG_EXPAND_SZ;
    DWORD dwBytes = (DWORD)4096;

    LONG lResult = RegQueryValueExA( hRootKey, "Path", 0, &dwType, sTmpPath, &dwBytes);
	
	if( lResult != ERROR_SUCCESS ) return NULL;

	char* sResult = new char[4096];

	if (!sResult) return NULL;

	strcpy( sResult, ( char* )sTmpPath );

	if (sTmpPath) free(sTmpPath);

	return sResult;
}


// 편집된 path 정보를 Registry에 저장한다.
void CPathRegulation::vSetPath( char* sPath )
{
	if( !sPath ) return;

    DWORD dwType = REG_EXPAND_SZ;

    LONG lResult = RegSetValueExA( hRootKey, "Path", 0, dwType, (LPBYTE)sPath, ( DWORD )strlen( sPath ) );

	return;
}




// vDivisionItem에서 분류해서 생긴 List를 제거한다.
void CPathRegulation::vDestroyList( void )
{
	if( !pRootPath ) return;

	UniPathPtr_t pNextPath, pCurPath;

	pCurPath = pRootPath;

	while( pCurPath )
	{
		if( pCurPath->sPath ) delete pCurPath->sPath;
		pNextPath = pCurPath->pNext;
		delete pCurPath;
		pCurPath = pNextPath;
	}


	pRootPath = NULL;

	return;
}



// Path 정보를 분리한다.
void CPathRegulation::vDivisionItem( void )
{
	char* sCurr = sCurrPath;
	char* sEnd;
	int dStrLng = 0;
	UniPathPtr_t pCurr, pTail;
	char target_path[1024];

	pCurr = 0x00;
	pTail = 0x00;

	if (installed_path[strlen(installed_path) - 1] == '\\')
	{
		sprintf_s(target_path, (size_t)1024, "%sbin", installed_path);
	}
	else
	{
		sprintf_s(target_path, (size_t)1024, "%s\\bin", installed_path);
	}

	while( sCurr )
	{
		pCurr = new UniPath_t;
		memset( pCurr, 0x00, sizeof( UniPath_t ) );

		sEnd = strchr( sCurr, ';' );

		if( !sEnd )
		{
			// Parsing 이 완료된 부분
			pCurr->sPath = new char[ strlen( sCurr ) + 1 ];
			memset( pCurr->sPath, 0x00, strlen( sCurr ) + 1 );
			strcpy( pCurr->sPath, sCurr );

			// CUBRID path인지 확인한다.
			if (_strnicmp(target_path, pCurr->sPath, strlen(target_path)) == 0)
			{
				pCurr->bKeep = false;
			}
			else
			{
				pCurr->bKeep = true;
			}

			sCurr = 0x00;
		}
		else
		{
			// Parsing 할 string이 더 존재.
			*sEnd = 0x00;
			dStrLng = (int) (sEnd - sCurr);

			if( dStrLng <= 0 )
				continue;

			pCurr->sPath = new char[ dStrLng + 1 ];
			memset( pCurr->sPath, 0x00, dStrLng + 1 );
			memcpy( pCurr->sPath, sCurr, dStrLng );

			// CUBRID path인지 확인한다.
			if (_strnicmp(target_path, pCurr->sPath, strlen(target_path)) == 0)
			{
				pCurr->bKeep = false;
			}
			else
			{
				pCurr->bKeep = true;
			}

			sCurr = sEnd + 1;
		}

		pCurr->pNext = 0x00;

		if( !pRootPath )
		{
			pRootPath = pCurr;
			pTail = pRootPath;
		}
		else
		{
			pTail->pNext = pCurr;
			pTail = pTail->pNext;
		}
	}


	return;
}
