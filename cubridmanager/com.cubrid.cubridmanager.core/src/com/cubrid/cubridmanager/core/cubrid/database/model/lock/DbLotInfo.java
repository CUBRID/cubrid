package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

import java.util.ArrayList;
import java.util.List;

public class DbLotInfo {
	private int numlocked;                                                   
	private int maxnumlock; 
	
	private List<DbLotEntry> dbLotEntryList;

	public int getNumlocked() {
    	return numlocked;
    }

	public void setNumlocked(int numlocked) {
    	this.numlocked = numlocked;
    }

	public int getMaxnumlock() {
    	return maxnumlock;
    }

	public void setMaxnumlock(int maxnumlock) {
    	this.maxnumlock = maxnumlock;
    }

	public List<DbLotEntry> getDbLotEntryList() {
		if(dbLotEntryList==null)
			dbLotEntryList=new ArrayList<DbLotEntry>();
    	return dbLotEntryList;
    }

	public void addEntry(DbLotEntry bean) {
		if (dbLotEntryList == null)
			dbLotEntryList = new ArrayList<DbLotEntry>();
		this.dbLotEntryList.add(bean);
    }
	
}
