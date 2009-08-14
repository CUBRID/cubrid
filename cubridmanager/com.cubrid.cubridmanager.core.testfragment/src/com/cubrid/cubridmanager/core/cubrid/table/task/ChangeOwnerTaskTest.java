package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class ChangeOwnerTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/changeowner_send");
		String msg = Tool.getFileContent(filepath);	
		

		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		ChangeOwnerTask task=new ChangeOwnerTask(site);
		task.setDbName("demodb");
		task.setClassName("game");		
		task.setOwnerName("DBA");
		//compare 
		assertEquals(msg, task.getRequest());
	}
	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/changeowner_receive");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		node=node.getChildren().get(0);
		assertEquals("demodb", node.getValue("dbname"));
		assertEquals("game", node.getValue("classname"));
		assertEquals("DBA", node.getValue("owner"));
	}
}
