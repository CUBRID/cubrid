package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

import com.cubrid.cubridmanager.core.common.model.IModel;

public class DatabaseLockInfo implements IModel{

	private LockInfo lockInfo;

	public LockInfo getLockInfo() {
    	return lockInfo;
    }

	public void addLockInfo(LockInfo lockInfo) {
    	this.lockInfo = lockInfo;
    }

	public String getTaskName() {
	    return "lockdb";
    }
}
