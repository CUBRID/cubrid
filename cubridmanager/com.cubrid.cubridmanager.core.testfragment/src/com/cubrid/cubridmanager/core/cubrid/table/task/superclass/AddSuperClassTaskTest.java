package com.cubrid.cubridmanager.core.cubrid.table.task.superclass;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class AddSuperClassTaskTest extends SetupEnvTestCase {
	String table = "sub_athelete";
	String dbname="demodb";
	String superclass = "_db_auth";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/addsuper_send");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddSuperClassTask task = new AddSuperClassTask(site);
		task.setDbName(dbname);
		task.setClassName(table);

		task.setSuperClass(superclass);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/addsuper_receive");
		String msg = Tool.getFileContent(filepath);
		

		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals(dbname, classinfo.getDbname());
		assertEquals(table, classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test FK constraint
		List<String> supers = classinfo.getSuperClasses();
		assertNotNull(supers);
		assertNotSame(-1, supers.indexOf(superclass));
		
	}
}
