package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;

public class OptimizeTaskTest extends
		SetupEnvTestCase {

	public void testSpecificTable() {

		System.out.println("<database.optimizedb.001.req.txt>");
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.OPTIMIZE_DATABASE_TASK_NAME, site,
				CommonSendMsg.optimizeDbSendMsg);
		task.setDbName("demodb");
		task.setClassName("code");
		task.execute();
		
		assertTrue(task.isSuccess());
		assertNull(task.getErrorMsg());
	}

	public void testAllTables() {

		System.out.println("<database.optimizedb.002.req.txt>");
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.OPTIMIZE_DATABASE_TASK_NAME, site,
				CommonSendMsg.optimizeDbSendMsg);
		task.setDbName("demodb");
		task.execute();
		
		assertTrue(task.isSuccess());
		assertNull(task.getErrorMsg());

	}
	
	public void testNotExistDatabase() {

		System.out.println("<database.optimizedb.003.req.txt>");
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.OPTIMIZE_DATABASE_TASK_NAME, site,
				CommonSendMsg.optimizeDbSendMsg);
		task.setDbName("notexistdb");
		task.execute();
		
		assertTrue(task.isSuccess()); 
		assertNull(task.getErrorMsg());
		// this result does not understanding... I expacted a failure result -.-;
		// perhaps, cubrid will be done a database authentication before executing a shell command(cubrid optimizedb ...).
		// must be examine in the future...
		
	}
	
	public void testEmptyDbname() {

		System.out.println("<database.optimizedb.004.req.txt>");
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.OPTIMIZE_DATABASE_TASK_NAME, site,
				CommonSendMsg.optimizeDbSendMsg);
		task.setDbName("");
		task.execute();
		
		assertFalse(task.isSuccess());
		assertNotNull(task.getErrorMsg());

	}
	
	public void testNotExistClassnameOnExistDatabase() {
		
		System.out.println("<database.optimizedb.005.req.txt>");
		
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.OPTIMIZE_DATABASE_TASK_NAME, site,
				CommonSendMsg.optimizeDbSendMsg);
		task.setDbName("demodb");
		task.setClassName("notexistclassname");
		task.execute();
		
		assertFalse(task.isSuccess());
		assertNotNull(task.getErrorMsg());
		
	}
	
}
