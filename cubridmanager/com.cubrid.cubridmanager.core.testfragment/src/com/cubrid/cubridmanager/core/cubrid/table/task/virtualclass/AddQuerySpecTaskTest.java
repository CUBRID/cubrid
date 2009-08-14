package com.cubrid.cubridmanager.core.cubrid.table.task.virtualclass;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class AddQuerySpecTaskTest extends SetupEnvTestCase {
	String dbname="demodb";
	String vtable = "v_code";
	String queryspec="select code as aaa from code";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/addqueryspec_send");
		String msg = Tool.getFileContent(filepath);
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddQuerySpecTask task = new AddQuerySpecTask(site);
		task.setDbName(dbname);
		task.setVirtualClassName(vtable);
		task.setQuerySpec(queryspec);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/addqueryspec_receive");
		String msg = Tool.getFileContent(filepath);
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals(dbname, classinfo.getDbname());
		List<String> list=classinfo.getQuerySpecs();
		boolean found=false;
		for(String item:list){
			if(item.equals(queryspec)){
				found=true;
			}
		}
		assertEquals(true,found);		
	}
}