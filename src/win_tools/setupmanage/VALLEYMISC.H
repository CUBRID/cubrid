/* --------------------------------------------------------------

	FileName : valleyMisc.h

 -------------------------------------------------------------- */
#ifndef _VALLEY_MISC_LIB_
#define _VALLEY_MISC_LIB_

#define DO_EXIT		1
#define NO_EXIT		0


#ifdef __cplusplus
extern "C" {
#endif



int  MakeMeDaemon();     /* Make caller daemon process */
void WriteDebug( char* p_format, ... );         // );
void WriteLog( char* p_logfile, char* p_format, ... );    //  va_list );
void WriteDailyLog( char* p_logfile, char* p_format, ... );    //  va_list );
void WriteDebugLog(char* p_logfile, char* p_format, ...); //va_list );
void error_occur( int needExit,char* p_format, ...);       //  va_list );
void GetCurDateTime( char* p_buf, char* p_form );

#ifdef __cplusplus
}
#endif
#endif
