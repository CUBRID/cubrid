package com.cubrid.cubridmanager.core.cubrid.table.task.virtualclass;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassItem;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

public class DropVirtualClassTest  extends SetupEnvTestCase {
	String dbname="demodb";
	String vtable = "v_code";
	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/dropvirtualclass_send");
		String msg = Tool.getFileContent(filepath);
		
		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		DropVirtualClass task = new DropVirtualClass(site);
		task.setDbName(dbname);
		task.setClassName(vtable);
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/virtualclass/test.message/dropvirtualclass_receive");
		String msg = Tool.getFileContent(filepath);
		
	
		TreeNode node = MessageUtil.parseResponse(msg);
		DBClasses classinfo = ModelUtil.getClassList(node);
		// test basic class information
		assertEquals(dbname, classinfo.getDbname());
		List<ClassItem> list=classinfo.getUserClassList().getClassList();
		boolean found=false;
		for(ClassItem item:list){
			if(item.getClassname().equals(vtable)){
				found=true;
			}
		}
		assertEquals(false,found);		
	}
}