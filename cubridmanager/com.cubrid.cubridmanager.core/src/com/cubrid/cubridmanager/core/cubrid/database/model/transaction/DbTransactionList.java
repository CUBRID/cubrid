package com.cubrid.cubridmanager.core.cubrid.database.model.transaction;

import com.cubrid.cubridmanager.core.common.model.IModel;

public class DbTransactionList implements IModel {

	private String dbname;
	private TransactionInfo transationInfo;

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public String getTaskName() {

		return "gettransactioninfo";
	}

	public TransactionInfo getTransationInfo() {
		return transationInfo;
	}

	/** *****add the model to list by reflect method******** */
				   
	public void addTransactionInfo(TransactionInfo transationInfo) {
		this.transationInfo = transationInfo;
	}

	public void setTransationInfo(TransactionInfo transationInfo) {
    	this.transationInfo = transationInfo;
    }
}
