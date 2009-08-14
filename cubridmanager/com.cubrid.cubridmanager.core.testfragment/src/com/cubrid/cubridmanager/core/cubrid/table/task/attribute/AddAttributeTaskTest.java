package com.cubrid.cubridmanager.core.cubrid.table.task.attribute;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.CreateClassTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.DropClassTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class AddAttributeTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/addattribute_send");
		String msg = Tool.getFileContent(filepath);
		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		AddAttributeTask task = new AddAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setAttributeName("t");
		task.setType("CHAR(2)");
		task.setDefault("");
		task.setCategory(AttributeCategory.INSTANCE);
		task.setUnique(true);
		task.setNotNull(true);
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/addattribute_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);

		TreeNode classinfo = node.getChildren().get(0);
		SchemaInfo table = ModelUtil.getSchemaInfo(classinfo);
		// basic information
		assertEquals("demodb", table.getDbname());
		assertEquals("game", table.getClassname());
		assertEquals("user", table.getType());
		assertEquals("PUBLIC", table.getOwner());
		assertEquals("normal", table.getVirtual());
		// attribute test
		DBAttribute attribute = table.getDBAttributeByName("host_year", false);
		assertNotNull(attribute);
		assertEquals("integer", attribute.getType());
		assertEquals("game", attribute.getInherit());
		assertEquals(false, attribute.isIndexed());
		assertEquals(true, attribute.isNotNull());
		assertEquals(false, attribute.isShared());
		assertEquals(true, attribute.isUnique());
		assertEquals("", attribute.getDefault());

		attribute = table.getDBAttributeByName("event_code", false);
		assertNotNull(attribute);
		assertEquals("integer", attribute.getType());
		assertEquals("game", attribute.getInherit());
		assertEquals(false, attribute.isIndexed());
		assertEquals(true, attribute.isNotNull());
		assertEquals(false, attribute.isShared());
		assertEquals(true, attribute.isUnique());
		assertEquals("", attribute.getDefault());

		attribute = table.getDBAttributeByName("athlete_code", false);
		assertNotNull(attribute);
		assertEquals("integer", attribute.getType());
		assertEquals("game", attribute.getInherit());
		assertEquals(false, attribute.isIndexed());
		assertEquals(true, attribute.isNotNull());
		assertEquals(false, attribute.isShared());
		assertEquals(true, attribute.isUnique());
		assertEquals("", attribute.getDefault());

		attribute = table.getDBAttributeByName("stadium_code", false);
		assertNotNull(attribute);
		assertEquals("integer", attribute.getType());
		assertEquals("game", attribute.getInherit());
		assertEquals(false, attribute.isIndexed());
		assertEquals(true, attribute.isNotNull());
		assertEquals(false, attribute.isShared());
		assertEquals(false, attribute.isUnique());
		assertEquals("", attribute.getDefault());

		// test constraint
		// PRIMARY KEY
		Constraint constrait = table.getConstraintByName("pk_game_host_year_event_code_athlete_code");
		assertNotNull(constrait);
		assertEquals("PRIMARY KEY", constrait.getType());
		assertNotSame(-1, constrait.getAttributes().indexOf("host_year"));
		assertNotSame(-1, constrait.getAttributes().indexOf("event_code"));
		assertNotSame(-1, constrait.getAttributes().indexOf("athlete_code"));
		//FOREIGN KEY
		constrait = table.getConstraintByName("fk_game_event_code");
		assertNotNull(constrait);
		assertEquals("FOREIGN KEY", constrait.getType());
		assertNotSame(-1, constrait.getAttributes().indexOf("event_code"));
		assertNotSame(-1, constrait.getRules().indexOf("REFERENCES event"));
		assertNotSame(-1, constrait.getRules().indexOf("ON DELETE RESTRICT"));
		assertNotSame(-1, constrait.getRules().indexOf("ON UPDATE RESTRICT"));
		//NOT NULL
		constrait = table.getConstraintByName("n_game_athlete_code");
		assertNotNull(constrait);
		assertEquals("NOT NULL", constrait.getType());
		assertNotSame(-1, constrait.getAttributes().indexOf("athlete_code"));

	}

	public void testTogether() {
		String classname = "test_createdb";

		
		

		CreateClassTask createclasstask = new CreateClassTask(site);
		createclasstask.setDbName("demodb");
		createclasstask.setClassName(classname);
		createclasstask.execute();

		DBClasses dbclasses = createclasstask.getClassList();
		assertEquals("demodb", dbclasses.getDbname());

		AddAttributeTask addattributetask = new AddAttributeTask(site);
		addattributetask.setDbName("demodb");
		addattributetask.setClassName(classname);
		addattributetask.setAttributeName("t");
		addattributetask.setType("CHAR(2)");
		addattributetask.setDefault("");
		addattributetask.setCategory(AttributeCategory.INSTANCE);
		addattributetask.setUnique(true);
		addattributetask.setNotNull(true);
		addattributetask.execute();
		if (addattributetask.getErrorMsg() != null) {
			return;
		}

		SchemaInfo table = addattributetask.getSchemaInfo();
		DBAttribute attribute = table.getDBAttributeByName("t", false);
		assertNotNull(attribute);
		assertEquals("character(2)", attribute.getType());
		assertEquals(classname, attribute.getInherit());
		assertEquals(false, attribute.isIndexed());
		assertEquals(true, attribute.isNotNull());
		assertEquals(false, attribute.isShared());
		assertEquals(true, attribute.isUnique());
		assertEquals("", attribute.getDefault());

		DropClassTask dropclasstask = new DropClassTask(site);
		dropclasstask.setDbName("demodb");
		dropclasstask.setVirtualClassName(classname);
		dropclasstask.execute();	

		assertEquals(true, dropclasstask.isSuccess());

	}

}
