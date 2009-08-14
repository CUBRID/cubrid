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

import org.eclipse.jface.action.IAction;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.broker.action.ShowBrokerEnvStatusAction;
import com.cubrid.cubridmanager.ui.broker.action.ShowBrokerStatusAction;
import com.cubrid.cubridmanager.ui.broker.action.StartBrokerAction;
import com.cubrid.cubridmanager.ui.broker.action.StartBrokerEnvAction;
import com.cubrid.cubridmanager.ui.broker.action.StopBrokerAction;
import com.cubrid.cubridmanager.ui.broker.action.StopBrokerEnvAction;
import com.cubrid.cubridmanager.ui.common.action.AddHostAction;
import com.cubrid.cubridmanager.ui.common.action.ChangePasswordAction;
import com.cubrid.cubridmanager.ui.common.action.ConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.DeleteHostAction;
import com.cubrid.cubridmanager.ui.common.action.DisConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.OIDNavigatorAction;
import com.cubrid.cubridmanager.ui.common.action.PropertyAction;
import com.cubrid.cubridmanager.ui.common.action.RefreshAction;
import com.cubrid.cubridmanager.ui.common.action.StartRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StartServiceAction;
import com.cubrid.cubridmanager.ui.common.action.StopRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StopServiceAction;
import com.cubrid.cubridmanager.ui.common.action.UserManagementAction;
import com.cubrid.cubridmanager.ui.common.action.ViewServerVersionAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.BackupDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CheckDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CompactDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CopyDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CreateDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.DatabaseStatusViewAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.DeleteDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LoadDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LockInfoAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LoginDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LogoutDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.OptimizeAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.RenameDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.RestoreDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StartDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StopDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.TransactionInfoAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.UnloadDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.action.AddVolumeAction;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.action.AutoAddVolumeLogAction;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.action.SetAutoAddVolumeAction;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.action.SpaceFolderViewAction;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.action.SpaceInfoViewAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.AddBackupPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.AddQueryPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.BackUpErrLogAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.DeleteBackupPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.DeleteQueryPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.EditBackupPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.EditQueryPlanAction;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.action.QueryLogAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.AddFunctionAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.AddProcedureAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.DeleteFunctionAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.DeleteProcedureAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.EditFunctionAction;
import com.cubrid.cubridmanager.ui.cubrid.procedure.action.EditProcedureAction;
import com.cubrid.cubridmanager.ui.cubrid.serial.action.CreateSerialAction;
import com.cubrid.cubridmanager.ui.cubrid.serial.action.DeleteSerialAction;
import com.cubrid.cubridmanager.ui.cubrid.serial.action.EditSerialAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.CreateViewAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.DeleteTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.DropTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.EditTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.EditViewAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.ExportTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.ImportTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.InsertTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.NewTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.PropertyViewAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.RenameTableAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.TableSelectAllAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.TableSelectCountAction;
import com.cubrid.cubridmanager.ui.cubrid.trigger.action.AlterTriggerAction;
import com.cubrid.cubridmanager.ui.cubrid.trigger.action.DropTriggerAction;
import com.cubrid.cubridmanager.ui.cubrid.trigger.action.NewTriggerAction;
import com.cubrid.cubridmanager.ui.cubrid.user.action.AddUserAction;
import com.cubrid.cubridmanager.ui.cubrid.user.action.DeleteUserAction;
import com.cubrid.cubridmanager.ui.cubrid.user.action.EditUserAction;
import com.cubrid.cubridmanager.ui.logs.action.AnalyzeSqlLogAction;
import com.cubrid.cubridmanager.ui.logs.action.ExecuteSqlLogAction;
import com.cubrid.cubridmanager.ui.logs.action.LogPropertyAction;
import com.cubrid.cubridmanager.ui.logs.action.LogViewAction;
import com.cubrid.cubridmanager.ui.logs.action.ManagerLogViewAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveAllAccessLogAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveAllDbLogAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveAllErrorLogAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveAllManagerLogAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveAllScriptLogAction;
import com.cubrid.cubridmanager.ui.logs.action.RemoveLogAction;
import com.cubrid.cubridmanager.ui.logs.action.ResetAdminLogAction;
import com.cubrid.cubridmanager.ui.logs.action.TimeSetAction;
import com.cubrid.cubridmanager.ui.monitoring.action.AddStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.DeleteStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.EditStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.ShowStatusMonitorAction;
import com.cubrid.cubridmanager.ui.query.action.CopyAction;
import com.cubrid.cubridmanager.ui.query.action.CutAction;
import com.cubrid.cubridmanager.ui.query.action.FindReplaceAction;
import com.cubrid.cubridmanager.ui.query.action.PasteAction;
import com.cubrid.cubridmanager.ui.query.action.QueryNewAction;
import com.cubrid.cubridmanager.ui.query.action.QueryOpenAction;
import com.cubrid.cubridmanager.ui.query.action.RedoAction;
import com.cubrid.cubridmanager.ui.query.action.ShowSchemaAction;
import com.cubrid.cubridmanager.ui.query.action.SqlFormatAction;
import com.cubrid.cubridmanager.ui.query.action.UndoAction;

