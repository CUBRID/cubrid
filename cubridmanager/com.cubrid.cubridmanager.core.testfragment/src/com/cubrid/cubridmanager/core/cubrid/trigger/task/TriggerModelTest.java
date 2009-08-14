package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;

public class TriggerModelTest extends SetupEnvTestCase {
	public void testModelTrigger() {
		Trigger bean = new Trigger();
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.setConditionTime("conditionTime");
		assertEquals(bean.getConditionTime(), "conditionTime");
		bean.setEventType("eventType");
		assertEquals(bean.getEventType(), "eventType");
		bean.setTarget_class("target_class");
		assertEquals(bean.getTarget_class(), "target_class");
		bean.setCondition("condition");
		assertEquals(bean.getCondition(), "condition");
		bean.setActionTime("actionTime");
		assertEquals(bean.getActionTime(), "actionTime");
		bean.setActionType("actionType");
		assertEquals(bean.getActionType(), "actionType");
		bean.setAction("action");
		assertEquals(bean.getAction(), "action");
		bean.setStatus("status");
		assertEquals(bean.getStatus(), "status");
		bean.setPriority("priority");
		bean.setTarget_att("setTarget_att");
		assertEquals(bean.getTarget_att(), "setTarget_att");
	}
}