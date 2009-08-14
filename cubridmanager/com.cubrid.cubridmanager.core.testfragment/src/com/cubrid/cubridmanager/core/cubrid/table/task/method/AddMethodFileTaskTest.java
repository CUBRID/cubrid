package com.cubrid.cubridmanager.core.cubrid.table.task.method;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class AddMethodFileTaskTest extends SetupEnvTestCase {
	String methodfilename = "aaa";
	String table = "sub_athelete";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/method/test.message/addmethodfile_send");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddMethodFileTask task = new AddMethodFileTask(site);
		task.setDbName("demodb");

		task.setClassName(table);

		task.setMethodFile(methodfilename);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/method/test.message/addmethodfile_receive");
		String msg = Tool.getFileContent(filepath);		
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals(table, classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test FK constraint
		List<String> methodfiles = classinfo.getMethodFiles();
		assertNotNull(methodfiles);
		assertNotSame(-1, methodfiles.indexOf(methodfilename));
		
	}
}
