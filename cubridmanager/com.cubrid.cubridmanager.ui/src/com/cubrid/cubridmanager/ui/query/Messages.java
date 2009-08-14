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
package com.cubrid.cubridmanager.ui.query;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * This is message bundle classes and provide convenience methods for
 * manipulating messages.
 * 
 * @author pangqiren 2009-3-2
 * 
 */
public class Messages extends
		NLS {

	public static String open;
	public static String save;
	public static String saveAs;
	public static String commit;
	public static String rollback;
	public static String autoCommit;
	public static String title;
	public static String clear;
	public static String error;
	public static String choose;
	public static String change;
	public static String close;
	public static String cancel;
	public static String run;
	public static String btnYes;
	public static String btnNo;

	public static String update;
	public static String updateOk1;
	public static String deleteOk;
	public static String alterOk;
	public static String createOk;
	public static String dropOk;
	public static String updateOk2;
	public static String insertOk;
	public static String queryOk;
	public static String errWhere;
	public static String queryPlanTip;
	public static String proRunQuery;
	public static String undoTip;
	public static String redoTip;
	public static String stopBtn;
	public static String findNextTip;

	public static String schemaInfoViewTitle; // SchemaInfoView for query
	// editor and/or query explain

	public static String unCommentTip;
	public static String unIndentTip;
	public static String indentTip;
	public static String commentTip;
	public static String info;
	public static String transActive;
	public static String cantChangeStatus;
	public static String formatTip;

	public static String findTip;
	public static String findWhat;
	public static String replaceWith;
	public static String option;
	public static String matchCase;
	public static String wrapSearch;
	public static String matchWholeWord;
	public static String direction;
	public static String up;
	public static String down;
	public static String findBtn;
	public static String replaceBtn;
	public static String replaceAllBtn;

	public static String noDbSelected;
	public static String plsSelectDb;

	public static String tooManyRecord;
	public static String warning;
	public static String querySeq;
	public static String second;
	public static String totalRows;
	public static String delete;
	public static String confirmDelMsg;
	public static String errorHead;
	public static String copyClipBoard;
	public static String oidNavigator;
	public static String detailView;
	public static String allExport;
	public static String selectExport;
	public static String runError;
	public static String showOneTimeTip;

	public static String autoCommitLabel;
	public static String searchUnitInstances;
	public static String pageUnitInstances;
	public static String enableQueryPlan;
	public static String getOid;
	public static String useDefDriver;
	public static String jdbcDriver;
	public static String charSet;
	public static String changeFont;
	public static String font;
	public static String restoreDefault;
	public static String size;
	public static String brokerIP;
	public static String brokerPort;
	public static String queryEditorTitle;
	public static String queryTitle;

	public static String fileSave;
	public static String overWrite;
	public static String columnCountOver;
	public static String columnCountLimit;
	public static String export;

	public static String noContext;

	public static String connCloseConfirm;
	public static String changeDbConfirm;
	public static String beSure;

	public static String notSaveNull;

	public static String column;
	public static String value;
	public static String updateBtn;
	public static String closeBtn;

	public static String cfmUpdateChangedValue;
	public static String titleRowDetailDialog;
	public static String msgValueNoChanged;
	public static String msgValueNoChangedTitle;
	public static String errMsgServerNull;
	public static String msgQueryResultNullFlag;
	public static String exportLimit;
	public static String sheetCreated;
	public static String exportOk;

	public static String lblColumnName;
	public static String lblColumnValue;
	public static String saveResource;
	public static String saveConfirm;

	public static String TOOLTIP_QEDIT_EXPLAIN_NEW;
	public static String TOOLTIP_QEDIT_EXPLAIN_OPEN;
	public static String TOOLTIP_QEDIT_EXPLAIN_SAVE;
	public static String TOOLTIP_QEDIT_EXPLAIN_SAVEAS;
	public static String TOOLTIP_QEDIT_EXPLAIN_HISTORY_SWITCH;
	public static String TOOLTIP_QEDIT_EXPLAIN_HISTORY_SHOW_HIDE;
	public static String TOOLTIP_QEDIT_EXPLAIN_DISPLAY_MODE;

	public static String QEDIT_FIRSTPAGE;
	public static String QEDIT_LASTPAGE;
	public static String QEDIT_PREVIOUSPAGE;
	public static String QEDIT_NEXTPAGE;

	public static String commitUpdate;
	public static String WAITING_EXPORT;
	// public static String MSG_TOTALROWS;

	public static String TOOLTIP_QUERYPLANENABLE;
	public static String QEDIT_RESULT;
	public static String QEDIT_LOGSRESULT;
	public static String TOOLTIP_QEDIT_FIND;
	public static String TOOLTIP_QEDIT_REPLACE;
	public static String QEDIT_FIND;
	public static String QEDIT_NOTFOUND;
	public static String QEDIT_REPLACE;
	public static String QEDIT_REPLACEALL;
	public static String QEDIT_REPLACECOMPLETE;

	public static String QEDIT_ROW_DETAIL;

	public static String QEDIT_RESULT_FOLDER;
	public static String QEDIT_PLAN_FOLDER;
	public static String QEDIT_PLAN;
	public static String QEDIT_PLAN_CURFILE_TITLE;
	public static String QEDIT_PLAN_HISTORY_COL1;
	public static String QEDIT_PLAN_HISTORY_COL2;
	public static String QEDIT_PLAN_HISTORY_COL3;
	public static String QEDIT_PLAN_HISTORY_COL4;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL1;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL2;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL3;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL3_DTL;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL4;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL4_DTL;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL5;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL5_DTL;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL6;
	public static String QEDIT_PLAN_TREE_SIMPLE_COL7;
	public static String QEDIT_PLAN_FILENAME_ERROR;
	public static String QEDIT_PLAN_OPEN_FILE_ERROR;
	public static String QEDIT_PLAN_SAVE_FILE_ERROR;
	public static String QEDIT_PLAN_INVALID_PLAN_FILE;
	public static String QEDIT_PLAN_SAVE_CHANGE_QUESTION;
	public static String QEDIT_PLAN_SAVE_CHANGE_QUESTION_TITLE;
	public static String QEDIT_PLAN_CLEAR_QUESTION;
	public static String QEDIT_PLAN_CLEAR_QUESTION_TITLE;
	public static String QEDIT_PLAN_TREE_TERM_NAME_INDEX;
	public static String QEDIT_PLAN_TREE_TERM_NAME_JOIN;
	public static String QEDIT_PLAN_TREE_TERM_NAME_SELECT;
	public static String QEDIT_PLAN_TREE_TERM_NAME_FILTER;
	public static String QEDIT_PLAN_SQL_COPY;
	public static String QEDIT_PLAN_RAW_PLAN_COPY;
	public static String QEDIT_SELECT_TABLE_NOT_EXIST_IN_DB;

	public static String TASK_QUERYDESC;

	public static String msgChangeConnectionInfo;

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".query.Messages", Messages.class);
	}
}