/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search
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
package com.cubrid.cubridmanager.ui.cubrid.jobauto;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * Message bundle classes. Provides convenient methods for manipulating
 * messages.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-2 created by lizhiqiang
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".cubrid.jobauto.Messages", Messages.class);
	}
	//backup plan
	public static String addBackupPlanMsg;
	public static String addBackupPlanTitle;
	public static String editBackupPlanMsg;
	public static String editBackupPlanTitle;
	public static String msgIdLbl;
	public static String msgLevelLbl;
	public static String msgPathLbl;
	public static String msgPathBrowseBtn;
	public static String msgPeriodGroup;
	public static String msgPeriodTypeLbl;
	public static String msgPeriodDetailLbl;
	public static String msgPeriodHourLbl;
	public static String msgPeriodMinuteLbl;
	public static String msgStroreBtn;
	public static String msgDeleteBtn;
	public static String msgCheckingBtn;
	public static String msgUpdateBtn;
	public static String msgUseCompressBtn;
	public static String msgNumThreadLbl;
	public static String msgComboGroup;
	public static String msgOnlineBtn;
	public static String msgOfflineBtn;
	public static String tipPeriodDetailCombo;
	public static String hourToolTip;
	public static String minuteToolTip;
	public static String backplanIdMaxLen;
	public static String optionGroupName;
	public static String basicGroupName;
	public static String btnBrowse;
	public static String msgSelectDir;
	public static String zeroLever;
	public static String oneLever;
	public static String twoLever;

	public static String errIdTextMsg;
	public static String errIdRepeatMsg;
	public static String errPathTextMsg;
	public static String errDetailTextMsg;
	public static String errBackplanIdLen;
	public static String errBackupPlanIdEmpty;
	//query plan
	public static String addQryPlanMsg;
	public static String addQryPlanTitle;
	public static String editQryPlanMsg;
	public static String editQryPlanTitle;
	public static String msgQryBasicGroupName;
	public static String msgQryIdLbl;
	public static String msgQryPeriodGroup;
	public static String msgQryPeriodTypeLbl;
	public static String msgQryPeriodDetailLbl;
	public static String msgQryPeriodHourLbl;
	public static String msgQryPeriodMinuteLbl;
	public static String msgQryStateLbl;
	public static String queryplanIdMaxLen;

	public static String errQueryplanIdLen;
	public static String errQueryPlanIdRepeatMsg;
	public static String errQueryplanIdEmpty;
	public static String errQueryplanStmtEmpty;

	//backup error log
	public static String backupLogErrorDesc;
	public static String backupLogErrorTime;
	public static String backupLogBackid;
	public static String backupLogDb;
	public static String backupShellTitle;
	public static String backupLogMessage;
	public static String backupLogTitle;
	public static String backupLogLockGroupName;
	//query log
	public static String queryLogTitle;
	public static String queryLogMessage;
	public static String queryLogDesc;
	public static String queryLogErrorCode;
	public static String queryId;
	public static String queryLogTime;
	public static String queryLogCmUser;
	public static String queryLogDb;
	public static String queryLogLockGroupName;

	//common
	public static String btnOK;
	public static String btnCancel;
	public static String btnRefresh;
	public static String msgSelectDB;

	//DeleteBackupPlanAction
	public static String delBackupPlanConfirmContent;
	//DeleteQueryPlanAction
	public static String delQueryPlanConfirmContent;
	
	//PeriodGroup
	public static String monthlyPeriodType;
	public static String weeklyPeriodType;
	public static String dailyPeriodType;
	public static String specialdayPeriodType;
	
	public static String sundayOfWeek;
	public static String mondayOfWeek;
	public static String tuesdayOfWeek;
	public static String wednesdayOfWeek;
	public static String thursdayOfWeek;
	public static String fridayOfWeek;
	public static String saturdayOfWeek;
}