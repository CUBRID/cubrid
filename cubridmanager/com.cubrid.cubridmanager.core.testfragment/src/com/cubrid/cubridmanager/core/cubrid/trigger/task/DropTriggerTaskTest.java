package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class DropTriggerTaskTest extends SetupEnvTestCase {
	String dbname = "demodb";
	

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/droptrigger_send");
		String msg = Tool.getFileContent(filepath);	

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropTriggerTask task = new DropTriggerTask(site);
		task.setDbName(dbname);	
		task.setTriggerName("update_monitor");
		// compare
		assertEquals(msg, task.getRequest());

	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/droptrigger_receive");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);

		String status = node.getValue("status");

		boolean success;
		if (status.equals("success")) {
			success = true;
		} else {

			success = false;
		}
		assertEquals(true, success);	
	}
}
