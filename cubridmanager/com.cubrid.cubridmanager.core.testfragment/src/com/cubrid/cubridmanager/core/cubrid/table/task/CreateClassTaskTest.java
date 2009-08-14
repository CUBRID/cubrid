package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassItem;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;

public class CreateClassTaskTest extends
		SetupEnvTestCase {
	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/createclass_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		CreateClassTask task = new CreateClassTask(site);
		task.setDbName("demodb");
		task.setClassName("sub_athelete");
		//compare 
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/test.message/createclass_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));
		assertEquals("demodb", node.getValue("dbname"));
		assertEquals("sub_athelete", node.getValue("classname"));

		TreeNode subnode = node.getChildren().get(0);
		assertEquals("systemclass", subnode.getValue("open"));

		TreeNode subnode2 = node.getChildren().get(1);
		assertEquals("userclass", subnode2.getValue("open"));

		TreeNode ssubnode = subnode.getChildren().get(0);
		assertEquals("class", ssubnode.getValue("open"));
		assertEquals("normal", ssubnode.getValue("virtual"));
		assertEquals("DBA", ssubnode.getValue("owner"));

		DBClasses list = ModelUtil.getClassList(node);
		//test database name
		assertEquals("demodb", list.getDbname());
		//test class number correct
		List<ClassItem> classList = list.getSystemClassList().getClassList();
		List<ClassItem> classList2 = list.getUserClassList().getClassList();
		int size = classList.size();
		assertNotNull(classList);
		assertNotNull(classList2);
		assertEquals(41, classList.size());
		assertEquals(13, classList2.size());
		//test the first system class
		ClassItem sysclass = classList.get(0);
		assertEquals("db_root", sysclass.getClassname());
		assertEquals("DBA", sysclass.getOwner());
		assertEquals("normal", sysclass.getVirtual());
		assertEquals(false, sysclass.isVirtual());
		List<String> superclassList = sysclass.getSuperclassList();
		assertEquals(0, superclassList.size());

		list.getSystemClassList().removeClass(sysclass);
		assertEquals(size - 1, classList.size());
		list.getSystemClassList().removeAllClass();
		assertEquals(0, classList.size());
		//test the first user class
		ClassItem userclass = classList2.get(0);
		assertEquals("stadium", userclass.getClassname());
		assertEquals("PUBLIC", userclass.getOwner());
		assertEquals("normal", userclass.getVirtual());

	}
}
