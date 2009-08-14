#if !defined(__MESSAGE)
#define	__MESSAGE

#define	MAXMSG						148

/* error message */
#define	MSG_MAIN_TITLE				1
#define	MSG_TRAY_ERROR				2
#define	MSG_OS_ERROR				"Your OS isn't NT or Window 2000"
#define	MSG_ENV_ERROR				"You must have a %s variable in Evironment area.\n Set the variable in Evironment or Reboot the system."
#define	MSG_INSTALL_ERROR			4
#define	MSG_MASTER_ERROR			5
#define	MSG_START					6
#define	MSG_SERVER_ERROR			7
#define	MSG_SHUTDOWN				8
#define	MSG_SHUTDOWN_ERROR			9
#define	MSG_START_UNICAS			10
#define	MSG_START_UNICAS_ERROR		11
#define	MSG_RESTART_UNICAS			12
#define	MSG_RESTART_UNICAS_ERROR	13
#define	MSG_STOP_UNICAS				14
#define	MSG_STOP_UNICAS_ERROR		15
#define MSG_UC_CANT_MONITOR			16
#define	MSG_UNKNOWN_CUBRID			17
#define	MSG_UNKNOWN_UNICAS			18
#define	MSG_NO_VOLUME				19


#define	MSG_UCP_PATH				20
#define	MSG_UCP_FULLPATH			21
#define	MSG_UCP_DOSPATH				22
#define	MSG_UCP_FILENOTFOUND		23

/* prompt message */
#define	MSG_EXIT					24
#define	MSG_UCP_DELETE				25
#define	MSG_UC_RESTART				26


/* title for unicas property */
#define	TITLE_UCP_BROKER				27
#define	TITLE_UCP_SERVICE				28
#define	TITLE_UCP_APPL_SERVER			29
#define	TITLE_UCP_BROKER_PORT			30
#define TITLE_UCP_JOB_QUEUE				31
#define	TITLE_UCP_TIMEOUT				32
#define	TITLE_UCP_SQL_LOG				33
#define	TITLE_UCP_AUTO_ADD_APPL_SERVER	34
#define	TITLE_UCP_MIN_NUM_APPL_SERVER	35
#define	TITLE_UCP_MAX_NUM_APPL_SERVER	36

#define	TITLE_UCM_ID					37
#define	TITLE_UCM_PID					38
#define	TITLE_UCM_C						39
#define	TITLE_UCM_PORT					40
#define	TITLE_UCM_STATUS				41
#define	TITLE_UCM_LASTACCESSTIME		42
#define	TITLE_UCM_ACTION				43

/* message for each window */
/* IDD_ABOUTBOX */
#define	WN_ABT_TITLE					44
#define	WN_ABT_MSG						45
#define	WN_ABT_COPYRIGHTS				46
#define	WN_ABT_OK						47
#define	WN_ABT_VER_LABEL				48
#define	WN_ABT_VER_CUBRID				49
#define	WN_ABT_VER_UNICAS				50

/* IDD_PROPERTY */
#define	WN_DBP_TITLE					51
#define WN_DBP_PATHNAME					52
#define	WN_DBP_DBNAME					53
#define	WN_DBP_LOGPATH					54
#define	WN_DBP_CHECK					55
#define	WN_DBP_OK						56
#define	WN_DBP_CANCEL					57

/* IDD_SHOWRUNDB */
#define	WN_SRD_TITLE					58
#define	WN_SRD_GVOL						59
#define	WN_SRD_DVOL						60
#define	WN_SRD_IVOL						61
#define	WN_SRD_TVOL						62
#define	WN_SRD_OK						63
#define	WN_SRD_CANCEL					64

/* IDD_STARTSERVER */
#define	WN_STD_TITLE					65
#define	WN_STD_OK						66
#define	WN_STD_CANCEL					67

/* IDD_STOPSERVER */
#define	WN_SHD_TITLE					68
#define	WN_SHD_OK						69
#define	WN_SHD_CANCEL					70

/* IDD_UC_MONITOR */
#define	WN_UCM_TITLE					71
#define WN_UCM_MONITOR					72
#define	WN_UCM_SERVICE					73
#define	WN_UCM_SERVICE_ON				74
#define	WN_UCM_SERVICE_OFF				75
#define	WN_UCM_CANCEL					76

/* IDD_UCM_DETAIL */
#define	WN_UMD_TITLE					77
#define	WN_UMD_ADD						78
#define	WN_UMD_DROP						79
#define	WN_UMD_RESTART					80
#define	WN_UMD_CANCEL					81

/* IDD_UC_PROPERTY */
#define	WN_UCP_TITLE					82
#define	WN_UCP_NEW						83
#define	WN_UCP_UPDATE					84
#define	WN_UCP_DELETE					85
#define	WN_UCP_OK						86
#define	WN_UCP_CANCEL					87
#define	WN_UCP_MASTERINFO				88
#define	WN_UCP_SM						89
#define	WN_UCP_LOGFILE					90

