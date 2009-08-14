package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerAction;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerActionTime;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerConditionTime;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerEvent;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerStatus;

public class AddTriggerTaskTest extends SetupEnvTestCase {
	String dbname = "demodb";
	String triggerName = "test";
	String queryspec = "select code as aaa from code";
	TriggerConditionTime conditiontime = TriggerConditionTime.BEFORE;
	TriggerEvent eventType = TriggerEvent.INSERT;
	TriggerAction action = TriggerAction.INVALIDATE_TRANSACTION;
	String eventTarget = "athlete";
	TriggerActionTime actionTime = TriggerActionTime.AFTER;
	TriggerStatus status = TriggerStatus.ACTIVE;
	String priority = "0.0";

	public void testSend() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/addtrigger_send");
		String msg = Tool.getFileContent(filepath);

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		AddTriggerTask task = new AddTriggerTask(site);
		task.setDbName(dbname);
		task.setTriggerName(triggerName);
		task.setConditionTime(conditiontime);
		task.setEventType(eventType);
		task.setAction(action, null);
		task.setEventTarget(eventTarget);
		task.setActionTime(actionTime);
		task.setStatus(status);
		task.setPriority(priority);
		// compare
		assertEquals(msg, task.getRequest());
		
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/addtrigger_send2");
		msg = Tool.getFileContent(filepath);

		// replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		// composite message
		task = new AddTriggerTask(site);
		task.setDbName(dbname);
		task.setTriggerName("update_monitor");
		task.setConditionTime(conditiontime);
		task.setEventType(TriggerEvent.UPDATE);
		task.setAction(TriggerAction.PRINT, "test");
		task.setEventTarget(eventTarget);
		task.setActionTime(actionTime);
		task.setStatus(status);
		task.setPriority("0.02");
		// compare
		assertEquals(msg, task.getRequest());
	}

	public void testReceive() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/addtrigger_receive");
		String msg = Tool.getFileContent(filepath);	
		TreeNode node = MessageUtil.parseResponse(msg);

		String status = node.getValue("status");
		String errorMsg = null;
		boolean success;
		if (status.equals("success")) {
			success = true;
		} else {
			errorMsg = node.getValue("note");
			success = false;
		}
		assertEquals(true, success);
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/trigger/task/test.message/addtrigger_receive_error");
		msg = Tool.getFileContent(filepath);


		node = MessageUtil.parseResponse(msg);

		status = node.getValue("status");
		errorMsg = null;
		if (status.equals("success")) {
			success = true;
		} else {
			errorMsg = node.getValue("note");
			success = false;
		}
		assertEquals(false, success);
		assertEquals(" invalid use of keyword 'test', expecting { name }.", errorMsg);

	}
}