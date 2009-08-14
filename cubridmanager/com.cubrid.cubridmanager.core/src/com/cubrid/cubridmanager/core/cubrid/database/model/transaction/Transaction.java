package com.cubrid.cubridmanager.core.cubrid.database.model.transaction;

public class Transaction {
	
	private String tranindex;
	private String user;
	private String host;
	private String pid;
	private String program;

	public String getTranindex() {
		return tranindex;
	}

	public void setTranindex(String tranindex) {
		this.tranindex = tranindex;
	}

	public String getUser() {
		return user;
	}

	public void setUser(String user) {
		this.user = user;
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

	public String getProgram() {
		return program;
	}

	public void setProgram(String program) {
		this.program = program;
	}
}
