#include <StdAfx.h>
#include <afxtempl.h>
#include "UCConf.h"
#include "env.h"
#include "message.h"
#include "io.h"

extern CEnv		theEnv;
/////////////////////////////////////////////////////////////////////////////////////////
//
//
CUCConf::CUCConf()
{
	sprintf(m_unicasconf, "%s\\%s", theEnv.GetUniCAS(), FILE_UNICAS_CONF);
	sprintf(m_unicasconf_old, "%s\\%s.old", theEnv.GetUniCAS(), FILE_UNICAS_CONF);
}

CUCConf::~CUCConf()
{
CUCInfo	*ucinfo;

	while (!m_List.IsEmpty()) {
		ucinfo = (CUCInfo *)m_List.RemoveHead();
		delete ucinfo;
	}

}

bool CUCConf::Read()
{
CUCInfo			*ucinfo;
CStdioFile		unicasconf;
CFileException	e;
CString			data;
char			*token, *value;


	ucinfo = NULL;

	if (unicasconf.Open(m_unicasconf, CFile::modeRead)) {
		while (1) {
			data.Empty();
			TRY {
				if (!unicasconf.ReadString(data)) break;
				data.TrimLeft();
				data.TrimRight();
				if (data.IsEmpty()) continue;

				data.MakeLower();

				if (data[0] == '%') {
					if (data[1] == '%') {
						data.TrimLeft("% ");
						token = strtok((char *)LPCSTR(data), " ");
						value = strtok(NULL, " ");
						if (!strcmp(token, MASTER_SHM_ID)) {
							m_master_shm_id = value;
						} else if (!strcmp(token, ADMIN_LOG_FILE)) {
							m_admin_log_file = value;
							m_admin_log_file.MakeLower();
						}
					} else {
						if (ucinfo)	m_List.AddTail(ucinfo);
						ucinfo = new CUCInfo();
						data.TrimLeft("% ");
						ucinfo->m_broker = data;
					}
				} else {
					ucinfo->Parse(data);
				}
			}
			CATCH_ALL(e)
			{
				break;
			}
			END_CATCH_ALL
		}
		if (ucinfo) m_List.AddTail(ucinfo);
		unicasconf.Close();
	}

	return true;
}

bool CUCConf::Write()
{
CUCInfo *ucinfo;
CStdioFile unicasconf;
CString	element;
CFileException	e;
int		count;

	if (m_List.IsEmpty()) return false;

	if (!_access(m_unicasconf_old, 0)) {
		_unlink(m_unicasconf_old);
	}
	unicasconf.Rename(m_unicasconf, m_unicasconf_old);

	if (!unicasconf.Open(m_unicasconf, CFile::modeCreate | CFile::modeWrite, &e)) return false;

	element.Format("%%%% %-23s %s\n", MASTER_SHM_ID, m_master_shm_id);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));
	element.Format("%%%% %-23s %s\n", ADMIN_LOG_FILE, m_admin_log_file);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));
	unicasconf.WriteString("\n\n");

	count = (int) m_List.GetCount();
	for (int i = 0; i < count; i++) {
		ucinfo = (CUCInfo *)m_List.GetAt(m_List.FindIndex(i));
		ucinfo->Compose(unicasconf);
		unicasconf.WriteString("\n\n");
	}

	unicasconf.Close();

	return true;
}


/////////////////////////////////////////////////////////////////////////////////////////
//
//
CUCInfo::CUCInfo()
{
	m_broker.Empty();
	m_service = false;
	m_broker_port.Empty();
	m_appl_server = 0;
	m_min_num_appl_server = 1;
	m_max_num_appl_server = 5;
	m_auto_add_appl_server = false;
	m_appl_server_shm_id.Empty();
	m_appl_server_max_size = 10;
	m_log_dir = "log";
	m_log_backup = false;
	m_source_env = "CUBRID.env";
	m_sql_log = true;
	m_time_to_kill = "3";
	m_session_timeout = "60";
	m_job_queue_size = "30";
	m_access_log = true;
	m_security = false;
	m_access_list = "access_list";

	m_session = true;
	m_appl_root = "script";
	m_error_log = 0;
	m_entry_value_trim = true;
	m_oid_check = true;
	m_enc_appl = false;

	m_set_cookie = true;
	m_file_upload_temp_dir = "tmp";
	m_file_upload_delimiter = "^^^";

	m_compress_size = "3";
	m_priority_gap = "10";

//	m_pointer = NULL;
}

CUCInfo::~CUCInfo()
{
}

