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

import java.util.HashMap;
import java.util.Map;

import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.window.ApplicationWindow;
import org.eclipse.swt.widgets.Control;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.ui.broker.action.ShowBrokerEnvStatusAction;
import com.cubrid.cubridmanager.ui.broker.action.ShowBrokerStatusAction;
import com.cubrid.cubridmanager.ui.broker.action.StartBrokerAction;
import com.cubrid.cubridmanager.ui.broker.action.StartBrokerEnvAction;
import com.cubrid.cubridmanager.ui.broker.action.StopBrokerAction;
import com.cubrid.cubridmanager.ui.broker.action.StopBrokerEnvAction;
import com.cubrid.cubridmanager.ui.common.action.OIDNavigatorAction;
import com.cubrid.cubridmanager.ui.common.action.PropertyAction;
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
import com.cubrid.cubridmanager.ui.monitoring.action.AddStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.DeleteStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.EditStatusMonitorTemplateAction;
import com.cubrid.cubridmanager.ui.monitoring.action.ShowStatusMonitorAction;
import com.cubrid.cubridmanager.ui.query.action.QueryNewAction;
import com.cubrid.cubridmanager.ui.spi.action.IFocusAction;
import com.cubrid.cubridmanager.ui.spi.action.ISelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * A action manager is responsible for mananging all actions in menubar and
 * toolbar.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ActionManager implements
		ISelectionChangedListener {

	public static final String M_ACTION = "CMActions";
	public static final String M_RUN = "CMRun";
	public static final String M_TOOLS = "CMTools";
	public static final String M_CUBRID_MENU = "M_CUBRID";
	private static ActionManager manager = new ActionManager();
	private Map<String, ISelectionAction> selectionActions = new HashMap<String, ISelectionAction>();
	private Map<String, IFocusAction> foucsActions = new HashMap<String, IFocusAction>();
	private Map<String, IAction> actions = new HashMap<String, IAction>();
	private ISelectionProvider selectionProvider = null;
	private Control focusProvider = null;

	/**
	 * The constructor
	 */
	private ActionManager() {
	}

	/**
	 * 
	 * Return the only action manager instance
	 * 
	 * @return
	 */
	public static ActionManager getInstance() {
		return manager;
	}

	/**
	 * 
	 * Make actions
	 * 
	 * @param window
	 */
	public void makeActions(IWorkbenchWindow window) {
		new ActionBuilder().makeActions(window);
	}

	/**
	 * 
	 * Register action
	 * 
	 * @param action
	 */
	public synchronized void registerAction(IAction action) {
		if (action != null && action.getId() != null
				&& action.getId().trim().length() > 0) {
			if (action instanceof ISelectionAction) {
				selectionActions.put(action.getId(), (ISelectionAction) action);
				((ISelectionAction) action).setSelectionProvider(selectionProvider);
			} else if (action instanceof IFocusAction) {
				foucsActions.put(action.getId(), (IFocusAction) action);
				((IFocusAction) action).setFocusProvider(this.focusProvider);
			} else {
				actions.put(action.getId(), action);
			}
		}
	}

	/**
	 * 
	 * Get SelectionProvider
	 * 
	 * @return
	 */
	public ISelectionProvider getSelectionProvider() {
		return this.selectionProvider;
	}

	/**
	 * 
	 * Get FocusProvider
	 * 
	 * @return
	 */
	public Control getFocusProvider() {
		return this.focusProvider;
	}

	/**
	 * 
	 * Get registered action by action ID
	 * 
	 * @param id
	 * @return
	 */
	public IAction getAction(String id) {
		if (id != null && id.trim().length() > 0) {
			IAction action = selectionActions.get(id);
			if (action != null)
				return action;
			action = foucsActions.get(id);
			if (action != null)
				return action;
			return actions.get(id);
		}
		return null;
	}

	/**
	 * Change focus provider for IFocusAction and add action to focus provider
	 * to listen to focus changed event
	 * 
	 * @param control
	 */
	public void changeFocusProvider(Control control) {
		for (IFocusAction action : foucsActions.values()) {
			action.setFocusProvider(control);
		}
		focusProvider = control;
	}

	/**
	 * Change selection provider for ISelectionAction and add action to
	 * selection provider to listen to selection changed event
	 * 
	 * @param provider
	 */
	public void changeSelectionProvider(ISelectionProvider provider) {
		for (ISelectionAction action : selectionActions.values()) {
			action.setSelectionProvider(provider);
		}
		if (provider != null) {
			if (this.selectionProvider != null) {
				this.selectionProvider.removeSelectionChangedListener(this);
			}
			this.selectionProvider = provider;
			this.selectionProvider.addSelectionChangedListener(this);
			changeActionMenu();
		}
	}

	/**
	 * 
	 * Fire selection change event
	 * 
	 * @param selection
	 */
	public void fireSelectionChanged(ISelection selection) {
		SelectionChangedEvent event = new SelectionChangedEvent(
				getSelectionProvider(), selection);
		for (ISelectionAction action : selectionActions.values()) {
			action.selectionChanged(event);
		}
		selectionChanged(event);
	}

	/**
	 * 
	 * Fill in action menu,build action menubar and navigator context menu
	 * shared action
	 * 
	 * @param manager
	 */
	public void setActionsMenu(IMenuManager manager) {
		if (this.selectionProvider == null) {
			return;
		}
		IStructuredSelection selection = (IStructuredSelection) selectionProvider.getSelection();
		if (selection == null || selection.isEmpty()) {
			return;
		}
		ICubridNode node = null;
		Object obj = selection.getFirstElement();
		if (obj instanceof ICubridNode) {
			node = (ICubridNode) obj;
		} else {
			return;
		}
		// fill Action Menu according to node type
		CubridNodeType type = node.getType();
		switch (type) {
		case SERVER:
			addActionToManager(manager, getAction(ViewServerVersionAction.ID));
			addActionToManager(manager, getAction(PropertyAction.ID));
			manager.add(new Separator());
			break;
		case DATABASE_FOLDER:
			addActionToManager(manager, getAction(CreateDatabaseAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(PropertyAction.ID));
			manager.add(new Separator());
			break;
		case DATABASE:
			addActionToManager(manager, getAction(LoginDatabaseAction.ID));
			addActionToManager(manager, getAction(LogoutDatabaseAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(StartDatabaseAction.ID));
			addActionToManager(manager, getAction(StopDatabaseAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(QueryNewAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LoadDatabaseAction.ID));
			addActionToManager(manager, getAction(UnloadDatabaseAction.ID));
			addActionToManager(manager, getAction(BackupDatabaseAction.ID));
			addActionToManager(manager, getAction(RestoreDatabaseAction.ID));
			addActionToManager(manager, getAction(RenameDatabaseAction.ID));
			addActionToManager(manager, getAction(CopyDatabaseAction.ID));
			addActionToManager(manager, getAction(OptimizeAction.ID));
			addActionToManager(manager, getAction(CompactDatabaseAction.ID));
			addActionToManager(manager, getAction(CheckDatabaseAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LockInfoAction.ID));
			addActionToManager(manager, getAction(TransactionInfoAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DeleteDatabaseAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(OIDNavigatorAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(PropertyAction.ID));
			break;
		case USER_FOLDER:
			addActionToManager(manager, getAction(AddUserAction.ID));
			break;
		case USER:
			addActionToManager(manager, getAction(EditUserAction.ID));
			addActionToManager(manager, getAction(DeleteUserAction.ID));
			break;
		case JOB_FOLDER:
			break;
		case BACKUP_PLAN_FOLDER:
			addActionToManager(manager, getAction(AddBackupPlanAction.ID));
			addActionToManager(manager, getAction(BackUpErrLogAction.ID));
			break;
		case BACKUP_PLAN:
			addActionToManager(manager, getAction(EditBackupPlanAction.ID));
			addActionToManager(manager, getAction(DeleteBackupPlanAction.ID));
			break;
		case QUERY_PLAN_FOLDER:
			addActionToManager(manager, getAction(AddQueryPlanAction.ID));
			addActionToManager(manager, getAction(QueryLogAction.ID));
			break;
		case QUERY_PLAN:
			addActionToManager(manager, getAction(EditQueryPlanAction.ID));
			addActionToManager(manager, getAction(DeleteQueryPlanAction.ID));
			break;
		case DBSPACE_FOLDER:
			addActionToManager(manager, getAction(SetAutoAddVolumeAction.ID));
			addActionToManager(manager, getAction(AddVolumeAction.ID));
			addActionToManager(manager, getAction(AutoAddVolumeLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DatabaseStatusViewAction.ID));
			break;
		case GENERIC_VOLUME_FOLDER:
			addActionToManager(manager, getAction(AddVolumeAction.ID));
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case DATA_VOLUME_FOLDER:
			addActionToManager(manager, getAction(AddVolumeAction.ID));
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case INDEX_VOLUME_FOLDER:
			addActionToManager(manager, getAction(AddVolumeAction.ID));
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case TEMP_VOLUME_FOLDER:
			addActionToManager(manager, getAction(AddVolumeAction.ID));
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case ACTIVE_LOG_FOLDER:
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case ARCHIVE_LOG_FOLDER:
			addActionToManager(manager, getAction(SpaceFolderViewAction.ID));
			break;
		case GENERIC_VOLUME:
			addActionToManager(manager, getAction(SpaceInfoViewAction.ID));
			break;
		case DATA_VOLUME:
			addActionToManager(manager, getAction(SpaceInfoViewAction.ID));
			break;
		case INDEX_VOLUME:
			addActionToManager(manager, getAction(SpaceInfoViewAction.ID));
			break;
		case TEMP_VOLUME:
			addActionToManager(manager, getAction(SpaceInfoViewAction.ID));
			break;
		case STORED_PROCEDURE_FOLDER:
			addActionToManager(manager, getAction(AddFunctionAction.ID));
			addActionToManager(manager, getAction(AddProcedureAction.ID));
			break;
		case STORED_PROCEDURE_FUNCTION_FOLDER:
			addActionToManager(manager, getAction(AddFunctionAction.ID));
			break;
		case STORED_PROCEDURE_FUNCTION:
			addActionToManager(manager, getAction(EditFunctionAction.ID));
			addActionToManager(manager, getAction(DeleteFunctionAction.ID));
			break;
		case STORED_PROCEDURE_PROCEDURE_FOLDER:
			addActionToManager(manager, getAction(AddProcedureAction.ID));
			break;
		case STORED_PROCEDURE_PROCEDURE:
			addActionToManager(manager, getAction(EditProcedureAction.ID));
			addActionToManager(manager, getAction(DeleteProcedureAction.ID));
			break;
		case TRIGGER_FOLDER: // trigger
			addActionToManager(manager, getAction(NewTriggerAction.ID));
			break;
		case TRIGGER: // trigger instance
			addActionToManager(manager, getAction(AlterTriggerAction.ID));
			addActionToManager(manager, getAction(DropTriggerAction.ID));
			break;
		case SERIAL_FOLDER:
			addActionToManager(manager, getAction(CreateSerialAction.ID));
			break;
		case SERIAL:
			addActionToManager(manager, getAction(EditSerialAction.ID));
			addActionToManager(manager, getAction(DeleteSerialAction.ID));
			break;
		case BROKER_FOLDER:
			addActionToManager(manager, getAction(ShowBrokerEnvStatusAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(StartBrokerEnvAction.ID));
			addActionToManager(manager, getAction(StopBrokerEnvAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(PropertyAction.ID));
			manager.add(new Separator());
			break;
		case BROKER:
			addActionToManager(manager, getAction(ShowBrokerStatusAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(StartBrokerAction.ID));
			addActionToManager(manager, getAction(StopBrokerAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(PropertyAction.ID));
			manager.add(new Separator());
			break;
		case STATUS_MONITOR_FOLDER:
			addActionToManager(manager,
					getAction(AddStatusMonitorTemplateAction.ID));
			break;
		case STATUS_MONITOR_TEMPLATE:
			addActionToManager(manager, getAction(ShowStatusMonitorAction.ID));
			manager.add(new Separator());
			addActionToManager(manager,
					getAction(EditStatusMonitorTemplateAction.ID));
			addActionToManager(manager,
					getAction(DeleteStatusMonitorTemplateAction.ID));
			break;
		case LOGS_BROKER_ACCESS_LOG_FOLDER:
			addActionToManager(manager, getAction(RemoveAllAccessLogAction.ID));
			break;
		case LOGS_BROKER_ACCESS_LOG:
			addActionToManager(manager, getAction(LogViewAction.ID));
			addActionToManager(manager, getAction(RemoveLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LogPropertyAction.ID));
			break;
		case LOGS_BROKER_ERROR_LOG_FOLDER:
			addActionToManager(manager, getAction(RemoveAllErrorLogAction.ID));
			break;
		case LOGS_BROKER_ERROR_LOG:
			addActionToManager(manager, getAction(LogViewAction.ID));
			addActionToManager(manager, getAction(RemoveLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LogPropertyAction.ID));
			break;
		case LOGS_BROKER_ADMIN_LOG:
			addActionToManager(manager, getAction(LogViewAction.ID));
			addActionToManager(manager, getAction(ResetAdminLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LogPropertyAction.ID));
			break;
		case BROKER_SQL_LOG_FOLDER:
			addActionToManager(manager, getAction(RemoveAllScriptLogAction.ID));
			addActionToManager(manager, getAction(AnalyzeSqlLogAction.ID));
			break;
		case BROKER_SQL_LOG:
			addActionToManager(manager, getAction(LogViewAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(AnalyzeSqlLogAction.ID));
			addActionToManager(manager, getAction(ExecuteSqlLogAction.ID));
			addActionToManager(manager, getAction(RemoveLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LogPropertyAction.ID));
			break;
		case LOGS_SERVER_DATABASE_LOG:
			addActionToManager(manager, getAction(LogViewAction.ID));
			addActionToManager(manager, getAction(RemoveLogAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(LogPropertyAction.ID));
			break;
		case LOGS_SERVER_DATABASE_FOLDER:
			addActionToManager(manager, getAction(RemoveAllDbLogAction.ID));
			break;
		case LOGS_MANAGER_ACCESS_LOG:
		case LOGS_MANAGER_ERROR_LOG:
			addActionToManager(manager, getAction(ManagerLogViewAction.ID));
			addActionToManager(manager, getAction(RemoveAllManagerLogAction.ID));
			break;
		case SYSTEM_TABLE:
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			manager.add(new Separator());
			break;
		case SYSTEM_VIEW:
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			addActionToManager(manager, getAction(PropertyViewAction.ID));
			manager.add(new Separator());
			break;
		case TABLE_FOLDER:
			addActionToManager(manager, getAction(NewTableAction.ID));
			break;
		case USER_PARTITIONED_TABLE_FOLDER: // partition table
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DeleteTableAction.ID));
			addActionToManager(manager, getAction(InsertTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(ImportTableAction.ID));
			addActionToManager(manager, getAction(ExportTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DropTableAction.ID));
			addActionToManager(manager, getAction(RenameTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(EditTableAction.ID));
			break;
		case USER_PARTITIONED_TABLE: // partition table/subtable
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(ExportTableAction.ID));
			break;
		case USER_TABLE:
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DeleteTableAction.ID));
			addActionToManager(manager, getAction(InsertTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(ImportTableAction.ID));
			addActionToManager(manager, getAction(ExportTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(DropTableAction.ID));
			addActionToManager(manager, getAction(RenameTableAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(EditTableAction.ID));
			break;
		case USER_VIEW: // User schema/View instance
			addActionToManager(manager, getAction(TableSelectAllAction.ID));
			addActionToManager(manager, getAction(TableSelectCountAction.ID));
			manager.add(new Separator());
			addActionToManager(manager, getAction(ExportTableAction.ID));
			manager.add(new Separator());
			IAction tableDropAction = getAction(DropTableAction.ID);
			addActionToManager(manager, tableDropAction);
			addActionToManager(manager, getAction(RenameTableAction.ID));
			addActionToManager(manager, getAction(EditViewAction.ID));

			break;
		case VIEW_FOLDER:
			addActionToManager(manager, getAction(CreateViewAction.ID));
			break;
		default:
		}
		manager.update(true);
	}

	public static void addActionToManager(IMenuManager manager, IAction action) {
		if (action != null) {
			manager.add(action);
		}
	}

	/**
	 * Notifies that the selection has changed.
	 */
	public void selectionChanged(SelectionChangedEvent event) {
		if (!(event.getSelection() instanceof IStructuredSelection)) {
			return;
		}
		changeActionMenu();
	}

	/**
	 * 
	 * Change action menu
	 * 
	 */
	private void changeActionMenu() {
		ApplicationWindow window = (ApplicationWindow) PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IMenuManager menuBarManager = window.getMenuBarManager();
		if (menuBarManager == null) {
			return;
		}
		IMenuManager actionMenuManager = menuBarManager.findMenuUsingPath(ActionManager.M_ACTION);
		if (actionMenuManager != null) {
			actionMenuManager.removeAll();
			setActionsMenu(actionMenuManager);
			menuBarManager.update(true);
		}
	}

}
