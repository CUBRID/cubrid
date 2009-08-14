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

package com.cubrid.cubridmanager.app;

import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.ICoolBarManager;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.action.ToolBarContributionItem;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.swt.SWT;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.actions.ActionFactory.IWorkbenchAction;
import org.eclipse.ui.application.ActionBarAdvisor;
import org.eclipse.ui.application.IActionBarConfigurer;

import com.cubrid.cubridmanager.ui.broker.action.StartBrokerEnvAction;
import com.cubrid.cubridmanager.ui.broker.action.StopBrokerEnvAction;
import com.cubrid.cubridmanager.ui.common.action.AboutAction;
import com.cubrid.cubridmanager.ui.common.action.AddHostAction;
import com.cubrid.cubridmanager.ui.common.action.ChangePasswordAction;
import com.cubrid.cubridmanager.ui.common.action.CheckNewVersionAction;
import com.cubrid.cubridmanager.ui.common.action.ConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.CubridOnlineForumAction;
import com.cubrid.cubridmanager.ui.common.action.CubridProjectSiteAction;
import com.cubrid.cubridmanager.ui.common.action.DeleteHostAction;
import com.cubrid.cubridmanager.ui.common.action.DisConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.OpenPreferenceAction;
import com.cubrid.cubridmanager.ui.common.action.QuitAction;
import com.cubrid.cubridmanager.ui.common.action.RefreshAction;
import com.cubrid.cubridmanager.ui.common.action.StartRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StartServiceAction;
import com.cubrid.cubridmanager.ui.common.action.StopRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StopServiceAction;
import com.cubrid.cubridmanager.ui.common.action.UserManagementAction;
import com.cubrid.cubridmanager.ui.common.action.ViewServerVersionAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.BackupDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CheckDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CreateDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LoadDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LockInfoAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.OptimizeAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.RestoreDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StartDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StopDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.TransactionInfoAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.UnloadDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.NewTableAction;
import com.cubrid.cubridmanager.ui.cubrid.user.action.AddUserAction;
import com.cubrid.cubridmanager.ui.query.action.QueryNewAction;
import com.cubrid.cubridmanager.ui.spi.ActionManager;

/**
 * An action bar advisor is responsible for creating, adding, and disposing of
 * the actions added to a workbench window. Each window will be populated with
 * new actions.
 */