void CUCInfo::Parse(CString data)
{
char	*token, *value;
bool	comment = false;

	if (data[0] == '#') {
		comment = true;
		data.TrimLeft("# ");
	}
	if (data.IsEmpty()) return;

	token = strtok((char *)LPCSTR(data), " ");
	value = strtok(NULL, " ");

	if (!token || !value) return;
	if (!strcmp(token, SERVICE)) {
		if (!strcmp(value, ON)) {
			m_service = true;
		} else if (!strcmp(value, OFF)) {
			m_service = false;
		}
	} else if (!strcmp(token, BROKER_PORT)) {
		m_broker_port = value;
	} else if (!strcmp(token, APPL_SERVER)) {
		if (!strcmp(value, STR_CAS)) {
			m_appl_server = NO_CAS;
		} else if (!strcmp(value, STR_WAS)) {
			m_appl_server = NO_WAS;
		} else if (!strcmp(value, STR_VAS)) {
			m_appl_server = NO_VAS;
		} else if (!strcmp(value, STR_ULS)) {
			m_appl_server = NO_ULS;
		} else if (!strcmp(value, STR_AMS)) {
			m_appl_server = NO_AMS;
		}
	} else if (!strcmp(token, MIN_NUM_APPL_SERVER)) {
		m_min_num_appl_server = atoi(value);
	} else if (!strcmp(token, MAX_NUM_APPL_SERVER)) {
		m_max_num_appl_server = atoi(value);
	} else if (!strcmp(token, AUTO_ADD_APPL_SERVER)) {
		if (!strcmp(value, ON)) {
			m_auto_add_appl_server = true;
		} else if (!strcmp(value, OFF)) {
			m_auto_add_appl_server = false;
		}
	} else if (!strcmp(token, APPL_SERVER_SHM_ID)) {
		m_appl_server_shm_id = value;
	} else if (!strcmp(token, APPL_SERVER_MAX_SIZE)) {
		m_appl_server_max_size = atoi(value);
	} else if (!strcmp(token, LOG_DIR)) {
		m_log_dir = value;
	} else if (!strcmp(token, LOG_BACKUP)) {
		if (!strcmp(value, ON)) {
			m_log_backup = true;
		} else if (!strcmp(value, OFF)) {
			m_log_backup = false;
		}
	} else if (!strcmp(token, SOURCE_ENV)) {
		m_source_env = value;
	} else if (!strcmp(token, SQL_LOG)) {
		if (!strcmp(value, ON)) {
			m_sql_log = true;
		} else if (!strcmp(value, OFF)) {
			m_sql_log = false;
		}
	} else if (!strcmp(token, TIME_TO_KILL)) {
		m_time_to_kill = value;
	} else if (!strcmp(token, SESSION_TIMEOUT)) {
		m_session_timeout = value;
	} else if (!strcmp(token, JOB_QUEUE_SIZE)) {
		m_job_queue_size = value;
	} else if (!strcmp(token, ACCESS_LOG)) {
		if (!strcmp(value, ON)) {
			m_access_log = true;
		} else if (!strcmp(value, OFF)) {
			m_access_log = false;
		}
	} else if (!strcmp(token, ACCESS_LIST)) {
		m_security = !comment;
		m_access_list = value;
	} else if (!strcmp(token, SESSION)) {
		if (!strcmp(value, ON)) {
			m_session = true;
		} else if (!strcmp(value, OFF)) {
			m_session = false;
		}
	} else if (!strcmp(token, APPL_ROOT)) {
		m_appl_root = value;
	} else if (!strcmp(token, ERROR_LOG)) {
		if (!strcmp(value, STR_BOTH)) {
			m_error_log = NO_BOTH;
		} else if (!strcmp(value, STR_BROWSER)) {
			m_error_log = NO_BROWSER;
		} else if (!strcmp(value, STR_LOGFILE)) {
			m_error_log = NO_LOGFILE;
		} else if (!strcmp(value, STR_NONE)) {
			m_error_log = NO_NONE;
		}
	} else if (!strcmp(token, ENTRY_VALUE_TRIM)) {
		if (!strcmp(value, ON)) {
			m_entry_value_trim = true;
		} else if (!strcmp(value, OFF)) {
			m_entry_value_trim = false;
		}
	} else if (!strcmp(token, OID_CHECK)) {
		if (!strcmp(value, ON)) {
			m_oid_check = true;
		} else if (!strcmp(value, OFF)) {
			m_oid_check = false;
		}
	} else if (!strcmp(token, ENC_APPL)) {
		if (!strcmp(value, ON)) {
			m_enc_appl = true;
		} else if (!strcmp(value, OFF)) {
			m_enc_appl = false;
		}
	} else if (!strcmp(token, SET_COOKIE)) {
		if (!strcmp(value, ON)) {
			m_set_cookie = true;
		} else if (!strcmp(value, OFF)) {
			m_set_cookie = false;
		}
	} else if (!strcmp(token, FILE_UPLOAD_TEMP_DIR)) {
		m_file_upload_temp_dir = value;
	} else if (!strcmp(token, FILE_UPLOAD_DELIMITER)) {
		m_file_upload_delimiter = value;
	} else if (!strcmp(token, COMPRESS_SIZE)) {
		m_compress_size = value;
	} else if (!strcmp(token, PRIORITY_GAP)) {
		m_priority_gap = value;
	}
}

