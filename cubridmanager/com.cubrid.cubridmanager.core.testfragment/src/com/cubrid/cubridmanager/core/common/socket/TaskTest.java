package com.cubrid.cubridmanager.core.common.socket;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;

public class TaskTest extends SetupEnvTestCase {

	// public void testSetMSG() {
	// fail("Not yet implemented");
	// }
	//
	// public void testSendMSG() {
	// fail("Not yet implemented");
	// }

	public void testSetFieldValue() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/updateattribute");
		String msg = Tool.getFileContent(filepath);
		
	
		TreeNode node = MessageUtil.parseResponse(msg, true);
		TreeNode node2 = node.getChildren().get(0).getChildren().get(0);
		DBAttribute attribute = new DBAttribute();
	
		SocketTask.setFieldValue(node2, attribute);
		assertEquals(node2.getValue("name"), attribute.getName());
		assertEquals(node2.getValue("type"), attribute.getType());
		assertEquals(node2.getValue("inherit"), attribute.getInherit());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("indexed")), attribute.isIndexed());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("notnull")), attribute.isNotNull());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("shared")), attribute.isShared());
		assertEquals(CommonTool.strYN2Boolean(node2.getValue("unique")), attribute.isUnique());
		assertEquals(node2.getValue("default"), attribute.getDefault());
	}

}