public class ApplicationActionBarAdvisor extends
		ActionBarAdvisor {
	// standard actions provided by the workbench
	private IWorkbenchAction helpAction = null;
	private IWorkbenchAction dynamichelpAction = null;
	private IWorkbenchAction searchHelpAction = null;
	private IWorkbenchAction saveAction = null;
	private IWorkbenchAction saveasAction = null;
	private IWorkbenchAction saveAllAction = null;
	private IWorkbenchAction closeAction = null;
	private IWorkbenchAction closeAllAction = null;

	// retarget actions provided by the workbench
	private IWorkbenchAction undoRetargetAction = null;
	private IWorkbenchAction redoRetargetAction = null;
	private IWorkbenchAction copyRetargetAction = null;
	private IWorkbenchAction pasteRetargetAction = null;
	private IWorkbenchAction cutRetargetAction = null;
	private IWorkbenchAction findRetargetAction = null;

	// customized action for CUBRID Manager
	// common actions
	private IAction preferenceAction = null;
	private IAction quitAction = null;
	private IAction checkNewVersionAction = null;
	private IAction cubridOnlineForumAction = null;
	private IAction cubridProjectSiteAction = null;
	private IAction aboutAction = null;

	/**
	 * The constructor
	 * 
	 * @param configurer
	 */
	public ApplicationActionBarAdvisor(IActionBarConfigurer configurer) {
		super(configurer);
	}

	@Override
	protected void makeActions(IWorkbenchWindow window) {
		// standard actions provided by the workbench
		helpAction = ActionFactory.HELP_CONTENTS.create(window);
		helpAction.setText(Messages.helpActionName);
		register(helpAction);
		searchHelpAction = ActionFactory.HELP_SEARCH.create(window);
		searchHelpAction.setText(Messages.searchActionName);
		register(searchHelpAction);
		dynamichelpAction = ActionFactory.DYNAMIC_HELP.create(window);
		dynamichelpAction.setText(Messages.dynamicHelpActionName);
		register(dynamichelpAction);
		closeAction = ActionFactory.CLOSE.create(window);
		closeAction.setText(Messages.closeActionName);
		register(closeAction);
		closeAllAction = ActionFactory.CLOSE_ALL.create(window);
		closeAllAction.setText(Messages.closeAllActionName);
		register(closeAllAction);
		saveAction = ActionFactory.SAVE.create(window);
		saveAction.setText(Messages.saveActionName);
		register(saveAction);
		saveasAction = ActionFactory.SAVE_AS.create(window);
		saveasAction.setText(Messages.saveAsActionName);
		register(saveasAction);
		saveAllAction = ActionFactory.SAVE_ALL.create(window);
		saveAllAction.setText(Messages.saveAllActionName);
		register(saveAllAction);

		// retarget actions provided by the workbench
		undoRetargetAction = ActionFactory.UNDO.create(window);
		undoRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.undoActionName);
		register(undoRetargetAction);
		redoRetargetAction = ActionFactory.REDO.create(window);
		redoRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.redoActionName);
		register(redoRetargetAction);
		copyRetargetAction = ActionFactory.COPY.create(window);
		copyRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.copyActionName);
		register(copyRetargetAction);
		cutRetargetAction = ActionFactory.CUT.create(window);
		cutRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.cutActionName);
		register(cutRetargetAction);
		pasteRetargetAction = ActionFactory.PASTE.create(window);
		pasteRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.pasteActionName);
		register(pasteRetargetAction);
		findRetargetAction = ActionFactory.FIND.create(window);
		findRetargetAction.setText(com.cubrid.cubridmanager.ui.spi.Messages.findReplaceActionName);
		register(findRetargetAction);

		// customized actions for CUBRID Manager
		//common action
		preferenceAction = new OpenPreferenceAction(window.getShell(),
				Messages.openPreferenceActionName, null);
		register(preferenceAction);
		ActionManager.getInstance().registerAction(preferenceAction);
		quitAction = new QuitAction(Messages.exitActionName);
		if (UrlConnUtil.isExistNewCubridVersion()) {
			checkNewVersionAction = new CheckNewVersionAction(
					Messages.checkNewVersionActionName,
					CubridManagerAppPlugin.getImageDescriptor("icons/new_version.png"));
		} else {
			checkNewVersionAction = new CheckNewVersionAction(
					Messages.checkNewVersionActionName, null);
		}
		cubridOnlineForumAction = new CubridOnlineForumAction(
				Messages.cubridOnlineForumActionName);
		cubridProjectSiteAction = new CubridProjectSiteAction(
				Messages.cubridProjectSiteActionName);
		aboutAction = new AboutAction(Messages.aboutActionName);
		ActionManager.getInstance().makeActions(window);
	}

	@Override
	protected void fillMenuBar(IMenuManager menuBar) {
		MenuManager fileMenu = new MenuManager(Messages.mnu_fileMenuName,
				IWorkbenchActionConstants.M_FILE);
		MenuManager editMenu = new MenuManager(Messages.mnu_editMenuName,
				IWorkbenchActionConstants.M_EDIT);
		MenuManager toolsMenu = new MenuManager(Messages.mnu_toolsMenuName,
				ActionManager.M_TOOLS);
		MenuManager actionMenu = new MenuManager(Messages.mnu_actionMenuName,
				ActionManager.M_ACTION);
		MenuManager helpMenu = new MenuManager(Messages.mnu_helpMneuName,
				IWorkbenchActionConstants.M_HELP);
		// fill in file menu
		fileMenu.add(ActionManager.getInstance().getAction(AddHostAction.ID));
		fileMenu.add(ActionManager.getInstance().getAction(DeleteHostAction.ID));
		fileMenu.add(ActionManager.getInstance().getAction(ConnectHostAction.ID));
		fileMenu.add(ActionManager.getInstance().getAction(
				DisConnectHostAction.ID));
		fileMenu.add(new Separator());
		fileMenu.add(closeAction);
		fileMenu.add(closeAllAction);
		fileMenu.add(new Separator());
		fileMenu.add(saveAction);
		fileMenu.add(saveasAction);
		fileMenu.add(saveAllAction);
		fileMenu.add(new Separator());
		fileMenu.add(preferenceAction);
		fileMenu.add(ActionManager.getInstance().getAction(RefreshAction.ID));
		fileMenu.add(new Separator());
		fileMenu.add(quitAction);
		fileMenu.add(new Separator());
		menuBar.add(fileMenu);
		// fill in edit menu
		editMenu.add(undoRetargetAction);
		editMenu.add(redoRetargetAction);
		editMenu.add(new Separator());
		editMenu.add(copyRetargetAction);
		editMenu.add(cutRetargetAction);
		editMenu.add(pasteRetargetAction);
		editMenu.add(new Separator());
		editMenu.add(findRetargetAction);
		menuBar.add(editMenu);
		// fill in the run menu
		toolsMenu.add(ActionManager.getInstance().getAction(QueryNewAction.ID));
		toolsMenu.add(new Separator());
		toolsMenu.add(ActionManager.getInstance().getAction(
				StartServiceAction.ID));
		toolsMenu.add(ActionManager.getInstance().getAction(
				StopServiceAction.ID));
		toolsMenu.add(new Separator());
		toolsMenu.add(ActionManager.getInstance().getAction(
				StartDatabaseAction.ID));
		toolsMenu.add(ActionManager.getInstance().getAction(
				StopDatabaseAction.ID));
		toolsMenu.add(new Separator());
		toolsMenu.add(ActionManager.getInstance().getAction(
				StartBrokerEnvAction.ID));
		toolsMenu.add(ActionManager.getInstance().getAction(
				StopBrokerEnvAction.ID));
		toolsMenu.add(new Separator());
		toolsMenu.add(ActionManager.getInstance().getAction(
				ChangePasswordAction.ID));
		toolsMenu.add(ActionManager.getInstance().getAction(
				UserManagementAction.ID));
		menuBar.add(toolsMenu);
		// fill in action menu
		menuBar.add(actionMenu);

		// fill in help menu
		helpMenu.add(helpAction);
		helpMenu.add(new Separator());
		helpMenu.add(searchHelpAction);
		helpMenu.add(dynamichelpAction);
		helpMenu.add(new Separator());
		helpMenu.add(checkNewVersionAction);
		helpMenu.add(cubridOnlineForumAction);
		helpMenu.add(cubridProjectSiteAction);
		helpMenu.add(new Separator());
		helpMenu.add(ActionManager.getInstance().getAction(
				ViewServerVersionAction.ID));
		helpMenu.add(aboutAction);
		menuBar.add(helpMenu);
	}

	@Override
	protected void fillCoolBar(ICoolBarManager coolBar) {
		//add new related action toolbar
		IToolBarManager newToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(newToolbarManager,
				"cubridManager_new_toolbar"));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				AddHostAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				CreateDatabaseAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				AddUserAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				NewTableAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				QueryNewAction.ID));
		newToolbarManager.add(new Separator());
		//add start and stop action toolbar
		IToolBarManager statusToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(statusToolbarManager,
				"cubridManager_status_toolbar"));
		statusToolbarManager.add(ActionManager.getInstance().getAction(
				RefreshAction.ID));
		statusToolbarManager.add(ActionManager.getInstance().getAction(
				StartRetargetAction.ID));
		statusToolbarManager.add(ActionManager.getInstance().getAction(
				StopRetargetAction.ID));
		statusToolbarManager.add(new Separator());
		//add database related action toolbar
		IToolBarManager dbToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(dbToolbarManager,
				"cubridManager_database_toolbar"));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				UnloadDatabaseAction.ID));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				LoadDatabaseAction.ID));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				BackupDatabaseAction.ID));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				RestoreDatabaseAction.ID));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				OptimizeAction.ID));
		dbToolbarManager.add(ActionManager.getInstance().getAction(
				CheckDatabaseAction.ID));
		dbToolbarManager.add(new Separator());
		//add database related action toolbar
		IToolBarManager dbToolbarManager2 = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(dbToolbarManager2,
				"cubridManager_database2_toolbar"));
		dbToolbarManager2.add(ActionManager.getInstance().getAction(
				TransactionInfoAction.ID));
		dbToolbarManager2.add(ActionManager.getInstance().getAction(
				LockInfoAction.ID));
		dbToolbarManager2.add(new Separator());
		//add server related action toolbar
		IToolBarManager serverToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(serverToolbarManager,
				"cubridManager_server_toolbar"));
		serverToolbarManager.add(ActionManager.getInstance().getAction(
				ViewServerVersionAction.ID));
	}

}
