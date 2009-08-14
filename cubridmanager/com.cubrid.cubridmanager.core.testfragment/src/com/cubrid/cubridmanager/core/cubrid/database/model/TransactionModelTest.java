package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.List;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.DbTransactionList;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.KillTransactionList;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.Transaction;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.TransactionInfo;

public class TransactionModelTest extends TestCase{
	public void testModelDbTransactionList() {
		DbTransactionList bean = new DbTransactionList();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
	}

	public void testModelKillTransactionList() {
		KillTransactionList bean = new KillTransactionList();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
	}

	public void testModelTransaction() {
		Transaction bean = new Transaction();
		bean.setTranindex("tranindex");
		assertEquals(bean.getTranindex(), "tranindex");
		bean.setUser("user");
		assertEquals(bean.getUser(), "user");
		bean.setHost("host");
		assertEquals(bean.getHost(), "host");
		bean.setPid("pid");
		assertEquals(bean.getPid(), "pid");
		bean.setProgram("program");
		assertEquals(bean.getProgram(), "program");
	}

	public void testModelTransactionInfo() {
		TransactionInfo bean = new TransactionInfo();
		bean.setTransactionList(null);
		bean.addTransaction(null);

		assertEquals(bean.getTransactionList() instanceof List,true);
	}
}
