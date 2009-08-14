package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class LoadDbTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/loaddb_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		LoadDbTask task = new LoadDbTask(site);
		task.setDbName("testdb2");
		task.setCheckOption("both");
		task.setUsedPeriod(false, "none");
		task.setDbUser("dba");
		task.setUsedEstimatedSize(false, "none");
		task.setNoUsedOid(false);
		task.setNoUsedLog(false);
		task.setSchemaPath("C:\\CUBRID\\DATABA~1\\demodb/demodb_schema");
		task.setObjectPath("C:\\CUBRID\\DATABA~1\\demodb/demodb_objects");
		task.setIndexPath("none");
		task.setUsedErrorContorlFile(true, "error");
		task.setUsedErrorContorlFile(false, "error");
		task.setUsedIgnoredClassFile(true, "error");
		task.setUsedIgnoredClassFile(false, "error");
		task.setUsedIgnoredClassFile(true, "error");
		task.setUsedIgnoredClassFile(false, "error");
		
		task.getLoadResult();
		
		//compare 
		//assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/loaddb_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		assertTrue(node.getValues("line").length > 0);
	}

}