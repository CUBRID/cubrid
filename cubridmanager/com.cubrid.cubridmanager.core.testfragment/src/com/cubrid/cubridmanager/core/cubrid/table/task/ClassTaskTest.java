package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class ClassTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/class_send");
		String msg = Tool.getFileContent(filepath);	
		

		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		ClassTask task=new ClassTask(site);
		task.setDbName("demodb");
		task.setClassName("_db_attribute");		
		//compare 
		assertEquals(msg, task.getRequest());
	}
	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/class_receive");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		node=node.getChildren().get(0);
		assertEquals("demodb", node.getValue("dbname"));
		assertEquals("_db_attribute", node.getValue("classname"));
		assertEquals("system", node.getValue("type"));		

	}
}
