package com.cubrid.cubridmanager.core.cubrid.table.task.superclass;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClass;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class GetSuperClassesInfoTaskTest extends SetupEnvTestCase {
	String dbname="demodb";
	String table = "sub_athelete";

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/getsuperclassesinfo_send");
		String msg = Tool.getFileContent(filepath);
		
	
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		GetSuperClassesInfoTask task = new GetSuperClassesInfoTask(site);
		task.setDbName(dbname);
		task.setClassName(table);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/superclass/test.message/getsuperclassesinfo_receive");
		String msg = Tool.getFileContent(filepath);
		

		TreeNode superclassinfo = MessageUtil.parseResponse(msg);
		List<TreeNode> classes = superclassinfo.getChildren().get(0).getChildren();	
		
		List<SuperClass> list=new ArrayList<SuperClass>();
		for(TreeNode node:classes){
			SuperClass superclass=ModelUtil.getSuperClass(node);
			list.add(superclass);					
		}
		// test basic class information
		assertNotNull(list);
		SuperClass c=list.get(0);
		assertEquals("_db_auth", c.getName());
		assertEquals(5, c.getAttributes().size());
		assertEquals(2, c.getClassMethods().size());
		
		SuperClass c2=list.get(1);
		assertEquals("_db_index", c2.getName());
		assertEquals(8, c2.getAttributes().size());
		assertEquals(3, c2.getClassAttributes().size());


		
	}
}
