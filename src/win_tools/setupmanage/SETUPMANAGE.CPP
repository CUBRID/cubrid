
//
// setupManage.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdlib.h>
#include <io.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <time.h>


#include <wtypes.h>
#include <winnt.h>
#include <winsvc.h>
#include <winuser.h>



#include "PathRegulation.h"

#define ERROR_FILE_NAME	"setup.log"

void vDeletePath( char* sRootPath );
int vCheckLib(char *libname);
void vShow_option( char* sCmd );


void GetCurDateTime( char* p_buf, char* p_form );
void WriteLog( char* p_logpath, char* p_logfile, char* p_format, ... );


int main(int argc, char* argv[])
{
	int res = 0;
	char cFlag = 0;

	if( argc != 3 )
	{
		vShow_option( argv[0] );
		return 0;
	}

	cFlag = argv[1][1];

	switch( cFlag )
	{
		case 'd':
			vDeletePath( argv[2] );
			break;
		case 'j':
			res = vCheckLib(argv[2]);
			if (res == 0) {
				exit(1);
			}
			else {
				exit(0);
			}
			break;
		default :
			vShow_option( argv[0] );
			break;
	}

	return 0;
}


// 현재의 Path에서 CUBRID 관련 Path만 삭제한다.
void vDeletePath( char* sRootPath )
{
	CPathRegulation* cPathCtl = new CPathRegulation();

	if (sRootPath != NULL)
		strcpy_s(cPathCtl->installed_path, sRootPath);
	else
		return;

	cPathCtl->vRemovePathInfo();

	delete cPathCtl;

	return;
}

// 해당 command의 Option을 보여준다.
void vShow_option( char* sCmd )
{
	if( !sCmd ) return;

	printf( "Used : %s -[d|u|c|i|o] [CUBRID Root Path]\n", sCmd );
	printf( "\nOption : \n" );
	printf( "\t-d -> Delete the registry path\n" );

	return;
}

void WriteLog( char* p_logpath, char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    old_logfile[256];
	char  	cur_time[25];
	long    f_size;
	long    f_pos;
	FILE*   logfile_fd = NULL;
	char    fullpath[1024];

	memset( fullpath, 0x00, sizeof( fullpath ) );

	sprintf_s( fullpath, (size_t)1024, "%s\\%s", p_logpath, p_logfile );


#define _MAX_LOGFILE_SIZE_	102400


	while( 1 )
	{
		/* Prepare Log File */
		if( fullpath == NULL )
			logfile_fd = stderr;
		else
			fopen_s(&logfile_fd, fullpath , "a+" );

		if( logfile_fd == NULL )
		{
			fprintf(stderr,"WriteLog:Can't open logfile [%s][%d]\n",
                                       fullpath, errno );
			return;
		}
		else
		{
			f_pos  = ftell( logfile_fd );

			fseek( logfile_fd, 0, SEEK_END );
			f_size = ftell( logfile_fd );

			fseek( logfile_fd, f_pos, SEEK_SET );

			/* If LogFile grows too long */
			if( f_size > _MAX_LOGFILE_SIZE_ )
			{
				fclose( logfile_fd );
	
				strcpy_s( old_logfile, fullpath );
				GetCurDateTime( cur_time,"%Y%m%d%H:%M:%S" );
				strcat_s( old_logfile, "." );
				strcat_s( old_logfile, cur_time);
				strcat_s( old_logfile, ".OLD"  );

				rename( fullpath, old_logfile );
			}
			else
				break;
		}
	}
	/* while End ! */


	GetCurDateTime( cur_time,"%Y%m%d %H:%M:%S" );
	fprintf( logfile_fd, "[%s] ", cur_time );


	va_start( str, p_format );

	vfprintf( logfile_fd, p_format, str );

	va_end( str );

	if( fullpath != NULL )
		fclose( logfile_fd );


}

void GetCurDateTime( char* p_buf, char* p_form )
{
	time_t c_time;
	struct tm l_time = {0};


	time( &c_time );

	localtime_s(&l_time, &c_time );

	strftime( p_buf, 24 , p_form, &l_time );
	
}

int vCheckLib(char *libname)
{
	HINSTANCE hinstLib;

	if (libname == NULL) return 0;
	hinstLib = LoadLibraryA(libname);
	if ( hinstLib == NULL) {
		return 0;
	}
	else {
		FreeLibrary(hinstLib);
		return 1;
	}
}
