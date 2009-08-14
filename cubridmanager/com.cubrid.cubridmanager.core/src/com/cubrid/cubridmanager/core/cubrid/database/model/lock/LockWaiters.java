package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

public class LockWaiters {

	private int tran_index;
	private String b_mode;
	private String start_at;
	private String waitfornsec;

	public int getTran_index() {
		return tran_index;
	}

	public void setTran_index(int tran_index) {
		this.tran_index = tran_index;
	}

	public String getB_mode() {
		return b_mode;
	}

	public void setB_mode(String b_mode) {
		this.b_mode = b_mode;
	}

	public String getStart_at() {
		return start_at;
	}

	public void setStart_at(String start_at) {
		this.start_at = start_at == null ? null : start_at.trim(); // eg. [Mon Jun 29 21:29:12 2009       ].trim()
	}

	public String getWaitfornsec() {
		return waitfornsec;
	}

	public void setWaitfornsec(String waitfornsec) {
		this.waitfornsec = waitfornsec;
	}
}