void CUCInfo::Compose(CStdioFile &unicasconf)
{
CString	element;

	element.Format("%% %-23s\n", m_broker);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", SERVICE, m_service ? ON : OFF);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", BROKER_PORT, m_broker_port);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	if (m_appl_server == NO_CAS) { // cas
		element.Format("%-23s %s\n", APPL_SERVER, STR_CAS);
	} else if (m_appl_server == NO_WAS) { // was
		element.Format("%-23s %s\n", APPL_SERVER, STR_WAS);
	} else if (m_appl_server == NO_VAS) { // vas
		element.Format("%-23s %s\n", APPL_SERVER, STR_VAS);
	} else if (m_appl_server == NO_ULS) { // uls
		element.Format("%-23s %s\n", APPL_SERVER, STR_ULS);
	} else if (m_appl_server == NO_AMS) { // ams
		element.Format("%-23s %s\n", APPL_SERVER, STR_AMS);
	}
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %d\n", MIN_NUM_APPL_SERVER, m_min_num_appl_server);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %d\n", MAX_NUM_APPL_SERVER, m_max_num_appl_server);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", AUTO_ADD_APPL_SERVER, m_auto_add_appl_server ? ON : OFF);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", APPL_SERVER_SHM_ID, m_appl_server_shm_id);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %d\n", APPL_SERVER_MAX_SIZE, m_appl_server_max_size);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", LOG_DIR, m_log_dir);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", LOG_BACKUP, m_log_backup ? ON : OFF);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", SOURCE_ENV, m_source_env);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", SQL_LOG, m_sql_log ? "ON" : "OFF");
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", TIME_TO_KILL, m_time_to_kill);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", SESSION_TIMEOUT, m_session_timeout);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", JOB_QUEUE_SIZE, m_job_queue_size);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	element.Format("%-23s %s\n", ACCESS_LOG, m_access_log ? ON : OFF);
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	if (m_security) {
		element.Format("%-23s %s\n", ACCESS_LIST, m_access_list);
	} else {
		element.Format("#%-23s %s\n", ACCESS_LIST, m_access_list);
	}
	element.MakeUpper();
	unicasconf.WriteString(LPCSTR(element));

	if (m_appl_server == NO_WAS || m_appl_server == NO_VAS) {
		element.Format("%-23s %s\n", SESSION, m_session ? ON : OFF);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", APPL_ROOT, m_appl_root);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		if (m_error_log == NO_BOTH) {			// BOTH
			element.Format("%-23s %s\n", ERROR_LOG, STR_BOTH);
		} else if (m_error_log == NO_BROWSER) {	// BROWSER
			element.Format("%-23s %s\n", ERROR_LOG, STR_BROWSER);
		} else if (m_error_log == NO_LOGFILE) {	// LOGFILE
			element.Format("%-23s %s\n", ERROR_LOG, STR_LOGFILE);
		} else if (m_error_log == NO_NONE) {	// NONE
			element.Format("%-23s %s\n", ERROR_LOG, STR_NONE);
		}
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", ENTRY_VALUE_TRIM, m_entry_value_trim ? ON : OFF);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", OID_CHECK, m_oid_check ? ON : OFF);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", ENC_APPL, m_enc_appl ? ON : OFF);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));
	}

	if (m_appl_server == NO_WAS) {
		element.Format("%-23s %s\n", SET_COOKIE, m_set_cookie ? ON : OFF);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", FILE_UPLOAD_TEMP_DIR, m_file_upload_temp_dir);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", FILE_UPLOAD_DELIMITER, m_file_upload_delimiter);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));
	}

	if (m_appl_server == NO_VAS) {
		element.Format("%-23s %s\n", COMPRESS_SIZE, m_compress_size);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));

		element.Format("%-23s %s\n", PRIORITY_GAP, m_priority_gap);
		element.MakeUpper();
		unicasconf.WriteString(LPCSTR(element));
	}
}
