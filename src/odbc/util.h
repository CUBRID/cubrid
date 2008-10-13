#ifndef	__CUBRID_ODBC_UTIL_HEADER		/* to avoid multiple inclusion */
#define	__CUBRID_ODBC_UTIL_HEADER

#include		"portable.h"

#define		UT_ALLOC(size)				ut_alloc(size)
#define		UT_REALLOC(ptr, size)		ut_realloc(ptr, size)
#define		UT_FREE(ptr)				ut_free(ptr)
#define		UT_MAKE_STRING(ptr, length)	ut_make_string(ptr, length)
#define		UT_MAKE_BINARY(ptr, length) ut_make_binary(ptr, length)
#define		UT_APPEND_STRING(str1, str2, len2)	ut_append_string(str1, str2, len2)

#define		UT_SET_DELIMITER			";;"

/* NC_FREE - NULL check free 
 * NA_FREE - NULL assign free
 * MOVE_STRING - free & copy string
 * NC_FREE_WH - NULL check free with handler
 * NA_FREE_WH - NULL assign free with handler
 * NC_FREE_WHO - NULL check free with handler, option
 * NA_FREE_WHO - NULL assign free with handler, option
 */
#define		NC_FREE(ptr)	if ( ptr != NULL ) UT_FREE(ptr)
#define		NA_FREE(ptr)									\
	do {													\
		if ( ptr != NULL ) {								\
			UT_FREE(ptr);									\
			ptr= NULL;										\
		}													\
	} while (0)

#define		NC_FREE_WH(handle, ptr)  if ( ptr != NULL ) handle(ptr)
#define		NA_FREE_WH(handle, ptr)							\
	do {													\
		if ( ptr != NULL ) {								\
			handle(ptr);									\
			ptr = NULL;										\
		}													\
	} while (0)												\
		
#define		NC_FREE_WHO(handle, ptr, opt)  if ( ptr != NULL ) handle(ptr, opt)
#define		NA_FREE_WHO(handle, ptr, opt)					\
	do {													\
		if ( ptr != NULL ) {								\
			handle(ptr, opt);								\
			ptr = NULL;										\
		}													\
	} while (0)												\

#define		UT_COPY_STRING(target, value)						\
	NC_FREE(target);										\
	target = UT_MAKE_STRING(value, -1)
  
#define SET_OPTION(value, option)       ((value) |= (option))
#define UNSET_OPTION(value, option)     ((value) ^= (option))
#define IS_OPTION_SETTED(value, option)    (( (value) & (option) ) == (option))

