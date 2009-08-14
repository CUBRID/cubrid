package com.cubrid.cubridmanager.core.cubrid.table.task.constraint;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

public class DropConstraintTaskTest extends SetupEnvTestCase {
	public void testIndexSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_send_index");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropConstraintTask task = new DropConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("_db_attribute");
		task.setType(ConstraintType.INDEX);
		task.setName("i__db_attribute_class_of_attr_name");
		String[] attributes =
			{
			        "class_of",
			        "attr_name",
			};
		task.setAttributes(attributes);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testIndexReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_receive_index_error");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);
		assertEquals("failure", node.getValue("status"));
	}
	public void testUniqueSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_send_unique");
		String msg = Tool.getFileContent(filepath);	
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropConstraintTask task = new DropConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setType(ConstraintType.UNIQUE);
		task.setName("u_game_t");
		String[] attributes =
			{
			        "t",			      
			};
		task.setAttributes(attributes);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testUniqueReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_receive_unique");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test unique constraint
		Constraint unique = classinfo.getConstraintByName("u_game_t");
		assertNull(unique);
		
	}
	public void testFKSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_send_FK");
		String msg = Tool.getFileContent(filepath);	
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropConstraintTask task = new DropConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setType(ConstraintType.FOREIGNKEY);
		task.setName("fk_game_athlete_code");
		String[] attributes =
			{
			        "athlete_code",
			};
		task.setAttributes(attributes);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testFKReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_receive_FK");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test fk constraint
		Constraint fk = classinfo.getConstraintByName("fk_game_athlete_code");
		assertNull(fk);
	}
	public void testPKSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_send_PK");
		String msg = Tool.getFileContent(filepath);	
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropConstraintTask task = new DropConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setType(ConstraintType.PRIMARYKEY);
		task.setName("pk_game_host_year_event_code_athlete_code");
		String[] attributes =
			{
			        "host_year",
			        "event_code",
			        "athlete_code"
			};
		task.setAttributes(attributes);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testPKReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/dropconstraint_receive_PK");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test pk constraint
		Constraint pk = classinfo.getConstraintByName("pk_game_host_year_event_code_athlete_code");
		assertNull(pk);
	}
}