/* IDD_UCP_DETAIL */
#define	WN_UPD_TITLE					91
#define	WN_UPD_COMMON_PROPERTY			92
#define	WN_UPD_BROKER					93
#define	WN_UPD_SERVICE_ON				94
#define	WN_UPD_SERVICE_OFF				95
#define	WN_UPD_APPL_SERVER				96
#define	WN_UPD_MORE						97
#define	WN_UPD_BROKER_PORT				98
#define	WN_UPD_APPL_SERVER_SHM_ID		99
#define	WN_UPD_APPLICATION_SERVER		100
#define	WN_UPD_AUTO_ADD_APPL_SERVER		101
#define	WN_UPD_MIN_NUM_APPL_SERVER		102
#define	WN_UPD_MAX_NUM_APPL_SERVER		103
#define	WN_UPD_APPL_SERVER_MAX_SIZE		104
#define	WN_UPD_LOG_INFORMATION			105
#define	WN_UPD_LOG_DIR					106
#define	WN_UPD_LOG_BACKUP_ON			107
#define	WN_UPD_LOG_BACKUP_OFF			108
#define	WN_UPD_SQL_LOG_ON				109
#define	WN_UPD_SQL_LOG_OFF				110
#define	WN_UPD_SQL_LOG_TIME				111
#define	WN_UPD_ACCESS_LOG_ON			112
#define	WN_UPD_ACCESS_LOG_OFF			113
#define	WN_UPD_SECURITY					114
#define	WN_UPD_SECURITY_ON				115
#define	WN_UPD_SECURITY_OFF				116
#define	WN_UPD_ACCESS_LIST				117
#define	WN_UPD_ETC						118
#define	WN_UPD_SOURCE_ENV				119
#define	WN_UPD_TIME_TO_KILL				120
#define	WN_UPD_SESSION_TIMEOUT			121
#define	WN_UPD_JOB_QUEUE_SIZE			122
#define	WN_UPD_OK						123
#define	WN_UPD_CANCEL					124

/* IDD_UCP_VAS */
#define	WN_WAS_TITLE					125
#define	WN_WAS_ENC_APPL					126
#define	WN_WAS_SESSION					127
#define	WN_WAS_SET_COOKIE				128
#define	WN_WAS_ENTRY_VALUE_TRIM			129
#define	WN_WAS_OID_CHECK				130
#define	WN_WAS_APPL_ROOT				131
#define	WN_WAS_FILE_UPLOAD_TEMP_DIR		132
#define	WN_WAS_ERROR_LOG				133
#define	WN_WAS_FILE_UPLOAD_DELIMITER	134
#define	WN_WAS_OK						135
#define	WN_WAS_CANCEL					136

/* IDD_UCP_WAS */
#define	WN_VAS_TITLE					137
#define	WN_VAS_ENC_APPL					138
#define	WN_VAS_SESSION					139
#define	WN_VAS_OID_CHECK				140
#define	WN_VAS_APPL_ROOT				141
#define	WN_VAS_FILE_UPLOAD_TEMP_DIR		142
#define	WN_VAS_ERROR_LOG				143
#define	WN_VAS_COMPRESS_SIZE			144
#define	WN_VAS_PRIORITY_GAP				145
#define	WN_VAS_OK						146
#define	WN_VAS_CANCEL					147

#define MSG_STOP_SERVICE            148

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

/* command & dir for cubrid */
#define	CMD_CUBRID_RELEASE	"cubrid_rel.exe"
#define	CMD_MAKEDB			"CdbWizard.exe"
#define	CMD_MASTER			"cub_master.exe"
#define	CMD_SERVER			"cub_server.exe"
#define	CMD_COMMDB			"cub_commdb.exe"
#define	CMD_SHUTDOWN_SERVER	"cub_commdb.exe -P"

/* command & dir for unicas */
#define	CMD_UNICAS_RELEASE	"bin\\unicas_ver.exe"
#define	CMD_START_UNICAS	"bin\\uc.exe start"
#define	CMD_STOP_UNICAS		"bin\\uc.exe stop"
#define	CMD_NAMED			"ucd.exe"
#define	CMD_UC_MONITOR		"monitor.exe"
#define	DIR_UC_BIN			"bin"
#define	DIR_UC_CONF			"conf"

/* environment variable */
#define	ENV_CUBRID			"CUBRID"
#define	ENV_CUBRID_DATABASES	"CUBRID_DATABASES"
#define	ENV_CUBRID_LANG	"CUBRID_LANG"

/* filename */
#define	FILE_ORDBLIST		"ordblist.txt"
#define	FILE_ORDBINFO		"ordbinfo.txt"
#define	FILE_UNICAS_CONF	"conf\\unicas.conf"

#endif
