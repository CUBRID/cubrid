package com.cubrid.cubridmanager.core.cubrid.table.task.superclass;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBResolution;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class AddResolutionTaskTest extends SetupEnvTestCase {
	String dbname="demodb";
	String table = "testresolution";
	String superclass="super1";
	String name="name";
	String alias="name1";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/addresolution_send");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddResolutionTask task = new AddResolutionTask(site);
		task.setDbName(dbname);
		task.setClassName(table);
		task.setSuperClass(superclass);
		task.setName(name);
		task.setAlias(alias);
		task.setCategory(AttributeCategory.INSTANCE);


		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/addresolution_receive");
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
		List<DBResolution> methodfiles = classinfo.getResolutions();
		assertNotNull(methodfiles);
		assertEquals(alias, methodfiles.get(0).getAlias());
		
	}
}
