package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

public class DatabaseTransaction {

	private int index;

	private String pname;

	private String uid;

	private String host;

	private String pid;

	private String isolevel;

	private int timeout;

	public int getIndex() {
		return index;
	}

	public void setIndex(int index) {
		this.index = index;
	}

	public String getPname() {
		return pname;
	}

	public void setPname(String pname) {
		this.pname = pname;
	}

	public String getUid() {
		return uid;
	}

	public void setUid(String uid) {
		this.uid = uid;
	}

	public String getHost() {
		return host;
	}

	public void setHost(String host) {
		this.host = host;
	}

	public String getPid() {
		return pid;
	}

	public void setPid(String pid) {
		this.pid = pid;
	}

	public String getIsolevel() {
		return isolevel;
	}

	public void setIsolevel(String isolevel) {
		this.isolevel = isolevel;
	}

	public int getTimeout() {
		return timeout;
	}

	public void setTimeout(int timeout) {
		this.timeout = timeout;
	}
}
