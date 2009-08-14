package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;

public class CompactDbTaskTest extends
		SetupEnvTestCase {

	public void testActiveDb() {
		
		System.out.println("<database.compactdb.001.req.txt>");	
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.COMPACT_DATABASE_TASK_NANE,
				ServerManager.getInstance().getServer(host, monport),
				CommonSendMsg.commonDatabaseSendMsg);
		task.setDbName("activedb");
		task.execute();
		
		assertFalse(task.isSuccess());
		assertNotNull(task.getErrorMsg());
		
	}
	
	public void testInActiveDb() {
		
		System.out.println("<database.compactdb.002.req.txt>");	
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.COMPACT_DATABASE_TASK_NANE,
				ServerManager.getInstance().getServer(host, monport),
				CommonSendMsg.commonDatabaseSendMsg);
		task.setDbName("inactivedb");
		//task.execute();
		
		assertTrue(task.isSuccess());
		assertNull(task.getErrorMsg());
		
	}
	
	public void testNotExistDb() {
		
		System.out.println("<database.compactdb.003.req.txt>");	
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.COMPACT_DATABASE_TASK_NANE,
				ServerManager.getInstance().getServer(host, monport),
				CommonSendMsg.commonDatabaseSendMsg);
		task.setDbName("notexistdb");
		task.execute();
		
		assertFalse(task.isSuccess());
		assertNotNull(task.getErrorMsg());
		
	}
	
}
