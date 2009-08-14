package com.cubrid.cubridmanager.core.cubrid.jobauto.model.errlog;

public class BackUpErrorLog {

	private String dbname;
	private String backupid;
	private String error_time;
	private String error_desc;

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public String getBackupid() {
		return backupid;
	}

	public void setBackupid(String backupid) {
		this.backupid = backupid;
	}

	public String getError_time() {
		return error_time;
	}

	public void setError_time(String error_time) {
		this.error_time = error_time;
	}

	public String getError_desc() {
		return error_desc;
	}

	public void setError_desc(String error_desc) {
		this.error_desc = error_desc;
	}

}
