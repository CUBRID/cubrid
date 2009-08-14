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
package com.cubrid.cubridmanager.ui.spi;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * This is message bundle classes and provide convenience methods for
 * manipulating messages.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".spi.Messages", Messages.class);
	}
	public static String titleWarning;
	public static String titleError;
	public static String titleConfirm;
	public static String btnYes;
	public static String btnNo;
	public static String btnOk;
	public static String msgRunning;
	public static String errCannotConnectServer;
	public static String msgContextMenuCopy;
	public static String msgContextMenuPaste;
	// loader related message
	public static String msgSqlLogFolderName;
	public static String msgAccessLogFolderName;
	public static String msgErrorLogFolderName;
	public static String msgAdminLogFolderName;
	public static String msgUserFolderName;
	public static String msgJobAutoFolderName;
	public static String msgDbSpaceFolderName;
	public static String msgTablesFolderName;
	public static String msgViewsFolderName;
	public static String msgSystemTableFolderName;
	public static String msgSystemViewFolderName;
	public static String msgSpFolderName;
	public static String msgTriggerFolderName;
	public static String msgSerialFolderName;
	public static String msgGenerialVolumeFolderName;
	public static String msgDataVolumeFolderName;
	public static String msgIndexVolumeFolderName;
	public static String msgTempVolumeFolderName;
	public static String msgLogVolumeFolderName;
	public static String msgActiveLogFolderName;
	public static String msgArchiveLogFolderName;
	public static String msgBackupPlanFolderName;
	public static String msgQueryPlanFolderName;
	public static String msgLogsBrokerFolderName;
	public static String msgLogsManagerFolderName;
	public static String msgLogsServerFolderName;
	public static String msgDatabaseFolderName;
	public static String msgBrokersFolderName;
	public static String msgStatusMonitorFolderName;
	public static String msgLogsFolderName;
	public static String msgFunctionFolderName;
	public static String msgProcedureFolderName;
	public static String errDatabaseNoExist;
	public static String errBrokerNoExist;
	// property page related
	public static String msgCmServerPropertyPageName;
	public static String msgBrokerPropertyPageName;
	public static String msgManagerPropertyPageName;
	public static String msgServicePropertyPageName;
	public static String msgDatabaseServerCommonPropertyPageName;
	public static String msgDatabaseServerPropertyPageName;
	public static String msgQueryPropertyPageName;
	public static String titlePropertiesDialog;

	// action related message
	// common action
	public static String refreshActionName;
	public static String undoActionName;
	public static String redoActionName;
	public static String copyActionName;
	public static String cutActionName;
	public static String findReplaceActionName;
	public static String pasteActionName;
	public static String formatActionName;
	public static String openPreferenceActionName;
	public static String propertyActionName;
	public static String userManagementActionName;
	public static String startServiceActionName;
	public static String stopServiceActionName;
	public static String oidNavigatorActionName;
	public static String startActionName;
	public static String stopActionName;

	// host server
	public static String addHostActionName;
	public static String connectHostActionName;
	public static String disConnectHostActionName;
	public static String changePasswordActionName;
	public static String deleteHostActionName;
	public static String viewServerVersionActionName;
	// database operation
	public static String loginDatabaseActionName;
	public static String logoutDatabaseActionName;
	public static String startDatabaseActionName;
	public static String stopDatabaseActionName;
	public static String backupDatabaseActionName;
	public static String restoreDatabaseActionName;
	public static String createDatabaseActionName;
	public static String copyDatabaseActionName;
	public static String databaseStatusViewActionName;
	public static String lockInfoActionName;
	public static String checkDatabaseActionName;
	public static String renameDatabaseActionName;
	public static String loadDatabaseActionName;
	public static String unloadDatabaseActionName;
	public static String optimizeActionName;
	public static String compactDatabaseActionName;
	public static String transactionInfoActionName;
	public static String deleteDatabaseActionName;
	// db user action
	public static String deleteUserActionName;
	public static String editUserActionName;
	public static String addUserActionName;
	// query editor
	public static String queryNewActionName;
	public static String queryOpenActionName;

	// log
	public static String removeAllAccessLogActionName;
	public static String removeAllErrorLogActionName;
	public static String removeAllScriptLogActionName;
	public static String removeAllLogActionName;
	public static String removeLogActionName;
	public static String logViewActionName;
	public static String timeSetActionName;
	public static String logPropertyActionName;
	public static String activityAnalyzeCasLogActionName;
	public static String activityCasLogRunActionName;
	public static String resetAdminLogActionName;
	// backup
	public static String addBackupPlanActionName;
	public static String editBackupPlanActionName;
	public static String deleteBackupPlanActionName;
	public static String backUpErrLogActionName;
	// query plan
	public static String addQueryPlanActionName;
	public static String editQueryPlanActionName;
	public static String deleteQueryPlanActionName;
	public static String queryPlanLogActionName;

	// database space
	public static String setAutoAddVolumeActionName;
	public static String setAddVolumeActionName;
	public static String autoAddVolumeLogActionName;
	public static String spaceFolderViewActionName;
	public static String spaceInfoViewActionName;

	// query execution plan
	public static String openExecutionPlanActionName;
	public static String showSchemaActionName;

	// monitor
	public static String addStatusMonitorActionName;
	public static String editStatusMonitorActionName;
	public static String delStatusMonitorActionName;
	public static String viewStatusMonitorActionName;

	// table
	public static String tableNewActionName;
	public static String tabledeleteActionName;
	public static String tableSelectCountActionName;
	public static String tableSelectAllActionName;
	public static String tableInsertActionName;
	public static String tableExportActionName;
	public static String tableImportActionName;
	public static String tableRenameActionName;
	public static String viewRenameActionName;
	public static String tableEditActionName;
	public static String createViewActionName;
	public static String editViewActionName;
	public static String propertyViewActionName;
	// trigger
	public static String dropTriggerActionName;
	// serial
	public static String deleteSerialActionName;
	public static String createSerialActionName;
	public static String editSerialActionName;
	public static String tableDropActionName;
	public static String viewDropActionName;
	public static String newTriggerActionName;
	public static String alterTriggerActionName;
	// procedure
	public static String addFunctionActionName;
	public static String editFunctionActionName;
	public static String deleteFunctionActionName;
	public static String addProcedureActionName;
	public static String editProcedureActionName;
	public static String deleteProcedureActionName;

	// broker
	public static String startBrokerEnvActionName;
	public static String stopBrokerEnvActionName;
	public static String startBrokerActionName;
	public static String stopBrokerActionName;
	public static String showBrokersStatusActionName;
	public static String showBrokerStatusActionName;
	public static String brokerEditorPropertyActionName;

}