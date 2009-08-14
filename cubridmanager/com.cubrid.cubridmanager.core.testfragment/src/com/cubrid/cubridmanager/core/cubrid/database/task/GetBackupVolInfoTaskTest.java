package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class GetBackupVolInfoTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/getbackupvolinfo_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		GetBackupVolInfoTask task = new GetBackupVolInfoTask(site);
		task.setDbName("demodb");
		task.setLevel("0");
		task.setPath("C:\\CUBRID\\databases\\demodb\\backup\\demodb_backup_lv0");
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/getbackupvolinfo_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		assertTrue(node.getValues("line").length > 0);
	}

}