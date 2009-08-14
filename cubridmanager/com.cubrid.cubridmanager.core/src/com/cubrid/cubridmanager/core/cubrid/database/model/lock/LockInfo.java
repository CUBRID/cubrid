package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

import java.util.ArrayList;
import java.util.List;

public class LockInfo {
	private int esc;
	private int dinterval;
	private List<DatabaseTransaction> transaction;

	private DbLotInfo dbLotInfo;

	public int getEsc() {
    	return esc;
    }

	public void setEsc(int esc) {
    	this.esc = esc;
    }

	public int getDinterval() {
    	return dinterval;
    }

	public void setDinterval(int dinterval) {
    	this.dinterval = dinterval;
    }

	public List<DatabaseTransaction> getTransaction() {
    	return transaction;
    }

	public void addTransaction(DatabaseTransaction bean) {

		if (transaction == null)
			transaction = new ArrayList<DatabaseTransaction>();
		this.transaction.add(bean);
    }

	public DbLotInfo getDbLotInfo() {
    	return dbLotInfo;
    }

	public void addLot(DbLotInfo dbLotInfo) {
    	this.dbLotInfo = dbLotInfo;
    }
	 
}
