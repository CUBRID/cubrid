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

public class AddFKConstraintTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_send_FK");
		String msg = Tool.getFileContent(filepath);	
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddFKConstraintTask task = new AddFKConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");
		task.setReferenceClassName("athlete");
		String[] foreignKeys =
			{
			        "athlete_code",			        
			};
		String[] referenceKeys =
			{
			        "code",			        
			};
		task.setName("fk_game_athlete_code");
		task.setForeignAndRefKeys(foreignKeys, referenceKeys);
		task.setCategory(AttributeCategory.INSTANCE);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_receive_FK");
		String msg = Tool.getFileContent(filepath);	
		
		
		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo = ModelUtil.getSchemaInfo(node.getChildren().get(0));
		// test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		// test FK constraint
		Constraint fk = classinfo.getConstraintByName("fk_game_athlete_code");
		assertNotNull(fk);
		assertEquals("FOREIGN KEY", fk.getType());
		List<String> attributes = fk.getAttributes();
		assertEquals("athlete_code", attributes.get(0));		
		List<String> rules = fk.getRules();
		assertEquals("REFERENCES athlete", rules.get(0));
		assertEquals("ON DELETE RESTRICT", rules.get(1));
		assertEquals("ON UPDATE RESTRICT", rules.get(2));
	}
}
