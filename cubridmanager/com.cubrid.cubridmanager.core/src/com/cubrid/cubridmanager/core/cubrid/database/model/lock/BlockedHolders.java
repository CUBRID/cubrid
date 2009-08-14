package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

public class BlockedHolders {
	private int tran_index;
	private String granted_mode;
	private int count;
	private int nsubgranules;
	// TODO
	private String blocked_mode;
	private String Start_at;
	private String wait_for_sec;

	public int getTran_index() {
		return tran_index;
	}

	public void setTran_index(int tran_index) {
		this.tran_index = tran_index;
	}

	public String getGranted_mode() {
		return granted_mode;
	}

	public void setGranted_mode(String granted_mode) {
		this.granted_mode = granted_mode;
	}

	public int getCount() {
		return count;
	}

	public void setCount(int count) {
		this.count = count;
	}

	public int getNsubgranules() {
		return nsubgranules;
	}

	public void setNsubgranules(int nsubgranules) {
		this.nsubgranules = nsubgranules;
	}

	public String getBlocked_mode() {
		return blocked_mode;
	}

	public void setBlocked_mode(String blocked_mode) {
		this.blocked_mode = blocked_mode;
	}

	public String getStart_at() {
		return Start_at;
	}

	public void setStart_at(String start_at) {
		Start_at = start_at;
	}

	public String getWait_for_sec() {
		return wait_for_sec;
	}

	public void setWait_for_sec(String wait_for_sec) {
		this.wait_for_sec = wait_for_sec;
	}
}
