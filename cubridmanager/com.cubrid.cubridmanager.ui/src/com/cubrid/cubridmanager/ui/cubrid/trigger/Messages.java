/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.cubrid.trigger;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * To access and store messages for international requirement
 * 
 * @author moulinwang
 * @version 1.0 - 2009-5-11 created by moulinwang
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".cubrid.trigger.Messages", Messages.class);
	}
	public static String dropTriggerWarnMSG1;
	public static String dropTriggerWarnMSG2;
	public static String infoSQLScriptTab;
	public static String infoTriggerTab;
	public static String MSG_WARNING;
	public static String newTriggerShellTitle;
	public static String okBTN;
	public static String cancleBTN;
	public static String invalidTriggerNameError;
	public static String enterEventTargetMSG;
	public static String triggerInfo;
	public static String triggerGroupName;
	public static String triggerName;
	public static String conditionApply;
	public static String triggerEvent;
	public static String triggerEventTargetTable;
	public static String triggerEventTargetColumn;
	public static String triggerCondition;
	public static String triggerExecutionTime;
	public static String triggerContents;
	public static String triggerOptionalGroupName;
	public static String triggerSetStatus;
	public static String triggerStatus;
	public static String triggerSetPriority;
	public static String triggerPriority;

	public static String triggerToolTipConditionTime;
	public static String triggerToolTipPriority;
	public static String triggerToolTipStatus;
	public static String triggerToolTipActivity;
	public static String triggerToolTipActivityType;
	public static String triggerToolTipDelayedTime;
	public static String triggerToolTipDatabaseEventType;
	public static String triggerToolTipName;
	public static String triggerToolTipEventTarget;
	public static String triggerToolTipCondition;
	public static String MSG_INFORMATION;
	public static String newTriggerSuccess;
	public static String newTriggerMSGTitle;

	public static String conditionTimeBefore;
	public static String conditionTimeAfter;
	public static String conditionTimeDeferred;

	public static String eventTypeInsert;
	public static String eventTypeSInsert;
	public static String eventTypeUpdate;
	public static String eventTypeSUpdate;
	public static String eventTypeDelete;
	public static String eventTypeSDelete;
	public static String eventTypeCommit;
	public static String eventTypeRollback;

	public static String actionTimeDefault;
	public static String actionTimeAfter;
	public static String actionTimeDeferred;

	public static String actionTypeReject;
	public static String actionTypeInvalidateTransaction;
	public static String actionTypePrint;
	public static String actionTypeOtherSQL;

	public static String triggerStatusActive;
	public static String triggerStatusInactive;

	public static String triggerActionGroupText;
	public static String triggerStatusGroupText;
	public static String triggerPriorityGroupText;
	public static String triggerPriorityText;
	public static String sqlStatementMSG;
	public static String triggerAlterMSG;
	public static String alterTriggerShellTitle;
	public static String triggerAlterMSGTitle;
	public static String alterTriggerSuccess;
	public static String errFormatPriority;
	public static String errRangePriority;
	public static String errPriorityFormat;

}
