#if !defined(__MESSAGE)
#define	__MESSAGE

/* error message */
#define	MSG_MAIN_TITLE		"CUBRID Service Tray"
#define	MSG_TRAY_ERROR		"Tray Icon을 생성할 수 없습니다 !!"
#define	MSG_ENV_ERROR		"필수 환경 변수(%s)를 찾을 수 없습니다 !!\n 환경 변수를 설정하던가 System을 Rebooting하십시오."
#define	MSG_INSTALL_ERROR	"제품(%s)가 제대로 인스톨되지 않았습니다 !!"
#define	MSG_MASTER_ERROR	"마스터를 작동할 수 없습니다 !!"
#define	MSG_START			"DB 서버를 작동합니다 (%s)"
#define	MSG_SERVER_ERROR	"DB 서버를 구동할 수 없습니다 !!"
#define	MSG_SHUTDOWN		"DB 서버를 정지합니다 (%s)"
#define	MSG_SHUTDOWN_ERROR	"DB 서버를 정지할 수 없습니다."
#define	MSG_START_UNICAS	"CUBRID CAS 서버를 구동합니다 !!"
#define	MSG_START_UNICAS_ERROR	"CUBRID CAS 서버를 구동할 수 없습니다 !!"
#define	MSG_RESTART_UNICAS	"CUBRID CAS 서버를 재 구동합니다 !!"
#define	MSG_RESTART_UNICAS_ERROR	"CUBRID CAS 서버를 재 구동할 수 없습니다 !!"
#define	MSG_STOP_UNICAS		"CUBRID CAS 서버를 정지합니다 !!"
#define	MSG_STOP_UNICAS_ERROR	"CUBRID CAS 서버를 정지할 수 없습니다 !!"
#define MSG_UC_CANT_MONITOR	"선택된 broker는 모니터링할 수 없습니다 !!"
#define	MSG_UNKNOWN_CUBRID	"Version information is not available."
#define	MSG_UNKNOWN_UNICAS	"Version information is not available."
#define	MSG_NO_VOLUME		"No Volume"


#define	MSG_UCP_PATH		"패스를 사용할 수 없습니다 !!"
#define	MSG_UCP_FULLPATH	"절대 패스를 사용할 수 없습니다 !!"
#define	MSG_UCP_DOSPATH		"Dos 패스를 사용할 수 없습니다 !!"
#define	MSG_UCP_FILENOTFOUND	"지정한 파일을 사용할 수 없습니다 !!"

/* prompt message */
#define	MSG_EXIT			"[경고] 종료하시겠습니까 ?"
#define	MSG_UCP_DELETE		"삭제하시겠습니까 ?"
#define	MSG_UC_RESTART		"재 구동하시겠습니까 ?"


/* title for unicas property */
#define	TITLE_UCP_BROKER				"broker"
#define	TITLE_UCP_SERVICE				"status"
#define	TITLE_UCP_APPL_SERVER			"server"
#define	TITLE_UCP_BROKER_PORT			"port"
#define TITLE_UCP_JOB_QUEUE				"queue"
#define	TITLE_UCP_TIMEOUT				"timeout"
#define	TITLE_UCP_SQL_LOG				"sql-log"
#define	TITLE_UCP_AUTO_ADD_APPL_SERVER		"auto"
#define	TITLE_UCP_MIN_NUM_APPL_SERVER		"min"
#define	TITLE_UCP_MAX_NUM_APPL_SERVER		"max"

#define	TITLE_UCM_ID					"id"
#define	TITLE_UCM_PID					"pid"
#define	TITLE_UCM_C						"c"
#define	TITLE_UCM_PORT					"port"
#define	TITLE_UCM_STATUS				"status"
#define	TITLE_UCM_LASTACCESSTIME		"last access time"
#define	TITLE_UCM_ACTION				"action"

/* message for each window */
/* IDD_ABOUTBOX */
#define	WN_ABT_TITLE					"CUBRID Service Tray"
#define	WN_ABT_MSG						"CUBRID Service Tray for Windows 2000/NT"
#define	WN_ABT_COPYRIGHTS				"CopyRigths(C) 2002-2005 KCOMS Co.,LTD\r\nCopyRight(C) 2006 CUBRID Co., LTD"
#define	WN_ABT_OK						"확인"
#define	WN_ABT_VER_LABEL				"버젼 정보"
#define	WN_ABT_VER_CUBRID				"Version of CUBRID ::"
#define	WN_ABT_VER_UNICAS				"Version of CUBRID CAS ::"

/* IDD_PROPERTY */
#define	WN_DBP_TITLE					"DB 속성"
#define WN_DBP_PATHNAME					"경로명"
#define	WN_DBP_DBNAME					"서버명"
#define	WN_DBP_LOGPATH					"로그경로명"
#define	WN_DBP_CHECK					"매니저 구동시 DB 서버를 구동함"
#define	WN_DBP_OK						"적용"
#define	WN_DBP_CANCEL					"취소"

