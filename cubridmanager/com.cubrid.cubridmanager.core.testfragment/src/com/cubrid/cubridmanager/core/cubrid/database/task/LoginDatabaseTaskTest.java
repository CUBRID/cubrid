package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class LoginDatabaseTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/logindb_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		LoginDatabaseTask task = new LoginDatabaseTask(site);
		task.setDbName("demodb");
		task.setCMUser("admin");
		task.setDbPassword("");
		task.setDbUser("dba");
		assertEquals(msg, task.getRequest());
		//compare 

	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/logindb_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		node.toTreeString();
		node.getValueByMap();
		node.getResponseMessage();
		//compare 
		assertEquals("success", node.getValue("status"));
		assertEquals("isdba", node.getValue("authority"));

	}

}