/**
 * 
 * This class is responsible to build CUBRID Manager menu and toolbar action
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ActionBuilder {

	// implemeted actions for retarget actions
	private IAction undoAction = null;
	private IAction redoAction = null;
	private IAction copyAction = null;
	private IAction pasteAction = null;
	private IAction cutAction = null;
	private IAction findReplaceAction = null;

	// customized action for CUBRID Manager
	// common actions
	private IAction propertyAction = null;
	private IAction refreshAction = null;
	private IAction userManagementAction = null;
	private IAction changePasswordAction = null;
	private IAction oidNavigatorAction = null;
	private IAction viewServerVersionAction = null;
	private IAction startServiceAction = null;
	private IAction stopServiceAction = null;
	private IAction startAction = null;
	private IAction stopAction = null;
	//host actions
	private IAction addHostAction = null;
	private IAction deleteHostAction = null;
	private IAction connectHostAction = null;
	private IAction disConnectHostAction = null;
	// database actions
	private IAction copyDatabaseAction = null;
	private IAction databaseStatusViewAction = null;
	private IAction createDatabaseAction = null;
	private IAction loginDatabaseAction = null;
	private IAction logoutDatabaseAction = null;
	private IAction startDatabaseAction = null;
	private IAction stopDatabaseAction = null;
	private IAction loadDatabaseAction = null;
	private IAction unloadDatabaseAction = null;
	private IAction backupDatabaseAction = null;
	private IAction restoreDatabaseAction = null;
	private IAction renameDatabaseAction = null;
	private IAction optimizeAction = null;
	private IAction compactDatabaseAction = null;
	private IAction lockInfoAction = null;
	private IAction checkDatabaseAction = null;
	private IAction transactionInfoAction = null;
	private IAction deleteDatabaseAction = null;
	// user actions
	private IAction editUserAction = null;
	private IAction addUserAction = null;
	private IAction deleteUserAction = null;

	// query editor actions
	private IAction queryNewAction;
	private QueryOpenAction queryOpenAction;

	// backup plan action
	private IAction addBackupPlanAction;
	private IAction editBackupPlanAction;
	private IAction deleteBackupPlanAction;
	private IAction backUpErrLogAction;
	// query plan action
	private IAction addQueryPlanAction;
	private IAction editQueryPlanAction;
	private IAction deleteQueryPlanAction;
	private IAction queryLogAction;

	// database space actions
	private IAction setAutoAddVolumeAction;
	private IAction addVolumeAction;
	private IAction spaceFolderViewAction;
	private IAction spaceInfoViewAction;
	private IAction autoAddVolumeLogAction;
	// logs
	private IAction removeAllAccessLogAction;
	private IAction removeAllErrorLogAction;
	private IAction removeAllScriptLogAction;
	private IAction removeAllLogAction;
	private IAction removeLogAction;
	private IAction logViewAction;
	private IAction timeSetAction;
	private IAction managerLogViewAction;
	private IAction RemoveAllManagerLogAction;
	private IAction activityAnalyzeCasLogAction;
	private IAction activityCasLogRunAction;
	private IAction logPropertyAction;
	private IAction resetAdminLogAction;
	// monitor
	private IAction addStatusMonitorAction;
	private IAction editStatusMonitorAction;
	private IAction delStatusMonitorAction;
	private IAction showStatusMonitorAction;
	// table
	private IAction tableNewAction;
	private IAction tableDeleteAction;
	private IAction tableSelectCountAction;
	private IAction tableSelectAllAction;
	private IAction tableInsertAction;
	private IAction tableExportAction;
	private IAction tableRenameAction;
	private IAction tableImportAction;
	private IAction tableDropAction;
	private IAction tableEditAction;
	private IAction createViewAction;
	private IAction editViewAction;
	private IAction propertyViewAction;
	private IAction showSchemaAction;
	// serial
	private IAction deleteSerialAction;
	private IAction createSerialAction;
	private IAction editSerialAction;
	//trigger
	private IAction dropTriggerAction;
	private IAction newTriggerAction;
	private IAction alterTriggerAction;
	// procedure
	private IAction addFunctionAction;
	private IAction editFunctionAction;
	private IAction deleteFunctionAction;

	private IAction addProcedureAction;
	private IAction editProcedureAction;
	private IAction deleteProcedureAction;
	//broker
	private IAction startBrokerEnvAction;
	private IAction stopBrokerEnvAction;
	private IAction startBrokerAction;
	private IAction stopBrokerAction;
	private IAction showBrokersStatusAction;
	private IAction showBrokerStatusAction;

	/**
	 * 
	 * Make all actions for CUBRID Manager menu and toolbar
	 * 
	 * @param window
	 */
	public void makeActions(IWorkbenchWindow window) {

		// implemented actions for retarget actions
		undoAction = new UndoAction(window.getShell(), Messages.undoActionName,
				null);
		ActionManager.getInstance().registerAction(undoAction);

		redoAction = new RedoAction(window.getShell(), Messages.redoActionName,
				null);
		ActionManager.getInstance().registerAction(redoAction);

		copyAction = new CopyAction(window.getShell(), Messages.copyActionName,
				null);
		ActionManager.getInstance().registerAction(copyAction);

		pasteAction = new PasteAction(window.getShell(),
				Messages.pasteActionName, null);
		ActionManager.getInstance().registerAction(pasteAction);

		cutAction = new CutAction(window.getShell(), Messages.cutActionName,
				null);
		ActionManager.getInstance().registerAction(cutAction);

		findReplaceAction = new FindReplaceAction(window.getShell(),
				Messages.findReplaceActionName, null);
		ActionManager.getInstance().registerAction(findReplaceAction);
		// customized actions for CUBRID Manager
		//common action

		propertyAction = new PropertyAction(
				window.getShell(),
				Messages.propertyActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/property.png"));
		ActionManager.getInstance().registerAction(propertyAction);

		userManagementAction = new UserManagementAction(
				window.getShell(),
				Messages.userManagementActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_cmuser.png"));
		ActionManager.getInstance().registerAction(userManagementAction);

		oidNavigatorAction = new OIDNavigatorAction(window.getShell(),
				Messages.oidNavigatorActionName, null);
		ActionManager.getInstance().registerAction(oidNavigatorAction);

		refreshAction = new RefreshAction(
				window.getShell(),
				Messages.refreshActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/refresh.png"));
		ActionManager.getInstance().registerAction(refreshAction);

		startServiceAction = new StartServiceAction(
				window.getShell(),
				Messages.startServiceActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_service_start.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_service_start_disabled.png"));

		ActionManager.getInstance().registerAction(startServiceAction);
		stopServiceAction = new StopServiceAction(
				window.getShell(),
				Messages.stopServiceActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_service_stop.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_service_stop_disabled.png"));
		ActionManager.getInstance().registerAction(stopServiceAction);

		ActionManager.getInstance().registerAction(
				ActionFactory.PROPERTIES.create(window));

		startAction = new StartRetargetAction(
				window.getShell(),
				Messages.startActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_start.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_start_disabled.png"));
		ActionManager.getInstance().registerAction(startAction);

		stopAction = new StopRetargetAction(
				window.getShell(),
				Messages.stopActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_stop.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_stop_disabled.png"));
		ActionManager.getInstance().registerAction(stopAction);
		//host related action
		addHostAction = new AddHostAction(
				window.getShell(),
				Messages.addHostActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_add.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_add_disabled.png"));
		ActionManager.getInstance().registerAction(addHostAction);

		deleteHostAction = new DeleteHostAction(
				window.getShell(),
				Messages.deleteHostActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_delete.png"));
		ActionManager.getInstance().registerAction(deleteHostAction);

		connectHostAction = new ConnectHostAction(
				window.getShell(),
				Messages.connectHostActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_connect.png"));
		ActionManager.getInstance().registerAction(connectHostAction);

		disConnectHostAction = new DisConnectHostAction(
				window.getShell(),
				Messages.disConnectHostActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_disconnect.png"));
		ActionManager.getInstance().registerAction(disConnectHostAction);

		changePasswordAction = new ChangePasswordAction(
				window.getShell(),
				Messages.changePasswordActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/host_change_password.png"));
		ActionManager.getInstance().registerAction(changePasswordAction);

		viewServerVersionAction = new ViewServerVersionAction(
				window.getShell(),
				Messages.viewServerVersionActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_version.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/menu_version_disabled.png"));
		ActionManager.getInstance().registerAction(viewServerVersionAction);
		//database related action
		createDatabaseAction = new CreateDatabaseAction(
				window.getShell(),
				Messages.createDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_create.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_create_disabled.png"));
		ActionManager.getInstance().registerAction(createDatabaseAction);

		loginDatabaseAction = new LoginDatabaseAction(
				window.getShell(),
				Messages.loginDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_login.png"));
		ActionManager.getInstance().registerAction(loginDatabaseAction);

		logoutDatabaseAction = new LogoutDatabaseAction(
				window.getShell(),
				Messages.logoutDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_logout.png"));
		ActionManager.getInstance().registerAction(logoutDatabaseAction);

		startDatabaseAction = new StartDatabaseAction(
				window.getShell(),
				Messages.startDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_start.png"));
		ActionManager.getInstance().registerAction(startDatabaseAction);

		stopDatabaseAction = new StopDatabaseAction(
				window.getShell(),
				Messages.stopDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_stop.png"));
		ActionManager.getInstance().registerAction(stopDatabaseAction);

		loadDatabaseAction = new LoadDatabaseAction(
				window.getShell(),
				Messages.loadDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_load.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_load_disabled.png"));
		ActionManager.getInstance().registerAction(loadDatabaseAction);

		unloadDatabaseAction = new UnloadDatabaseAction(
				window.getShell(),
				Messages.unloadDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_unload.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_unload_disabled.png"));
		ActionManager.getInstance().registerAction(unloadDatabaseAction);

		backupDatabaseAction = new BackupDatabaseAction(
				window.getShell(),
				Messages.backupDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_backup.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_backup_disabled.png"));
		ActionManager.getInstance().registerAction(backupDatabaseAction);

		restoreDatabaseAction = new RestoreDatabaseAction(
				window.getShell(),
				Messages.restoreDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_restore.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_restore_disabled.png"));
		ActionManager.getInstance().registerAction(restoreDatabaseAction);

		renameDatabaseAction = new RenameDatabaseAction(
				window.getShell(),
				Messages.renameDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_rename.png"));
		ActionManager.getInstance().registerAction(renameDatabaseAction);

		copyDatabaseAction = new CopyDatabaseAction(
				window.getShell(),
				Messages.copyDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_copy.png"));
		ActionManager.getInstance().registerAction(copyDatabaseAction);

		databaseStatusViewAction = new DatabaseStatusViewAction(
				window.getShell(), Messages.databaseStatusViewActionName, null);
		ActionManager.getInstance().registerAction(databaseStatusViewAction);

		lockInfoAction = new LockInfoAction(
				window.getShell(),
				Messages.lockInfoActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_lockinfo.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_lockinfo_disabled.png"));
		ActionManager.getInstance().registerAction(lockInfoAction);

		transactionInfoAction = new TransactionInfoAction(
				window.getShell(),
				Messages.transactionInfoActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_traninfo.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_traninfo_disabled.png"));
		ActionManager.getInstance().registerAction(transactionInfoAction);

		deleteDatabaseAction = new DeleteDatabaseAction(
				window.getShell(),
				Messages.deleteDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_delete.png"));
		ActionManager.getInstance().registerAction(deleteDatabaseAction);

		checkDatabaseAction = new CheckDatabaseAction(
				window.getShell(),
				Messages.checkDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_check.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_check_disabled.png"));
		ActionManager.getInstance().registerAction(checkDatabaseAction);

		optimizeAction = new OptimizeAction(
				window.getShell(),
				Messages.optimizeActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_optimize.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_optimize_disabled.png"));
		ActionManager.getInstance().registerAction(optimizeAction);

		compactDatabaseAction = new CompactDatabaseAction(
				window.getShell(),
				Messages.compactDatabaseActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/database_compact.png"));
		ActionManager.getInstance().registerAction(compactDatabaseAction);
		//database user related action
		editUserAction = new EditUserAction(
				window.getShell(),
				Messages.editUserActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/user_edit.png"));
		ActionManager.getInstance().registerAction(editUserAction);

		addUserAction = new AddUserAction(
				window.getShell(),
				Messages.addUserActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/user_add.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/user_add_disabled.png"));
		ActionManager.getInstance().registerAction(addUserAction);

		deleteUserAction = new DeleteUserAction(
				window.getShell(),
				Messages.deleteUserActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/user_delete.png"));
		ActionManager.getInstance().registerAction(deleteUserAction);
		//job auto related action
		addBackupPlanAction = new AddBackupPlanAction(
				window.getShell(),
				Messages.addBackupPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_backup_add.png"));
		ActionManager.getInstance().registerAction(addBackupPlanAction);

		editBackupPlanAction = new EditBackupPlanAction(
				window.getShell(),
				Messages.editBackupPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_backup_edit.png"));
		ActionManager.getInstance().registerAction(editBackupPlanAction);

		deleteBackupPlanAction = new DeleteBackupPlanAction(
				window.getShell(),
				Messages.deleteBackupPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_backup_delete.png"));
		ActionManager.getInstance().registerAction(deleteBackupPlanAction);

		backUpErrLogAction = new BackUpErrLogAction(
				window.getShell(),
				Messages.backUpErrLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_log.png"));
		ActionManager.getInstance().registerAction(backUpErrLogAction);

		addQueryPlanAction = new AddQueryPlanAction(
				window.getShell(),
				Messages.addQueryPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_query_add.png"));
		ActionManager.getInstance().registerAction(addQueryPlanAction);

		editQueryPlanAction = new EditQueryPlanAction(
				window.getShell(),
				Messages.editQueryPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_query_edit.png"));
		ActionManager.getInstance().registerAction(editQueryPlanAction);

		deleteQueryPlanAction = new DeleteQueryPlanAction(
				window.getShell(),
				Messages.deleteQueryPlanActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_query_delete.png"));
		ActionManager.getInstance().registerAction(deleteQueryPlanAction);

		queryLogAction = new QueryLogAction(
				window.getShell(),
				Messages.queryPlanLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_log.png"));
		ActionManager.getInstance().registerAction(queryLogAction);
		//database space related action
		setAutoAddVolumeAction = new SetAutoAddVolumeAction(
				window.getShell(),
				Messages.setAutoAddVolumeActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/volume_auto_add.png"));
		ActionManager.getInstance().registerAction(setAutoAddVolumeAction);

		addVolumeAction = new AddVolumeAction(
				window.getShell(),
				Messages.setAddVolumeActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/volume_add.png"));
		ActionManager.getInstance().registerAction(addVolumeAction);

		autoAddVolumeLogAction = new AutoAddVolumeLogAction(
				window.getShell(),
				Messages.autoAddVolumeLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_log.png"));
		ActionManager.getInstance().registerAction(autoAddVolumeLogAction);

		spaceFolderViewAction = new SpaceFolderViewAction(window.getShell(),
				Messages.spaceFolderViewActionName, null);
		ActionManager.getInstance().registerAction(spaceFolderViewAction);

		spaceInfoViewAction = new SpaceInfoViewAction(window.getShell(),
				Messages.spaceInfoViewActionName, null);
		ActionManager.getInstance().registerAction(spaceInfoViewAction);

		//status monitor related action
		addStatusMonitorAction = new AddStatusMonitorTemplateAction(
				window.getShell(),
				Messages.addStatusMonitorActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/status_add.png"));
		ActionManager.getInstance().registerAction(addStatusMonitorAction);

		editStatusMonitorAction = new EditStatusMonitorTemplateAction(
				window.getShell(),
				Messages.editStatusMonitorActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/status_edit.png"));
		ActionManager.getInstance().registerAction(editStatusMonitorAction);

		delStatusMonitorAction = new DeleteStatusMonitorTemplateAction(
				window.getShell(),
				Messages.delStatusMonitorActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/status_delete.png"));
		ActionManager.getInstance().registerAction(delStatusMonitorAction);

		showStatusMonitorAction = new ShowStatusMonitorAction(
				window.getShell(),
				Messages.viewStatusMonitorActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/status_execute.png"));
		ActionManager.getInstance().registerAction(showStatusMonitorAction);
		//logs related action
		removeAllAccessLogAction = new RemoveAllAccessLogAction(
				window.getShell(),
				Messages.removeAllAccessLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete_all.png"));
		ActionManager.getInstance().registerAction(removeAllAccessLogAction);

		removeAllErrorLogAction = new RemoveAllErrorLogAction(
				window.getShell(),
				Messages.removeAllErrorLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete_all.png"));
		ActionManager.getInstance().registerAction(removeAllErrorLogAction);

		removeAllScriptLogAction = new RemoveAllScriptLogAction(
				window.getShell(),
				Messages.removeAllScriptLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete_all.png"));
		ActionManager.getInstance().registerAction(removeAllScriptLogAction);

		removeAllLogAction = new RemoveAllDbLogAction(
				window.getShell(),
				Messages.removeAllLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete_all.png"));
		ActionManager.getInstance().registerAction(removeAllLogAction);

		removeLogAction = new RemoveLogAction(
				window.getShell(),
				Messages.removeLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete.png"));
		ActionManager.getInstance().registerAction(removeLogAction);

		logViewAction = new LogViewAction(
				window.getShell(),
				Messages.logViewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_view.png"));
		ActionManager.getInstance().registerAction(logViewAction);

		timeSetAction = new TimeSetAction(
				window.getShell(),
				Messages.timeSetActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/auto_log.png"));
		ActionManager.getInstance().registerAction(timeSetAction);

		managerLogViewAction = new ManagerLogViewAction(
				window.getShell(),
				Messages.logViewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_view.png"));
		ActionManager.getInstance().registerAction(managerLogViewAction);

		RemoveAllManagerLogAction = new RemoveAllManagerLogAction(
				window.getShell(),
				Messages.removeLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_delete.png"));
		ActionManager.getInstance().registerAction(RemoveAllManagerLogAction);

		logPropertyAction = new LogPropertyAction(
				window.getShell(),
				Messages.logPropertyActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/log_property.png"));
		ActionManager.getInstance().registerAction(logPropertyAction);

		activityAnalyzeCasLogAction = new AnalyzeSqlLogAction(
				window.getShell(),
				Messages.activityAnalyzeCasLogActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/sqllog_analysis.png"));
		ActionManager.getInstance().registerAction(activityAnalyzeCasLogAction);

		activityCasLogRunAction = new ExecuteSqlLogAction(
				window.getShell(),
				Messages.activityCasLogRunActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/sqllog_execute.png"));
		ActionManager.getInstance().registerAction(activityCasLogRunAction);

		resetAdminLogAction = new ResetAdminLogAction(window.getShell(),
				Messages.resetAdminLogActionName, null);
		ActionManager.getInstance().registerAction(resetAdminLogAction);

		//table schema related action
		tableNewAction = new NewTableAction(
				window.getShell(),
				Messages.tableNewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_add.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_add_disabled.png"));
		ActionManager.getInstance().registerAction(tableNewAction);

		tableEditAction = new EditTableAction(
				window.getShell(),
				Messages.tableEditActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_edit.png"));
		ActionManager.getInstance().registerAction(tableEditAction);

		createViewAction = new CreateViewAction(
				window.getShell(),
				Messages.createViewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_add.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_add_disabled.png"));
		ActionManager.getInstance().registerAction(createViewAction);

		editViewAction = new EditViewAction(
				window.getShell(),
				Messages.editViewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_edit.png"));
		ActionManager.getInstance().registerAction(editViewAction);
		propertyViewAction = new PropertyViewAction(window.getShell(),
				Messages.propertyViewActionName, null);
		ActionManager.getInstance().registerAction(propertyViewAction);
		tableSelectCountAction = new TableSelectCountAction(
				window.getShell(),
				Messages.tableSelectCountActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_select_count.png"));
		ActionManager.getInstance().registerAction(tableSelectCountAction);

		tableDeleteAction = new DeleteTableAction(
				window.getShell(),
				Messages.tabledeleteActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/schema_table_delete.png"));
		ActionManager.getInstance().registerAction(tableDeleteAction);

		tableSelectAllAction = new TableSelectAllAction(
				window.getShell(),
				Messages.tableSelectAllActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_select_all.png"));
		ActionManager.getInstance().registerAction(tableSelectAllAction);

		tableInsertAction = new InsertTableAction(
				window.getShell(),
				Messages.tableInsertActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_record_insert.png"));
		ActionManager.getInstance().registerAction(tableInsertAction);

		tableExportAction = new ExportTableAction(
				window.getShell(),
				Messages.tableExportActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_data_export.png"));
		ActionManager.getInstance().registerAction(tableExportAction);

		tableImportAction = new ImportTableAction(
				window.getShell(),
				Messages.tableImportActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_data_import.png"));
		ActionManager.getInstance().registerAction(tableImportAction);

		tableRenameAction = new RenameTableAction(
				window.getShell(),
				Messages.tableRenameActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/table_rename.png"));
		ActionManager.getInstance().registerAction(tableRenameAction);

		tableDropAction = new DropTableAction(window.getShell(),
				Messages.tableDropActionName, null);
		ActionManager.getInstance().registerAction(tableDropAction);

		showSchemaAction = new ShowSchemaAction(window.getShell(),
				Messages.showSchemaActionName, null);
		ActionManager.getInstance().registerAction(showSchemaAction);

		//trigger related action
		newTriggerAction = new NewTriggerAction(
				window.getShell(),
				Messages.newTriggerActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/trigger_add.png"));
		ActionManager.getInstance().registerAction(newTriggerAction);

		alterTriggerAction = new AlterTriggerAction(
				window.getShell(),
				Messages.alterTriggerActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/trigger_edit.png"));
		ActionManager.getInstance().registerAction(alterTriggerAction);

		dropTriggerAction = new DropTriggerAction(
				window.getShell(),
				Messages.dropTriggerActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/trigger_delete.png"));
		ActionManager.getInstance().registerAction(dropTriggerAction);

		//serial related action
		deleteSerialAction = new DeleteSerialAction(
				window.getShell(),
				Messages.deleteSerialActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/serial_delete.png"));
		ActionManager.getInstance().registerAction(deleteSerialAction);

		createSerialAction = new CreateSerialAction(
				window.getShell(),
				Messages.createSerialActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/serial_add.png"));
		ActionManager.getInstance().registerAction(createSerialAction);

		editSerialAction = new EditSerialAction(
				window.getShell(),
				Messages.editSerialActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/serial_edit.png"));
		ActionManager.getInstance().registerAction(editSerialAction);
		//stored procedure related action
		addFunctionAction = new AddFunctionAction(
				window.getShell(),
				Messages.addFunctionActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_add.png"));
		ActionManager.getInstance().registerAction(addFunctionAction);
		editFunctionAction = new EditFunctionAction(
				window.getShell(),
				Messages.editFunctionActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_edit.png"));
		ActionManager.getInstance().registerAction(editFunctionAction);
		deleteFunctionAction = new DeleteFunctionAction(
				window.getShell(),
				Messages.deleteFunctionActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_delete.png"));
		ActionManager.getInstance().registerAction(deleteFunctionAction);

		addProcedureAction = new AddProcedureAction(
				window.getShell(),
				Messages.addProcedureActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_add.png"));
		ActionManager.getInstance().registerAction(addProcedureAction);
		editProcedureAction = new EditProcedureAction(
				window.getShell(),
				Messages.editProcedureActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_edit.png"));
		ActionManager.getInstance().registerAction(editProcedureAction);
		deleteProcedureAction = new DeleteProcedureAction(
				window.getShell(),
				Messages.deleteProcedureActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/procedure_delete.png"));
		ActionManager.getInstance().registerAction(deleteProcedureAction);
		//broker related action
		startBrokerEnvAction = new StartBrokerEnvAction(
				window.getShell(),
				Messages.startBrokerEnvActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/broker_group_start.png"));
		ActionManager.getInstance().registerAction(startBrokerEnvAction);

		stopBrokerEnvAction = new StopBrokerEnvAction(
				window.getShell(),
				Messages.stopBrokerEnvActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/broker_group_stop.png"));
		ActionManager.getInstance().registerAction(stopBrokerEnvAction);

		startBrokerAction = new StartBrokerAction(window.getShell(),
				Messages.startBrokerActionName, null);
		ActionManager.getInstance().registerAction(startBrokerAction);

		stopBrokerAction = new StopBrokerAction(window.getShell(),
				Messages.stopBrokerActionName, null);
		ActionManager.getInstance().registerAction(stopBrokerAction);

		showBrokersStatusAction = new ShowBrokerEnvStatusAction(
				window.getShell(), Messages.showBrokersStatusActionName);
		ActionManager.getInstance().registerAction(showBrokersStatusAction);

		showBrokerStatusAction = new ShowBrokerStatusAction(window.getShell(),
				Messages.showBrokerStatusActionName);
		ActionManager.getInstance().registerAction(showBrokerStatusAction);

		//query editor related action
		SqlFormatAction formatAction = new SqlFormatAction(
				window.getShell(),
				Messages.formatActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/queryeditor/query_format.png"));
		ActionManager.getInstance().registerAction(formatAction);

		queryNewAction = new QueryNewAction(
				window.getShell(),
				Messages.queryNewActionName,
				CubridManagerUIPlugin.getImageDescriptor("icons/action/new_query.png"),
				CubridManagerUIPlugin.getImageDescriptor("icons/action/new_query_disabled.png"));
		ActionManager.getInstance().registerAction(queryNewAction);

		queryOpenAction = new QueryOpenAction(window.getShell(),
				Messages.queryOpenActionName, null);
		ActionManager.getInstance().registerAction(queryOpenAction);
	}
}