/* IDD_SHOWRUNDB */
#define	WN_SRD_TITLE					"구동하고 있는 DB의 볼륨 정보"
#define	WN_SRD_GVOL						"Generic Vol."
#define	WN_SRD_DVOL						"Data Vol."
#define	WN_SRD_IVOL						"Index Vol."
#define	WN_SRD_TVOL						"Temp Vol."
#define	WN_SRD_OK						"확인"
#define	WN_SRD_CANCEL					"취소"

/* IDD_STARTSERVER */
#define	WN_STD_TITLE					"DB 서버 구동"
#define	WN_STD_OK						"구동"
#define	WN_STD_CANCEL					"취소"

/* IDD_STOPSERVER */
#define	WN_SHD_TITLE					"DB 서버 정지"
#define	WN_SHD_OK						"정지"
#define	WN_SHD_CANCEL					"취소"

/* IDD_UC_MONITOR */
#define	WN_UCM_TITLE					"CUBRID CAS 모니터링"
#define WN_UCM_MONITOR					"모니터"
#define	WN_UCM_SERVICE					"서비스"
#define	WN_UCM_SERVICE_ON				"서비스 개시"
#define	WN_UCM_SERVICE_OFF				"서비스 중단"
#define	WN_UCM_CANCEL					"끝내기"

/* IDD_UCM_DETAIL */
#define	WN_UMD_TITLE					"모니터링 (%s)"
#define	WN_UMD_ADD						"추가"
#define	WN_UMD_DROP						"삭제"
#define	WN_UMD_RESTART					"재구동"
#define	WN_UMD_CANCEL					"끝내기"

/* IDD_UC_PROPERTY */
#define	WN_UCP_TITLE					"CUBRID CAS 속성 정보 변경"
#define	WN_UCP_NEW						"생성"
#define	WN_UCP_UPDATE					"변경"
#define	WN_UCP_DELETE					"삭제"
#define	WN_UCP_OK						"적용"
#define	WN_UCP_CANCEL					"취소"
#define	WN_UCP_MASTERINFO				"Master Information"
#define	WN_UCP_SM						"Master's shared memory"
#define	WN_UCP_LOGFILE					"Log File of Admin"

/* IDD_UCP_DETAIL */
#define	WN_UPD_TITLE					"정보 (%s)"
#define	WN_UPD_COMMON_PROPERTY			"Common Property"
#define	WN_UCP_BROKER					"Broker Name"
#define	WN_UCP_SERVICE_ON				"Service ON"
#define	WN_UCP_SERVICE_OFF				"Service OFF"
#define	WN_UCP_APPL_SERVER				"Server Type"
#define	WN_UCP_MORE						"More >>"
#define	WN_UCP_BROKER_PORT				"TCP/IP Port"
#define	WN_UCP_APPL_SERVER_SHM_ID		"SM ID"
#define	WN_UCP_APPLICATION_SERVER		"Application Server"
#define	WN_UCP_AUTO_ADD_APPL_SERVER		"Automatic adding"
#define	WN_UCP_MIN_NUM_APPL_SERVER		"Min"
#define	WN_UCP_MAX_NUM_APPL_SERVER		"Max"
#define	WN_UCP_APPL_SERVER_MAX_SIZE		"Size Limit"
#define	WN_UCP_LOG_INFORMATION			"Log Information"
#define	WN_UCP_LOG_DIR					"Directory"
#define	WN_UCP_LOG_BACKUP_ON			"Backup ON"
#define	WN_UCP_LOG_BACKUP_OFF			"Backup OFF"
#define	WN_UCP_SQL_LOG_ON				"SQL Tracking ON"
#define	WN_UCP_SQL_LOG_OFF				"SQL Tracking OFF"
#define	WN_UCP_SQL_LOG_TIME				"Time"
#define	WN_UCP_ACCESS_LOG_ON			"Access LOG ON"
#define	WN_UCP_ACCESS_LOG_OFF			"Access LOG OFF"
#define	WN_UCP_SECURITY					"Security"
#define	WN_UCP_SECURITY_ON				"Security ON"
#define	WN_UCP_SECURITY_OFF				"Security OFF"
#define	WN_UCP_ACCESS_LIST				"File"
#define	WN_UCP_ETC						"ETC"
#define	WN_UCP_SOURCE_ENV				"Enviornment"
#define	WN_UCP_TIME_TO_KILL				"Time to Kill"
#define	WN_UCP_SESSION_TIMEOUT			"Session Timeout"
#define	WN_UCP_JOB_QUEUE_SIZE			"Job Queue size"
#define	WN_UCP_OK						"적용"
#define	WN_UCP_CANCEL					"취소"

/* IDD_UCP_VAS */
#define	WN_WAS_TITLE					"WAS 정보"
#define	WN_WAS_ENC_APPL					"Support Encoding script"
#define	WN_WAS_SESSION					"Support Session"
#define	WN_WAS_SET_COOKIE				"Support Cookie"
#define	WN_WAS_ENTRY_VALUE_TRIM			"Trim entry value"
#define	WN_WAS_OID_CHECK				"Check OID"
#define	WN_WAS_APPL_ROOT				"Script root"
#define	WN_WAS_FILE_UPLOAD_TEMP_DIR		"file upload"
#define	WN_WAS_ERROR_LOG				"Error Log"
#define	WN_WAS_FILE_UPLOAD_DELIMITER	"Delimiter"
#define	WN_WAS_OK						"적용"
#define	WN_WAS_CANCEL					"취소"

