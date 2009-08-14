package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.List;
import java.util.Map;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DatabaseLockInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DatabaseTransaction;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockHolders;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockWaiters;

public class LockModelTest extends TestCase {

	public void testModelUserSendObj() {
		UserSendObj bean = new UserSendObj();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setUsername("username");
		assertEquals(bean.getUsername(), "username");
		bean.setUserpass("userpass");
		assertEquals(bean.getUserpass(), "userpass");
		bean.addGroups("1");
		assertEquals(bean.getGroups() instanceof List, true);
		bean.addAddmembers(new String());
		assertEquals(bean.getAddmembers() instanceof List, true);
		bean.addRemovemembers(new String());
		assertEquals(bean.getRemovemembers() instanceof List, true);
		bean.addAuthorization("1","1");
		assertEquals(bean.getAuthorization() instanceof Map, true);
	}

	public void testModelLockInfo() {
		LockInfo bean = new LockInfo();
		bean.setEsc(3);
		assertEquals(bean.getEsc(), 3);
		bean.setDinterval(9);
		assertEquals(bean.getDinterval(), 9);
		bean.addTransaction(new DatabaseTransaction());
		assertEquals(bean.getTransaction() instanceof List, true);
	}

	public void testModelDatabaseTransaction() {
		DatabaseTransaction bean = new DatabaseTransaction();
		bean.setIndex(5);
		assertEquals(bean.getIndex(), 5);
		bean.setPname("pname");
		assertEquals(bean.getPname(), "pname");
		bean.setUid("uid");
		assertEquals(bean.getUid(), "uid");
		bean.setHost("host");
		assertEquals(bean.getHost(), "host");
		bean.setPid("pid");
		assertEquals(bean.getPid(), "pid");
		bean.setIsolevel("isolevel");
		assertEquals(bean.getIsolevel(), "isolevel");
		bean.setTimeout(7);
		assertEquals(bean.getTimeout(), 7);
	}

	public void testModelLockHolders() {
		LockHolders bean = new LockHolders();
		bean.setTran_index(10);
		assertEquals(bean.getTran_index(), 10);
		bean.setGranted_mode("granted_mode");
		assertEquals(bean.getGranted_mode(), "granted_mode");
		bean.setCount(5);
		assertEquals(bean.getCount(), 5);
		bean.setNsubgranules(12);
		assertEquals(bean.getNsubgranules(), 12);
	}

	public void testModelLockWaiters() {
		LockWaiters bean = new LockWaiters();
		bean.setTran_index(10);
		assertEquals(bean.getTran_index(), 10);
		bean.setB_mode("b_mode");
		assertEquals(bean.getB_mode(), "b_mode");
		bean.setStart_at("start_at");
		assertEquals(bean.getStart_at(), "start_at");
		bean.setWaitfornsec("waitfornsec");
		assertEquals(bean.getWaitfornsec(), "waitfornsec");
	}

	public void testModelDatabaseLockInfo() {
		DatabaseLockInfo bean = new DatabaseLockInfo();
		bean.addLockInfo(new LockInfo());
		assertEquals(bean.getLockInfo() instanceof LockInfo, true);
	}
}
