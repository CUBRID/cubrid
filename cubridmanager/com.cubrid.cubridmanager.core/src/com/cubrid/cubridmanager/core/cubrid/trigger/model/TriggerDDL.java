package com.cubrid.cubridmanager.core.cubrid.trigger.model;

import java.math.BigDecimal;

import com.cubrid.cubridmanager.core.CommonTool;

public class TriggerDDL {

	static String newLine = CommonTool.newLine;
	static String endLineChar = ";";

	public static String getDDL(Trigger trigger) {
		StringBuffer bf = new StringBuffer();
		//CREATE TRIGGER trigger_name
		bf.append("CREATE TRIGGER ");
		String triggerName = trigger.getName();
		if (triggerName == null || triggerName.trim().equals("")) {
			bf.append("<trigger_name>");
		} else {
			bf.append("\"").append(triggerName).append("\"");
		}
		bf.append(newLine);
		//[ STATUS { ACTIVE | INACTIVE } ]
		String status = trigger.getStatus();
		if (status.equals("ACTIVE")) {
			//do nothing
		} else {
			bf.append("STATUS INACTIVE");
			bf.append(newLine);
		}
		//[ PRIORITY key ]
		String priority = trigger.getPriority();
		try {
			BigDecimal p = new BigDecimal(priority);
			if (p.equals(new BigDecimal("0.00"))) {
				//do nothing
			} else {
				bf.append("PRIORITY ").append(priority);
				bf.append(newLine);
			}
		} catch (NumberFormatException e) {
			bf.append("PRIORITY ").append(priority);
			bf.append(newLine);
		}

		//event_time event_type [ event_target ]
		StringBuffer ifBF = new StringBuffer();
		StringBuffer execBF = new StringBuffer();

		String conditionTime = trigger.getConditionTime();
		String eventType = trigger.getEventType();
		String targetTable = trigger.getTarget_class();
		String targetColumn = trigger.getTarget_att();

		String condition = trigger.getCondition();
		String actionTime = trigger.getActionTime();
		//EXECUTE [ AFTER | DEFERRED ] action [ ; ]
		execBF.append("EXECUTE ");
		if (condition == null || condition.trim().equals("")) {
			if (actionTime.equals("AFTER") || actionTime.equals("DEFERRED")) {
				bf.append(actionTime);
			} else {
				bf.append("BEFORE");
			}

		} else {
			bf.append(conditionTime);
			//[ IF condition ]
			ifBF.append("IF ").append(condition);
			ifBF.append(newLine);
			execBF.append(actionTime);
		}
		bf.append(" ").append(eventType).append(" ");
		if (targetTable == null || targetTable.trim().equals("")) {
			if (eventType.equals("COMMIT") || eventType.equals("ROLLBACK")) {
				// append nothing
			} else {
				bf.append("<event_target>");
			}
			bf.append(newLine);
		} else {
			bf.append("ON \"").append(targetTable).append("\"");
			if (targetColumn != null && !targetColumn.equals("")) {
				bf.append("(\"").append(targetColumn).append("\")");
			}
			bf.append(newLine);
		}
		bf.append(ifBF.toString());
		bf.append(execBF.toString());

		String actionType = trigger.getActionType();
		String action = trigger.getAction();
		if (actionType.equals("REJECT")
				|| actionType.equals("INVALIDATE TRANSACTION")) {
			bf.append(actionType);
		} else if (actionType.equals("PRINT")) {
			bf.append(actionType);
			bf.append(newLine);
			bf.append("'").append(action.replace("'", "''")).append("'");
		} else {
			bf.append(newLine);
			bf.append(action);
		}
		bf.append(endLineChar);
		return bf.toString();
	}

	public static String getAlterDDL(Trigger oldTrigger, Trigger newTrigger) {
		String triggerName = oldTrigger.getName();
		String oldPriority = oldTrigger.getPriority();
		String oldStatus = oldTrigger.getStatus();
		String newPriority = newTrigger.getPriority();
		String newStatus = newTrigger.getStatus();
		StringBuffer bf = new StringBuffer();
		boolean statusChanged = false;
		if (!oldStatus.equals(newStatus)) {
			statusChanged = true;
		}
		if (statusChanged) {
			bf.append("ALTER TRIGGER ");
			bf.append("\"").append(triggerName).append("\"");			
			bf.append(" STATUS ").append(newStatus);
			bf.append(endLineChar);
			bf.append(newLine);
		}

		boolean priorityChanged = false;
		if (!oldPriority.equals(newPriority)) {
			priorityChanged = true;
		}
		if (priorityChanged) {
			bf.append("ALTER TRIGGER ");
			bf.append("\"").append(triggerName).append("\"");			
			bf.append(" PRIORITY ").append(newPriority);
			bf.append(endLineChar);
			bf.append(newLine);
		}
		if (statusChanged || priorityChanged) {
			return bf.toString();
		} else {
			return "";
		}
	}
}
