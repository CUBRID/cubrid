package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;

public class GetTriggerInfoTaskTest extends SetupEnvTestCase {
	String dbname = "demodb";
	

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/gettriggerinfo_send");
		String msg = Tool.getFileContent(filepath);	

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		GetTriggerListTask task = new GetTriggerListTask(site);
		task.setDbName(dbname);		
		// compare
		assertEquals(msg, task.getRequest());

	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/gettriggerinfo_receive");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg);

		List<Trigger> list=ModelUtil.getTriggerList(node.getChildren().get(0));
		String action="update resort set number_of_pools=new.number_of_pools-1 where \"name\"=obj.\"name\"";
		assertEquals(2, list.size());
		assertEquals("limit_pools", list.get(0).getName());
		assertEquals("BEFORE", list.get(0).getConditionTime());
		assertEquals("UPDATE", list.get(0).getEventType());
		assertEquals(action, list.get(0).getAction());
		assertEquals("resort", list.get(0).getTarget_class());
		assertEquals("number_of_pools", list.get(0).getTarget_att());
		assertEquals("new.number_of_pools>0", list.get(0).getCondition());
		assertEquals("BEFORE", list.get(0).getActionTime());
		assertEquals("ACTIVE", list.get(0).getStatus());
		assertEquals("0.01000", list.get(0).getPriority());		
	}
}