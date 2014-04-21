#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

#include "valleyMisc.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int errno;

int MakeMeDaemon()
{

	int    				rslt;
	struct sigaction		act;

	rslt = getppid();
	if(rslt == 1) return(0);

	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;

	sigaction(SIGTTOU,  &act,  (struct sigaction *)0);
	sigaction(SIGTTIN,  &act,  (struct sigaction *)0);
	sigaction(SIGTSTP,  &act,  (struct sigaction *)0);

	rslt = fork();
	if(rslt != 0) exit(0);

	setsid();
	sigaction(SIGHUP,   &act,  (struct sigaction *)0);

	rslt = fork();
	if(rslt != 0) exit(0);

	return(0);
}



void error_occur( int needExit, char* p_format, ...) //va_list str ) 
{
	va_list str;

	va_start( str, p_format );
	vfprintf( stderr, p_format, str );
	va_end( str );

	if( needExit == DO_EXIT )
		exit(0);
}


void WriteDebug( char* p_format, ... ) 
{
	va_list str;

#ifdef __DEBUG
	va_start( str, p_format );

	vfprintf( stderr, p_format, str );

	va_end( str );
#endif

}



void WriteLog( char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    old_logfile[256];
	char  	cur_time[25];
	long    f_size;
	long    f_pos;
	FILE*   logfile_fd;


#define _MAX_LOGFILE_SIZE_	102400


	while( 1 )
	{
		/* Prepare Log File */
		if( p_logfile == NULL )
			logfile_fd = stderr;
		else
			logfile_fd = fopen( p_logfile , "a+" );

	
		if( logfile_fd == NULL )
		{
			fprintf(stderr,"WriteLog:Can't open logfile [%s][%d]\n",
                                       p_logfile, errno );
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
	
				strcpy( old_logfile, p_logfile );
				GetCurDateTime( cur_time,"%Y%m%d%H:%M:%S" );
				strcat( old_logfile, "." );
				strcat( old_logfile, cur_time);
				strcat( old_logfile, ".OLD"  );

				rename( p_logfile, old_logfile );
			}
			else
				break;
		}
	}
	/* while End ! */


#ifndef __DEBUG
	GetCurDateTime( cur_time,"%Y%m%d %H:%M:%S" );
	fprintf( logfile_fd, "[%s] ", cur_time );
#endif



	va_start( str, p_format );

	vfprintf( logfile_fd, p_format, str );

	va_end( str );

	if( p_logfile != NULL )
		fclose( logfile_fd );


}





void WriteDailyLog( char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    logfilename[256];
	char	strDate[10];
	char  	cur_time[25];
	long    f_size;
	long    f_pos;
	FILE*   logfile_fd;



	memset( strDate,     0x00, sizeof(strDate));
	memset( logfilename, 0x00, sizeof(logfilename));


	GetCurDateTime( strDate,"%Y%m%d" );

	sprintf( logfilename, "%s.%s", p_logfile, strDate );


	/* Prepare Log File */
	if( p_logfile == NULL )
		logfile_fd = stderr;
	else
		logfile_fd = fopen( logfilename , "a+" );

	
	if( logfile_fd == NULL )
	{
		fprintf(stderr,"WriteLog:Can't open logfile [%s][%d]\n",
                                p_logfile, errno );
		return;
	}

#ifndef __DEBUG
	GetCurDateTime( cur_time,"%Y%m%d %H:%M:%S" );
	fprintf( logfile_fd, "[%s] ", cur_time );
#endif



	va_start( str, p_format );

	vfprintf( logfile_fd, p_format, str );

	va_end( str );

	if( p_logfile != NULL )
	{
		fflush( logfile_fd );
		fclose( logfile_fd );
	}


}




void GetCurDateTime( char* p_buf, char* p_form )
{
	time_t c_time;
	struct tm* l_time = NULL;


	time( &c_time );

	l_time = localtime( &c_time );

	strftime( p_buf, 24 , p_form, l_time );
	
}



void WriteDebugLog( char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    old_logfile[256];
	char  	cur_time[25];
	long    f_size;



#ifndef __DEBUG
	GetCurDateTime( cur_time,"%Y%m%d %H:%M:%S" );
	fprintf( stderr, "[%s]", cur_time );
#endif

	va_start( str, p_format );

	vfprintf( stdout, p_format, str );

	va_end( str );

}
#ifdef __cplusplus
}
#endif
