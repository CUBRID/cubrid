package com.cubrid.cubridmanager.core.cubrid.database.model.transaction;

import java.util.ArrayList;
import java.util.List;

public class TransactionInfo {
	private List<Transaction> transactionList;

	public List<Transaction> getTransactionList() {
    	return transactionList;
    }

	public void setTransactionList(List<Transaction> transactionList) {
    	this.transactionList = transactionList;
    }
	/*******add the model to list  by reflect method*********/
	public void addTransaction(Transaction bean) {
		if (transactionList == null)
			transactionList = new ArrayList<Transaction>();
		this.transactionList.add(bean);
	}
}
