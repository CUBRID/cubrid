/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.ui.logs;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * This is message bundle classes and provide convenience methods for
 * manipulating messages.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 * 
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".logs.Messages", Messages.class);
	}
	// title
	public static String title_casRunnerResult;
	public static String title_sqlLogAnalyzeResultDialog;
	public static String msg_sqlLogAnalyzeResultDialog;
	public static String title_sqlLogFileListDialog;
	public static String msg_sqlLogFileListDialog;
	public static String title_casRunnerConfigDialog;
	public static String msg_casRunnerConfigDialog;
	public static String title_casRunnerResultDialog;
	public static String msg_casRunnerResultDialog;
	public static String title_logPropertyDialog;
	public static String title_TimeSetDialog;

	// message related
	public static String errYear;
	public static String errMonth;
	public static String errDay;
	public static String errHour;
	public static String errMinute;
	public static String errSecond;
	public static String warning_removeLog;
	public static String warning_removeManagerLog;
	public static String warning_resetAdminLog;

	// other
	public static String msg_success;
	public static String msg_overwriteFile;
	public static String msg_fileSize;
	public static String msg_nullLogFile;
	public static String msg_deleteAllLog;
	public static String msg_selectTargeFile;
	public static String msg_validInputDbName;
	public static String msg_validInputBrokerName;
	public static String msg_validInputUserID;
	public static String msg_validInputPassword;

	// label
	public static String label_targetFileList;
	public static String label_logFile;
	public static String label_analysisResult;
	public static String label_casLogFile;
	public static String label_logContents;
	public static String label_executeResult;
	public static String label_brokerName;
	public static String label_userId;
	public static String label_password;
	public static String label_numThread;
	public static String label_numRepeatCount;
	public static String label_viewCasRunnerQueryResult;
	public static String label_viewCasRunnerQueryPlan;
	public static String label_database;

	// button
	public static String button_ok;
	public static String button_cancel;
	public static String button_close;
	public static String button_saveLogString;
	public static String button_executeOriginalQuery;
	public static String button_beforeResultFile;
	public static String button_nextResultFile;
	public static String button_deleteAll;

	// check
	public static String chk_analizeOption_t;

	// table
	public static String table_index;
	public static String table_max;
	public static String table_min;
	public static String table_avg;
	public static String table_totalCount;
	public static String table_errCount;
	public static String table_transactionExeTime;
	public static String table_property;
	public static String table_value;
	public static String table_logType;
	public static String table_fileName;
	public static String table_fileOwner;
	public static String table_fileSize;
	public static String table_changeDate;
	public static String table_filePath;
	public static String table_user;
	public static String table_taskName;
	public static String table_time;
	public static String table_description;
	public static String table_number;
	public static String table_casId;
	public static String table_ip;
	public static String table_startTime;
	public static String table_endTime;
	public static String table_elapsedTime;
	public static String table_processId;
	public static String table_errorInfo;
	public static String table_errorType;
	public static String table_errorCode;
	public static String table_tranId;
	public static String table_errorId;
	public static String table_errorMsg;
	public static String table_status;
	public static String table_content;
	// context
	public static String context_copy;

	public static String viewLogJobName;
}