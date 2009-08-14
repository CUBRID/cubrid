package com.cubrid.cubridmanager.core.common.task;

import java.util.HashMap;
import java.util.Map;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class SetCMConfParameterTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/setcmconfpara_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		SetCMConfParameterTask task = new SetCMConfParameterTask(site);
		Map<String, String> map = new HashMap<String, String>();
		map.put(ConfConstants.cm_port, "8001");
		map.put(ConfConstants.allow_user_multi_connection, "YES");
		map.put(ConfConstants.server_long_query_time, "10");
		map.put(ConfConstants.monitor_interval, "5");

		task.setConfParameters(map);
		//compare 
		assertEquals(msg, task.getRequest());

	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/setcmconfpara_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));

	}
}