/* IDD_UCP_WAS */
#define	WN_VAS_ENC_APPL					"Support Encoding script"
#define	WN_VAS_SESSION					"Support Session"
#define	WN_VAS_OID_CHECK				"Check OID"
#define	WN_VAS_APPL_ROOT				"Script root"
#define	WN_VAS_FILE_UPLOAD_TEMP_DIR		"file upload"
#define	WN_VAS_ERROR_LOG				"Error Log"
#define	WN_VAS_COMPRESS_SIZE			"compress"
#define	WN_VAS_PRIORITY_GAP				"priority gap"
#define	WN_VAS_OK						"적용"
#define	WN_VAS_CANCEL					"취소"

/* variable of unicas conf */
#define	ON					"on"
#define	OFF					"off"

#define	MASTER_SHM_ID		"master_shm_id"
#define	ADMIN_LOG_FILE		"admin_log_file"
#define	SERVICE				"service"
#define	BROKER_PORT			"broker_port"
#define	APPL_SERVER			"appl_server"
#define	STR_CAS				"cas"
#define	STR_WAS				"was"
#define	STR_VAS				"vas"
#define	STR_ULS				"uls"
#define	STR_AMS				"ams"
#define	NO_CAS				0
#define	NO_WAS				1
#define	NO_VAS				2
#define	NO_ULS				3
#define	NO_AMS				4
#define	MIN_NUM_APPL_SERVER	"min_num_appl_server"
#define	MAX_NUM_APPL_SERVER	"max_num_appl_server"
#define	AUTO_ADD_APPL_SERVER	"auto_add_appl_server"
#define	APPL_SERVER_SHM_ID	"appl_server_shm_id"
#define	APPL_SERVER_MAX_SIZE	"appl_server_max_size"
#define	LOG_DIR				"log_dir"
#define	LOG_BACKUP			"log_backup"
#define	SOURCE_ENV			"source_env"
#define	SQL_LOG				"sql_log"
#define	SQL_LOG_TIME		"sql_log_time"
#define	TIME_TO_KILL		"time_to_kill"
#define	SESSION_TIMEOUT		"session_timeout"
#define	JOB_QUEUE_SIZE		"job_queue_size"
#define	ACCESS_LOG			"access_log"
#define	ACCESS_LIST			"access_list"
#define	SESSION				"session"
#define	APPL_ROOT			"appl_root"
#define	ERROR_LOG			"error_log"
#define	STR_BOTH			"both"
#define	STR_BROWSER			"browser"
#define	STR_LOGFILE			"logfile"
#define	STR_NONE			"none"
#define	NO_BOTH				0
#define	NO_BROWSER			1
#define	NO_LOGFILE			2
#define	NO_NONE				3
#define	ENTRY_VALUE_TRIM	"entry_value_trim"
#define	OID_CHECK			"oid_check"
#define	ENC_APPL			"enc_appl"
#define	SET_COOKIE			"set_cookie"
#define	FILE_UPLOAD_TEMP_DIR	"file_upload_temp_dir"
#define	FILE_UPLOAD_DELIMITER	"file_upload_delimiter"
#define	COMPRESS_SIZE		"compress_size"
#define	PRIORITY_GAP		"priority_gap"

/* command & dir for CUBRID */
#define	CMD_CUBRID_RELEASE	"unirelse.exe"
#define	CMD_MAKEDB			"CdbWizard.exe"
#define	CMD_MASTER			"master.exe"
#define	CMD_SERVER			"server.exe"
#define	CMD_COMMDB			"commdb.exe"
#define	CMD_SHUTDOWN_SERVER	"commdb.exe -I"
#define	CMD_SPACEDB			"spacedb.exe"
#define	CMD_SPACEDB_CS		"spacedb.exe -cs"
#define	DIR_US_ADMIN		"admin"
#define	DIR_US_LIB			"lib"
#define	DIR_US_INCLUDE		"include"

/* command & dir for unicas */
#define	CMD_UNICAS_RELEASE	"bin\\unicas_ver.exe"
#define	CMD_START_UNICAS	"bin\\uc.exe start"
#define	CMD_STOP_UNICAS		"bin\\uc.exe stop"
#define	CMD_NAMED			"ucd.exe"
#define	CMD_UC_MONITOR		"monitor.exe"
#define	DIR_UC_BIN			"bin"
#define	DIR_UC_CONF			"conf"
#define	DIR_UC_LIB			"lib"

/* environment variable */
#define	ENV_CUBRID			"CUBRID"
#define	ENV_CUBRID_DATABASES	"CUBRID_DATABASES"
#define	ENV_UNICAS			"CUBRID_BROKER"

/* filename */
#define	FILE_ORDBLIST		"ordblist.txt"
#define	FILE_ORDBINFO		"ordbinfo.txt"
#define	FILE_UNICAS_CONF	"conf\\cubrid_broker.conf"

#endif