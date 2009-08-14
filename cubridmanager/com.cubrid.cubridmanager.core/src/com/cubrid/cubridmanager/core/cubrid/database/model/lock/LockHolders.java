package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

public class LockHolders {
	private int tran_index       ;
	private String granted_mode     ;
	private int count            ;
	private int nsubgranules     ;
	
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
}
