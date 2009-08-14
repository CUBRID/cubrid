package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class RenameClassTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/renameclass_send");
		String msg = Tool.getFileContent(filepath);	
		

		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		RenameClassTask task=new RenameClassTask(site);
		task.setDbName("demodb");
		task.setOldClassName("v_athlete");
		task.setNewClassName("v_athelete");
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/renameclass_receive");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));	
		assertEquals("demodb", node.getValue("dbname"));
		assertEquals("v_athelete", node.getValue("classname"));
		


	}

}