//////////////////   debug log
#ifdef __DEBUG_LOG
        #ifdef UNIX
                #include <sys/time.h>
                #include <stdlib.h>

                #define DEBUG_FILE_KEY          "DEBUG_TIMESTAMP_FILE"
                #define DEBUG_TIMESTAMP(value)                          \
                        do {                                            \
                            struct timeval now;                         \
                            FILE        *fp = NULL;                     \
                            char        *pt;                            \
                            pt = getenv(DEBUG_FILE_KEY);                \
                            if ( pt != NULL ) {                         \
                                fp = fopen(pt, "a+");                   \
                                gettimeofday(&now, NULL);               \
                                fprintf(fp,#value "	%ld.%07ld	%s	%d\n", now.tv_sec, now.tv_usec, __FILE__, __LINE__);                         \
                                fclose(fp);                             \
                            }                                           \
                        } while(0)
        #else
                #include <sys/types.h>
                #include <sys/timeb.h>

                #define DEBUG_FILE_KEY          "d:\\lsj1888\\time_log.txt"
#define DEBUG_TIMESTAMP(value)
/*
                #define DEBUG_TIMESTAMP(value)                          \
                        do {                                            \
                            struct _timeb now;                          \
                            FILE        *fp = NULL;                     \
                            fp = fopen(DEBUG_FILE_KEY, "a+");           \
                            _ftime(&now);                       \
                            fprintf(fp,#value "	%ld.%07ld	%s	%d\n", now.time, now.millitm, __FILE__, __LINE__);                           \
                            fclose(fp);                         \
                        } while(0)
						*/
        #endif
#else
        #define DEBUG_TIMESTAMP(value)
#endif

#ifdef __DEBUG_LOG
        #ifdef UNIX
                #include <stdlib.h>

                #define DEBUG_LOG_FILE_KEY          "DEBUG_LOG_FILE"
                #define DEBUG_LOG(value)                          \
                        do {                                            \
                            FILE        *fp = NULL;                     \
                            char        *pt;                            \
                            pt = getenv(DEBUG_LOG_FILE_KEY);                \
                            if ( pt != NULL ) {                         \
                                fp = fopen(pt, "a+");                   \
                                fprintf(fp,"%s at %s %d\n", value, __FILE__, __LINE__);                         \
                                fclose(fp);                             \
                            }                                           \
                        } while(0)
        #else
                
                #define DEBUG_LOG_FILE_KEY          "c:\\odbc_log.txt"
                #define DEBUG_LOG(value)                          \
                        do {                                            \
                            FILE        *fp = NULL;                     \
                            fp = fopen(DEBUG_LOG_FILE_KEY, "a+");           \
                            fprintf(fp,"%s at %s %d\n", value, __FILE__, __LINE__);                           \
                            fclose(fp);                         \
                        } while(0)
        #endif
#else
        #define DEBUG_LOG(value)
#endif

/* Dynamic string */
typedef struct __st_dynamic_string {
    char* value;
    int totalSize;
    int usedSize;
} D_STRING;

typedef struct __st_dynamic_binary {
	char* value;
	int size;
} D_BINARY;

typedef struct tagST_LIST{
  void				     *key;
  void            		*value;
  struct tagST_LIST       *next;
} ST_LIST;

PUBLIC void InitStr(D_STRING* str);
PUBLIC void FreeStr(D_STRING* str);
PUBLIC ERR_CODE ReallocImproved(char** dest, int* destSize, int usedSize, int allocSize);
PUBLIC ERR_CODE StrcatImproved(D_STRING* dest, char* source);
PUBLIC ERR_CODE MemcatImproved(D_STRING* dest, char* src, int srcSize);
PUBLIC ERR_CODE MemcpyImproved(D_STRING* dest, char* src, int srcSize);
PUBLIC void ConcatPath(char* prePath, char* postPath, char* resultPath);
PUBLIC _BOOL_ IsAlphaNumeric(int num);
PUBLIC void long_to_byte(long value, unsigned char* bytes, int length);
PUBLIC void byte_to_long(unsigned char* bytes, int length, long* value);
PUBLIC ERR_CODE bincpy(D_BINARY *dest, char *src, int size);
PUBLIC ERR_CODE binfree(D_BINARY *src);
PUBLIC void *ut_alloc(int size);
PUBLIC void ut_free(void* ptr);
PUBLIC void* ut_realloc(void* ptr, int size);
PUBLIC char *ut_make_string(const char *src, int length);
PUBLIC char* ut_append_string(char* str1, char* str2, int len2);
PUBLIC char *ut_make_binary(const char *src, int length);

PUBLIC int element_from_setstring(char **current,char *buf);
PUBLIC int size_from_setstring(char* setstring);
PUBLIC void add_element_to_setstring(char *setstring, char *element);

PUBLIC char* trim(char *str);
PUBLIC RETCODE str_value_assign(const char* in_value, 
								char* out_buf, 
								int out_buf_len, 
								int* val_len_ptr);
PUBLIC RETCODE bin_value_assign(const void*		in_value, 
								int				in_val_len,
								char*			out_buf, 
								int				out_buf_len, 
								int*			val_len_ptr);

PUBLIC short is_oidstr(char *str);
PUBLIC short is_oidstr_array(char** array, int size);
PUBLIC int replace_oid(char *sql_text, char **org_param_pos_pt, 
					   char **oid_param_pos_pt, char **oid_param_val_pt);


/*---------------------------------------------------------------------
 *					char util
* *--------------------------------------------------------------------*/

PUBLIC short char_islower(int c);
PUBLIC short char_isupper(int c);
PUBLIC short char_isalpha(int c);
PUBLIC short char_isdigit(int c);
PUBLIC short char_isxdigit(int c);
PUBLIC short char_isalnum(int c);
PUBLIC short char_isspace(int c);
PUBLIC short char_isascii(int c);

PUBLIC short char_tolower(int c);
PUBLIC short char_toupper(int c);


extern ERR_CODE ListHeadAdd (ST_LIST* head, void* key, void* val,
	ERR_CODE (*assignFunc)(ST_LIST*, void*, void*));
extern ERR_CODE ListTailAdd (ST_LIST* head, void* key, void* val,
	ERR_CODE (*assignFunc)(ST_LIST*, void*, void*));
extern void* ListFind (ST_LIST* head, void* key, ERR_CODE (*cmpFunc)(void*, void*));
extern ERR_CODE ListDeleteNode(ST_LIST* head, void* key,
		  ERR_CODE (*cmpFunc)(void*, void*), void (*nodeDelete)(ST_LIST*));
extern void ListDelete (ST_LIST* head, void (*nodeDelete)(ST_LIST*));
extern ERR_CODE ListCreate(ST_LIST** head);
extern _BOOL_ IsAnyNode(ST_LIST* head);
extern ST_LIST* HeadNode(ST_LIST* dummyHead);
extern ST_LIST* NextNode(ST_LIST* node);

#ifdef _DEBUG
extern void ListPrint(ST_LIST* head, void (*nodePrint)(ST_LIST*));
#endif

/* node assign function */
extern ERR_CODE NodeAssign(ST_LIST* node, void* key, void* value);
/* node compare function */
extern ERR_CODE NodeCompare(void* key, void* search);
/* node free function */
extern void NodeFree(ST_LIST* node);

PUBLIC int str_like(const unsigned char *src,
                    const unsigned char *pattern,
                    const unsigned char esc_char,
					short				case_sensitive);

PUBLIC void get_value_from_connect_str(char *szConnStrIn, 
								char *value, 
								int size, 
								char *keyword);

/*-------------  connection string util	------------------------*/
PUBLIC const char* next_element(const char* element_list);
PUBLIC const char*	element_value(const char* element);
PUBLIC const char*	element_value_by_key(const char *element_list,const char* key);

#endif	/* ! __CUBRID_ODBC_UTIL_HEADER */
