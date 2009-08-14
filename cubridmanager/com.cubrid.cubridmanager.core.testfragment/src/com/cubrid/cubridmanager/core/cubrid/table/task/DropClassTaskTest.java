package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class DropClassTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/dropclass_send");
		String msg = Tool.getFileContent(filepath);	
		

		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		DropClassTask task=new DropClassTask(site);
		task.setDbName("demodb");
		task.setVirtualClassName("v_code");		
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/dropclass_receive");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		assertEquals("demodb", node.getValue("dbname"));
		assertEquals("v_code", node.getValue("classname"));
		
		TreeNode subnode=node.getChildren().get(0);
		assertEquals("systemclass", subnode.getValue("open"));
		
		TreeNode subnode2=node.getChildren().get(1);
		assertEquals("userclass", subnode2.getValue("open"));
		
		TreeNode ssubnode=subnode.getChildren().get(0);
		assertEquals("class", ssubnode.getValue("open"));
		assertEquals("normal", ssubnode.getValue("virtual"));
		assertEquals("DBA", ssubnode.getValue("owner"));
	}
}
