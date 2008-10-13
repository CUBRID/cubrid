#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UCconf.h : header file
//

#include "Filename.h"

class CFilename;
/////////////////////////////////////////////////////////////////////////////
// Unicas.conf

class CUCInfo {
public:
	CUCInfo();
	~CUCInfo();
	void Parse(CString data);
    void Compose(CStdioFile &unicasconf);


public:		// common
	CString		m_broker;		// % BROKER?
	bool		m_service;		// SERVICE
	CString		m_broker_port;	// BROKER_PORT
	int			m_appl_server;	// APPL_SERVER
	int			m_min_num_appl_server;	// MIN_NUM_APPL_SERVER
	int			m_max_num_appl_server;	// MAX_NUM_APPL_SERVER
	bool		m_auto_add_appl_server;	// AUTO_ADD_APPL_SERVER
	CString		m_appl_server_shm_id;	// APPL_SERVER_SHM_ID
	int			m_appl_server_max_size;	// APPL_SERVER_MAX_SIZE
	CString		m_log_dir;		// LOG_DIR
	bool		m_log_backup;	// LOG_BACKUP
	CString		m_source_env;	// SOURCE_ENV
	bool		m_sql_log;		// SQL_LOG
	CString		m_sql_log_time;	// SQL_LOG_TIME
	CString		m_time_to_kill;	// TIME_TO_KILL
	CString		m_session_timeout;	// SESSION_TIMEOUT
	CString		m_job_queue_size;	// JOB_QUEUE_SIZE
	bool		m_access_log;	// ACCESS_LOG
	bool		m_security;
	CString		m_access_list;	// ACCESS_LIST

	// was & vas common
	bool		m_session;		// SESSION
	CString		m_appl_root;	// APPL_ROOT
	int			m_error_log;	// ERROR_LOG
	bool		m_oid_check;	// OID_CHECK
	bool		m_enc_appl;

	// was
	bool		m_set_cookie;	// SET_COOKIE
	CString		m_file_upload_temp_dir;	// FILE_UPLOAD_TEMP_DIR
	CString		m_file_upload_delimiter;	// FILE_UPLOAD_DELIMITER
	bool		m_entry_value_trim;		// ENTRY_VALUE_TRIM

	// vas
	CString		m_compress_size;	// COMPRESS_SIZE
	CString		m_priority_gap;		// PRIORITY_GAP
};

class CUCConf {
public:
	CUCConf();
	~CUCConf();
	bool Read();
	bool Write();

public:
	CString		m_master_shm_id;
	CFilename	m_admin_log_file;
	CPtrList	m_List;
	char	m_unicasconf[100];
	char	m_unicasconf_old[100];
};
