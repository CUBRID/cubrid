package com.cubrid.cubridmanager.core.cubrid.table.task.attribute;

import java.util.Arrays;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

public class UpdateAttributeTaskTest extends SetupEnvTestCase {
	public void testUpdateClassAttribute() {

		UpdateAttributeTask task = new UpdateAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("athlete");
		task.setOldAttributeName("classattribute");
		task.setNewAttributeName("classattribute2");
		task.setCategory(AttributeCategory.CLASS);
		task.setNotNull(false);
		task.setUnique(false);
		task.setDefault("'c'");
		task.execute();		
	}

	public void testIndexFieldNotSet() {
		UpdateAttributeTask task = new UpdateAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("athlete");
		task.setOldAttributeName("event");
		task.setNewAttributeName("eventnew");
		task.setCategory(AttributeCategory.INSTANCE);
		task.setNotNull(false);
		task.setUnique(false);
		task.setDefault("");
		task.execute();
		SchemaInfo table = task.getSchemaInfo();
		assertEquals("athlete", table.getClassname());
		task = new UpdateAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("athlete");
		task.setOldAttributeName("eventnew");
		task.setNewAttributeName("event");
		task.setCategory(AttributeCategory.INSTANCE);
		task.setNotNull(false);
		task.setUnique(false);
		task.setDefault("");
		task.execute();
		table = task.getSchemaInfo();
		assertEquals("athlete", table.getClassname());
	}

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/updateattribute_send");
		String msg = Tool.getFileContent(filepath);	
		

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		UpdateAttributeTask task = new UpdateAttributeTask(site);
		task.setDbName("demodb");
		task.setClassName("_db_attribute");
		task.setOldAttributeName("is_nullable");
		task.setNewAttributeName("s_nullable");
		task.setCategory(AttributeCategory.INSTANCE);
		task.setNotNull(false);
		task.setUnique(false);
		task.setDefault("");
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/updateattribute_receive_error");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);

		assertEquals("failure", node.getValue("status"));

	}

	public void testSetFieldValueTreeNode() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/updateattribute");
		String msg = Tool.getFileContent(filepath);	
		
		
		TreeNode node = MessageUtil.parseResponse(msg);

		TreeNode classinfo = node.getChildren().get(0);

		SchemaInfo table = ModelUtil.getSchemaInfo(classinfo);

		// test SchemaInfo object
		node = node.getChildren().get(0);
		assertEquals(node.getValue("classname"), table.getClassname());
		assertEquals(node.getValue("dbname"), table.getDbname());
		assertEquals(node.getValue("type"), table.getType());
		assertEquals(node.getValue("owner"), table.getOwner());
		assertEquals(node.getValue("virtual"), table.getVirtual());
		assertEquals(node.getValue("is_partitiongroup"), table.getIs_partitiongroup());
		assertEquals(node.getValue("partitiongroupname"), table.getPartitiongroupname());

		assertEquals(2, table.getAttributes().size());
		assertEquals(3, table.getConstraints().size());

		// test DBAttribute
		TreeNode node2 = node.getChildren().get(0);
		DBAttribute attribute = table.getAttributes().get(0);
		assertEquals(node2.getValue("name"), attribute.getName());
		assertEquals(node2.getValue("type"), attribute.getType());
		assertEquals(node2.getValue("inherit"), attribute.getInherit());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("indexed")), attribute.isIndexed());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("notnull")), attribute.isNotNull());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("shared")), attribute.isShared());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("unique")), attribute.isUnique());
		assertEquals(node2.getValue("default"), attribute.getDefault());

		// test Constraint
		node2 = node.getChildren().get(2);
		Constraint constraint = table.getConstraints().get(0);
		assertEquals(node2.getValue("name"), constraint.getName());
		assertEquals(node2.getValue("type"), constraint.getType());
		assertEquals(Arrays.toString(node2.getValues("attribute")), constraint.getAttributes().toString());
		assertEquals(Arrays.toString(node2.getValues("rule")), constraint.getRules().toString());
	}
}
