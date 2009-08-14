package com.cubrid.cubridmanager.core.cubrid.table.task.attribute;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class DropAttributeTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/dropattribute_send");
		String msg = Tool.getFileContent(filepath);	
		
		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		DropAttributeTask task=new DropAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("_db_attribute");
		task.setAttributeName("class_of");
		task.setCategory(AttributeCategory.INSTANCE);
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/dropattribute_receive_error");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);

		assertEquals("failure", node.getValue("status"));


		

	}
}
