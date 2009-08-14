package com.cubrid.cubridmanager.core.cubrid.table.task.virtualclass;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class ValidateVirtualClassTest extends SetupEnvTestCase {
	String dbname="demodb";
	String vtable = "v_code";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/validatevirtualclass_send");
		String msg = Tool.getFileContent(filepath);	
		
	
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		ValidateVirtualClass task = new ValidateVirtualClass(site);
		task.setDbName(dbname);
		task.setVirtualClassName(vtable);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/validatevirtualclass_receive");
		String msg = Tool.getFileContent(filepath);	
		
	
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals(dbname, classinfo.getDbname());
		
	}
}