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

public class AddIndexConstraintTaskTest extends SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_send_index");
		String msg = Tool.getFileContent(filepath);	
		

		//replace "token" field with the latest value
		msg=msg.replaceFirst("token:.*\n", "token:"+this.token+"\n");		
		//composite message
		AddIndexConstraintTask task=new AddIndexConstraintTask(site);
		task.setDbName("demodb");
		task.setClassName("game");	
		task.setName("test_codes_index");
		String[] attributes={
			"event_code","athlete_code","stadium_code",	"nation_code"
		};
		String[] attribute_orders={
			"asc","asc","asc","asc"	
		};
		task.setAttributesAndOrders(attributes, attribute_orders);
		task.setCategory(AttributeCategory.INSTANCE);
		//compare 
		assertEquals(msg, task.getRequest());
	}
	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/constraint/test.message/addconstraint_receive_index");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);
		SchemaInfo classinfo=ModelUtil.getSchemaInfo(node.getChildren().get(0));
		//test basic class information
		assertEquals("demodb", classinfo.getDbname());
		assertEquals("game", classinfo.getClassname());
		assertEquals("user", classinfo.getType());
		assertEquals("DBA", classinfo.getOwner());
		assertEquals("normal", classinfo.getVirtual());
		//test index constraint
		Constraint index=classinfo.getConstraintByName("test_codes_index");
		assertNotNull(index);
		assertEquals("INDEX", index.getType());
		List<String> attributes = index.getAttributes();
		assertEquals("event_code",attributes.get(0));
		assertEquals("athlete_code",attributes.get(1));
		assertEquals("stadium_code",attributes.get(2));
		assertEquals("nation_code",attributes.get(3));
		List<String> rules = index.getRules();
		assertEquals("event_code ASC",rules.get(0));
		assertEquals("athlete_code ASC",rules.get(1));
		assertEquals("stadium_code ASC",rules.get(2));
		assertEquals("nation_code ASC",rules.get(3));
	}
}