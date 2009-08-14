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
package com.cubrid.cubridmanager.plugin;

import org.eclipse.jface.action.ICoolBarManager;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.swt.SWT;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.IWorkbenchWindow;

import com.cubrid.cubridmanager.ui.common.action.AddHostAction;
import com.cubrid.cubridmanager.ui.common.action.ConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.DisConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.PropertyAction;
import com.cubrid.cubridmanager.ui.common.action.StartRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StartServiceAction;
import com.cubrid.cubridmanager.ui.common.action.StopRetargetAction;
import com.cubrid.cubridmanager.ui.common.action.StopServiceAction;
import com.cubrid.cubridmanager.ui.common.action.UserManagementAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.CreateDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LoginDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LogoutDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StartDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StopDatabaseAction;
import com.cubrid.cubridmanager.ui.cubrid.table.action.NewTableAction;
import com.cubrid.cubridmanager.ui.cubrid.user.action.AddUserAction;
import com.cubrid.cubridmanager.ui.query.action.QueryNewAction;
import com.cubrid.cubridmanager.ui.spi.ActionManager;

/**
 * 
 * This class is responsible to build CUBRID Manager menu and toolbar actions
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ActionBuilder {

	/**
	 * 
	 * Make all actions for CUBRID Manager menu and toolbar
	 * 
	 * @param window
	 */
	public void makeActions(IWorkbenchWindow window) {
		ActionManager.getInstance().makeActions(window);
	}

	/**
	 * Build CUBRID Manager Menu
	 * 
	 * @param parentMenu
	 * @return
	 */
	public IMenuManager[] buildMenu(IMenuManager parentMenu) {
		IMenuManager cubridMenuMgr = new MenuManager(Messages.CUBRID_MENU,
				ActionManager.M_CUBRID_MENU);
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				ConnectHostAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				DisConnectHostAction.ID));
		cubridMenuMgr.add(new Separator());
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				StartServiceAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				StopServiceAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				StartDatabaseAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				StopDatabaseAction.ID));
		cubridMenuMgr.add(new Separator());
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				LoginDatabaseAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				LogoutDatabaseAction.ID));
		cubridMenuMgr.add(new Separator());
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				AddHostAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				CreateDatabaseAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				QueryNewAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				AddUserAction.ID));
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				NewTableAction.ID));
		cubridMenuMgr.add(new Separator());
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				UserManagementAction.ID));
		cubridMenuMgr.add(new Separator());
		cubridMenuMgr.add(ActionManager.getInstance().getAction(
				PropertyAction.ID));
		parentMenu.insertBefore(IWorkbenchActionConstants.M_WINDOW,
				cubridMenuMgr);
		return new IMenuManager[] { cubridMenuMgr };
	}

	/**
	 * Build CUBRID Manager toolBar
	 * 
	 * @param parent
	 * @return
	 */
	public IToolBarManager[] buildToolBar(ICoolBarManager parent) {
		IToolBarManager newToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		newToolbarManager.add(ActionManager.getInstance().getAction(
				AddHostAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				CreateDatabaseAction.ID));
		newToolbarManager.add(ActionManager.getInstance().getAction(
				QueryNewAction.ID));
		parent.add(newToolbarManager);
		IToolBarManager statusToolbarManager = new ToolBarManager(SWT.FLAT
				| SWT.RIGHT);
		statusToolbarManager.add(ActionManager.getInstance().getAction(
				StartRetargetAction.ID));
		statusToolbarManager.add(ActionManager.getInstance().getAction(
				StopRetargetAction.ID));
		parent.add(statusToolbarManager);
		return new IToolBarManager[] { newToolbarManager, statusToolbarManager };
	}
}
