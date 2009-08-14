package com.cubrid.cubridmanager.core.common.task;

import java.util.HashMap;
import java.util.Map;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class SetCubridConfParameterTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/setcubridconfpara_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		SetCubridConfParameterTask task = new SetCubridConfParameterTask(site);
		Map<String, Map<String, String>> confParameters = new HashMap<String, Map<String, String>>();
		Map<String, String> map = new HashMap<String, String>();
		map.put(ConfConstants.service, "server,manager,broker");
		map.put(ConfConstants.server, "qiren,pang,demodb");
		confParameters.put(ConfConstants.service_section_name, map);
		map = new HashMap<String, String>();
		map.put(ConfConstants.max_clients, "50");
		confParameters.put(ConfConstants.common_section_name, map);
		task.setConfParameters(confParameters);
		//compare 
		assertEquals(msg, task.getRequest());

	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/common/task/test.message/setcubridconfpara_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));

	}
}