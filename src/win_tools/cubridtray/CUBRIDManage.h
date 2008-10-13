// CUBRIDManage.h: interface for the CCUBRIDManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_)
#define AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <time.h>


typedef struct _db_name_{
	unsigned int dNum;
//	char* sName;
	char sName[50];
	bool  bStart;
	struct _db_name_* next;
} DBNAME_t, * DBNAMEPtr_t;




class CCUBRIDManage  
{
private:
	DBNAMEPtr_t pStopDBList;
	DBNAMEPtr_t pStartDBList;

	char sCatchResStr[5120];



	bool bCatchResult( char* sCmd );
	char* sCatchResult( char* sCmd );
	DBNAMEPtr_t pMakeList( unsigned int dNum, char* sName );
	DBNAMEPtr_t pMakeList( DBNAMEPtr_t pParent, unsigned int dNum, char* sName );
	char* sGetName( char* sStr );
	bool  bCompareDB( char* sDBName, DBNAMEPtr_t pDBList );

	bool bCUBRID;
	bool bMASTER;

	DBNAMEPtr_t pGetDBListProcess();
	DBNAMEPtr_t pGetDBListFile();
	DBNAMEPtr_t pCompareDB();

	DBNAMEPtr_t pFileDBList;
	DBNAMEPtr_t pExecuteDBList;

	// ORDBLIST.txt를 최종적으로 검색한 최종 시간 정보와 현재의 시간 정보를 담는다.
	time_t pPreTimeFileList;
	time_t pCurTimeFileList;

	// DB Process를 최종적으로 검색한 최종 시간 정보와 현재의 시간 정보를 담는다.
	time_t pPreTimeProcessList;
	time_t pCurTimeProcessList;

	bool bCheckRefreshDBList();
	bool bRefreshDBList();

	DBNAMEPtr_t pCheckExecuteDBList();
public:
	CCUBRIDManage();
	virtual ~CCUBRIDManage();


	bool bStatusMaster();
	bool bCheckMaster();
	bool bStatusServer();
	bool bCheckServer();


	bool bStartCUBRID( char *sdbname );
	bool bStartMaster();

	bool bStopCUBRID( char* sdbname );
	bool bStopMaster();



	DBNAMEPtr_t pGetStartDBList();
	DBNAMEPtr_t pGetStopDBList();


	bool bInstallStatus();


	bool  bDestoryDBList( DBNAMEPtr_t pDBList );


	DBNAMEPtr_t pReqStopDBList();
	DBNAMEPtr_t pReqStartDBList();

};

#endif // !defined(AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_)
