package com.cubrid.cubridmanager.core.common.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class UpdateCMUserTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/updatecmuser_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		UpdateCMUserTask task = new UpdateCMUserTask(site);
		task.setCmUserName("qiren");
		task.setCasAuth("admin");
		task.setDbCreator("none");
		task.setStatusMonitorAuth("monitor");
		task.setDbAuth(new String[] { "pang", "demodb" }, new String[] { "dba",
				"dba" }, new String[] { "", "" }, new String[] { "33000",
				"30000" });
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/updatecmuser_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));

	}

}