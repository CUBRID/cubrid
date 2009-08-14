package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class GetBackupListTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/getbackuplist_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		GetBackupListTask task = new GetBackupListTask(site);
		task.setDbName("demodb");
		//compare 
		assertEquals(msg, task.getRequest());
		task.execute();
		assertTrue(task.getDbBackupList() != null);

	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/getbackuplist_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);

		//compare 
		assertEquals("success", node.getValue("status"));
		assertEquals(node.getValue("level0"),
				"C:\\CUBRID\\databases\\testdb\\backup\\testdb_backup_lv0");

	}

}