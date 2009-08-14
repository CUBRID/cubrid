package com.cubrid.cubridmanager.core.common.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class GetCubridConfParameterTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/getcubridconfpara_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		GetCubridConfParameterTask task = new GetCubridConfParameterTask(site);
		//compare 
		assertEquals(msg, task.getRequest());
		task.execute();
		assertTrue(task.getErrorMsg() == null
				|| task.getErrorMsg().trim().length() == 0);
		assertTrue(task.getConfParameters().size() > 0);
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/getcubridconfpara_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		assertTrue(node.childrenSize() > 0
				&& node.getChildren().get(0).getValue("open").equals("conflist"));
		//compare 
		assertEquals("success", node.getValue("status"));

	}
}