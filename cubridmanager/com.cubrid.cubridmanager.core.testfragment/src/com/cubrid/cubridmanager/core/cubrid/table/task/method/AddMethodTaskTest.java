package com.cubrid.cubridmanager.core.cubrid.table.task.method;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBMethod;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class AddMethodTaskTest extends SetupEnvTestCase {
	String table = "business";
	String dbname = "demodb";
	String methodname = "testmethodname";
	String implementation = "testfunction";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/method/test.message/addmethod_send");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddMethodTask task = new AddMethodTask(site);
		task.setDbName(dbname);
		task.setClassName(table);
		task.setMethodName(methodname);
		task.setImplementation(implementation);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
		

		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/method/test.message/addmethod_send_class");
		msg = Tool.getFileContent(filepath);	
		
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddMethodTask task2 = new AddMethodTask(site);	
	
		task2.setDbName(dbname);
		task2.setClassName("business");
		task2.setMethodName("testclass");
		task2.setImplementation("testclassfunc");
		task2.setCategory(AttributeCategory.CLASS);
		// compare
		assertEquals(msg, task2.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/method/test.message/addmethod_receive");
		String msg = Tool.getFileContent(filepath);	
		
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals(dbname, classinfo.getDbname());
		assertEquals(table, classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test instance method
		List<DBMethod> methods = classinfo.getMethods();
		assertNotNull(methods);
		DBMethod method=classinfo.getDBMethodByName(methodname);
		assertNotNull(method);
		assertEquals(implementation, method.getFunction());
		assertEquals(table, method.getInherit());
		assertEquals("void", method.getArguments().get(0));
		// test class method
		List<DBMethod> classmethods = classinfo.getClassMethods();
		assertNotNull(classmethods);
		DBMethod classmethod=classinfo.getDBMethodByName("add_new_business");
		assertNotNull(classmethod);
		assertEquals("new_business", classmethod.getFunction());
		assertEquals("business", classmethod.getInherit());
		assertEquals("", classmethod.getArguments().get(0));
		assertEquals("character varying(1073741823)", classmethod.getArguments().get(1));
		
		
	}
}
