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


#if !defined(__UNITRAY_COMM__)
#define	__UNITRAY_COMM__


/*
	UniTray Program에서 공통적으로 사용되는 Member
*/

typedef struct _db_name_{
	unsigned int dNum;
	char* sName;
	struct _db_name_* next;
} DBNAME_t, * DBNAME_Ptr;


typedef enum{
	PAR_MASTER,
	PAR_CUBRID,
	PAR_UNICAS,
	PAR_EAYSSERVER,
	PAR_EASYCLIENT,
	PAR_VSQL,
	PAR_CUBRID_DOWN
} DREGNAME_t;


char* sRegName[] = { "CUBRID", "CUBRID", "CUBRIDCAS", "cmserver", "cmclient", "Visual-SQL", "CUBRID" };
char* sProcessKey[] = { "running.", " Server", "broker", NULL, NULL, NULL, NULL };
char* sProcessCmd[] = { "commdb.exe -P", "commdb.exe -P", "monitor.exe", NULL, "Easy-Manager.exe", "Visual-SQL.exe", "commdb.exe -I" };
char* sProductName[] = { "Master", "CUBRID", "CUBRID CAS", "cmclient", "cmclient", "Visual-SQL", "CUBRID" };
//char* sExcuteName[] = { "master.exe", "server.exe", NULL, "emg.exe", "Easy-Manager.exe", "Visual-SQL.exe", "commdb.exe -I" };

typedef enum{
	REG_FULLPATH,
	REG_ROOTPATH,
	REG_VERSION,
	REG_PATCH,
	REG_BUILD,
	REG_TCL
} DREGKEYNAME_t;

char* sRegKey[] = { "FULL_PATH", "ROOT_PATH", "Version", "Patch", "Build", "TCL" };




#endif


