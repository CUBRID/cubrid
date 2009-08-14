package com.cubrid.cubridmanager.core.cubrid.table.task.constraint;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class AddPKConstraintTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_send_PK");
		String msg = Tool.getFileContent(filepath);	
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddPKConstraintTask task = new AddPKConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setName("pk_game_host_year_event_code_athlete_code");
		String[] attributes =
			{
			        "host_year",
			        "event_code",
			        "athlete_code",
			};
		task.setAttributes(attributes);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_receive_PK");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test PK constraint
		Constraint pk = classinfo.getConstraintByName("pk_game_host_year_event_code_athlete_code");
		assertNotNull(pk);
		assertEquals("PRIMARY KEY", pk.getType());
		List<String> attributes = pk.getAttributes();
		assertEquals("host_year", attributes.get(0));
		assertEquals("event_code", attributes.get(1));
		assertEquals("athlete_code", attributes.get(2));
	}
}