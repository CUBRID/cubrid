package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ClassType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerAction;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerEvent;

import junit.framework.TestCase;

public class ModelUtilTest extends
		TestCase {
	public void testAttributeCategory(){
		AttributeCategory a=AttributeCategory.eval("instance");
		assert(a.equals(AttributeCategory.INSTANCE));
	}
	public void testClassType(){
		ClassType a=ClassType.eval("normal");
		assert(a.equals(ClassType.NORMAL));
	}
	public void testConstraintType(){
		ConstraintType a=ConstraintType.eval("FOREIGN KEY");
		assert(a.equals(ConstraintType.FOREIGNKEY));
		a=ConstraintType.eval("PRIMARY KEY");
		assert(a.equals(ConstraintType.PRIMARYKEY));
	}
	
	public void testTriggerAction(){
		TriggerAction triggeraction=TriggerAction.eval("INVALIDATE TRANSACTION");
		assert(triggeraction.equals(TriggerAction.INVALIDATE_TRANSACTION));
		
		triggeraction=TriggerAction.eval("OTHER STATEMENT");
		assert(triggeraction.equals(TriggerAction.OTHER_STATEMENT));	
	}
	public void testTriggerEvent(){
		TriggerEvent a=TriggerEvent.eval("STATEMENT INSERT");
		assert(a.equals(TriggerEvent.STATEMENTINSERT));
	}
}
