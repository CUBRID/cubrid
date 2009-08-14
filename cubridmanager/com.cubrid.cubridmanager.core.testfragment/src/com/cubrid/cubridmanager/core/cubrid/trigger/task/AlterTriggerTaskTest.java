package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerStatus;

public class AlterTriggerTaskTest extends SetupEnvTestCase {
	String dbname = "demodb";
	

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/altertrigger_send");
		String msg = Tool.getFileContent(filepath);	

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AlterTriggerTask task = new AlterTriggerTask(site);
		task.setDbName(dbname);	
		task.setTriggerName("update_monitor");
		task.setStatus(TriggerStatus.INACTIVE);
		task.setPriority("0.04");
		// compare
		assertEquals(msg, task.getRequest());

	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/altertrigger_receive");
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