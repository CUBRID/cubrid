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

import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.jface.action.StatusLineManager;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.jface.window.ApplicationWindow;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IEditorReference;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IViewReference;
import org.eclipse.ui.IWorkbench;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.internal.WorkbenchWindow;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.common.action.ConnectHostAction;
import com.cubrid.cubridmanager.ui.common.control.CubridStatusLineContrItem;
import com.cubrid.cubridmanager.ui.common.navigator.CubridNavigatorView;
import com.cubrid.cubridmanager.ui.cubrid.database.action.LoginDatabaseAction;
import com.cubrid.cubridmanager.ui.logs.action.LogViewAction;
import com.cubrid.cubridmanager.ui.logs.action.ManagerLogViewAction;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.spi.action.ISelectionAction;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.event.ICubridNodeChangedListener;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * A layout manager is responsible for managing workbench part
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class LayoutManager implements
		ISelectionChangedListener,
		IDoubleClickListener,
		ICubridNodeChangedListener {
	private static final Logger logger = LogUtil.getLogger(LayoutManager.class);
	private static LayoutManager manager = new LayoutManager();
	private ISelectionProvider provider = null;
	private boolean isUseClickOnce = false;
	public static final String CUBRID_MANAGER_TITLE = "CUBRID Manager";

	/**
	 * The constructor
	 */
	private LayoutManager() {
	}

	/**
	 * 
	 * Get the only LayoutManager instance
	 * 
	 * @return
	 */
	public static LayoutManager getInstance() {
		return manager;
	}

	/**
	 * 
	 * Get whether use click once operaiton
	 * 
	 * @return
	 */
	public boolean isUseClickOnce() {
		return isUseClickOnce;
	}

	/**
	 * 
	 * Set whether use click once operation
	 * 
	 * @param isUseClickOnce
	 */
	public void setUseClickOnce(boolean isUseClickOnce) {
		this.isUseClickOnce = isUseClickOnce;
	}

	/**
	 * 
	 * Set current selected node
	 * 
	 * @param node
	 */
	public void setCurrentSelectedNode(ICubridNode node) {
		if (this.provider != null) {
			this.provider.setSelection(new StructuredSelection(node));
		}
	}

	/**
	 * 
	 * Get current selected CUBRID node
	 * 
	 * @return
	 */
	public ICubridNode getCurrentSelectedNode() {
		if (this.provider == null) {
			return null;
		}
		ISelection selection = this.provider.getSelection();
		if (selection == null || selection.isEmpty()) {
			return null;
		}
		Object obj = ((IStructuredSelection) selection).getFirstElement();
		if (obj == null || !(obj instanceof ICubridNode)) {
			return null;
		}
		return (ICubridNode) obj;
	}

	/**
	 * 
	 * Get SelectionProvider
	 * 
	 * @return
	 */
	public ISelectionProvider getSelectionProvider() {
		return this.provider;
	}

	/**
	 * Notifies that the selection has changed.
	 */
	public void selectionChanged(SelectionChangedEvent event) {
		if (this.provider == null) {
			return;
		}
		changeTitle(event.getSelection());
		checkConnectionStatus(event.getSelection());
		changeStuatusLine(event.getSelection());
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
		selectionChanged(event);
	}

	/**
	 * 
	 * Change CUBRID Manager title
	 * 
	 * @param selection
	 */
	private void changeTitle(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			return;
		}
		Object obj = ((IStructuredSelection) selection).getFirstElement();
		if (!(obj instanceof ICubridNode)) {
			return;
		}
		ICubridNode cubridNode = (ICubridNode) obj;
		String title = getTitle(cubridNode);
		Shell shell = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
		if (shell != null) {
			shell.setText(CUBRID_MANAGER_TITLE);
			if (title != null && title.trim().length() > 0) {
				shell.setText(CUBRID_MANAGER_TITLE + " - " + title);
			}
		}
	}

	/**
	 * 
	 * Get title of cubrid manager application
	 * 
	 * @param cubridNode
	 * @return
	 */
	public static String getTitle(ICubridNode cubridNode) {
		String title = "";
		if (cubridNode == null || cubridNode.getServer() == null) {
			return title;
		}
		CubridServer server = cubridNode.getServer();
		String serverTitle = server.getLabel();
		if (server.isConnected()) {
			ServerUserInfo userInfo = cubridNode.getServer().getServerInfo().getLoginedUserInfo();
			if (userInfo != null && userInfo.getUserName() != null
					&& userInfo.getUserName().trim().length() > 0) {
				serverTitle = userInfo.getUserName() + "@" + serverTitle;
			}
			String monPort = cubridNode.getServer().getMonPort();
			if (monPort != null && monPort.trim().length() > 0) {
				serverTitle = serverTitle + ":" + monPort;
			}
		}
		String databaseTitle = "";
		if (cubridNode instanceof ISchemaNode) {
			ISchemaNode schemaNode = (ISchemaNode) cubridNode;
			CubridDatabase database = schemaNode.getDatabase();
			databaseTitle += database.getLabel();
			if (database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (database.isLogined() && dbUserInfo != null
						&& dbUserInfo.getName() != null
						&& dbUserInfo.getName().trim().length() > 0) {
					databaseTitle = dbUserInfo.getName() + "@" + databaseTitle;
				}
				String brokerPort = QueryOptions.getBrokerPort(database.getDatabaseInfo());
				BrokerInfos brokerInfos = database.getServer().getServerInfo().getBrokerInfos();
				if (brokerInfos != null) {
					BrokerInfoList bis = brokerInfos.getBorkerInfoList();
					if (bis != null) {
						List<BrokerInfo> brokerInfoList = bis.getBrokerInfoList();
						boolean isExist = false;
						for (BrokerInfo brokerInfo : brokerInfoList) {
							if (brokerInfo.getPort() == null
									|| brokerInfo.getPort().trim().length() == 0
									|| !brokerPort.equals(brokerInfo.getPort())) {
								continue;
							}
							if (brokerPort.equals(brokerInfo.getPort())) {
								isExist = true;
								String status = "";
								if (brokerInfos.getBrokerstatus() == null
										|| !brokerInfos.getBrokerstatus().equalsIgnoreCase(
												"ON")) {
									status = "OFF";
								} else {
									status = brokerInfo.getState() == null
											|| brokerInfo.getState().trim().equalsIgnoreCase(
													"OFF") ? "OFF" : "ON";
								}
								String text = brokerInfo.getName() + "["
										+ brokerInfo.getPort() + "/" + status
										+ "]";
								databaseTitle += ":" + text;
								break;
							}
						}
						if (!isExist) {
							if (brokerPort != null
									&& brokerPort.trim().length() > 0) {
								databaseTitle += ":" + brokerPort;
							}
						}
					}
				} else if (brokerPort != null && brokerPort.trim().length() > 0) {
					databaseTitle += ":" + brokerPort;
				}
			}
		}
		if (serverTitle != null && serverTitle.trim().length() > 0) {
			title = serverTitle;
		}
		if (databaseTitle != null && databaseTitle.trim().length() > 0) {
			title = serverTitle + " / " + databaseTitle;
		}
		return title;
	}

	/**
	 * 
	 * When selection changed,check whether selected server is connected
	 * 
	 * @param selection
	 */
	private void checkConnectionStatus(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			return;
		}
		Object obj = ((IStructuredSelection) selection).getFirstElement();
		if (!(obj instanceof ICubridNode)) {
			return;
		}
		ICubridNode cubridNode = (ICubridNode) obj;
		CubridServer server = cubridNode.getServer();
		ServerInfo serverInfo = server.getServerInfo();
		if (!serverInfo.isConnected() && !(obj instanceof CubridServer)) {
			Shell shell = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
			CommonTool.openErrorBox(shell, Messages.errCannotConnectServer);
			LayoutManager.closeAllEditorAndViewInServer(server, false);
			server.removeAllChild();
			TreeViewer viewer = (TreeViewer) provider;
			viewer.refresh(server);
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent(server,
							CubridNodeChangedEventType.SERVER_DISCONNECTED));
		}
	}

	/**
	 * 
	 * When selection changed,change status line message
	 * 
	 * @param selection
	 */
	@SuppressWarnings("restriction")
	private void changeStuatusLine(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			return;
		}
		Object obj = ((IStructuredSelection) selection).getFirstElement();
		if (!(obj instanceof ICubridNode)) {
			return;
		}
		ICubridNode cubridNode = (ICubridNode) obj;
		ApplicationWindow window = (ApplicationWindow) PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		String nodePath = cubridNode.getLabel();
		ICubridNode parent = cubridNode.getParent();
		while (parent != null) {
			nodePath = parent.getLabel() + "/" + nodePath;
			parent = parent.getParent();
		}
		window.setStatus(nodePath);
		CubridServer server = cubridNode.getServer();
		ServerInfo serverInfo = server.getServerInfo();
		if (!serverInfo.isConnected()) {
			clearStatusLine();
			return;
		}
		if (window instanceof WorkbenchWindow) {
			StatusLineManager statusLineManager = ((WorkbenchWindow) window).getStatusLineManager();
			if (statusLineManager != null) {
				statusLineManager.remove(CubridStatusLineContrItem.ID);
				statusLineManager.add(new CubridStatusLineContrItem(cubridNode));
				statusLineManager.update(true);
			}
		}
	}

	/**
	 * 
	 * Clear the status line information of CUBRID Manager
	 * 
	 */
	@SuppressWarnings("restriction")
	public void clearStatusLine() {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		if (window instanceof WorkbenchWindow) {
			StatusLineManager statusLineManager = ((WorkbenchWindow) window).getStatusLineManager();
			if (statusLineManager != null) {
				statusLineManager.remove(CubridStatusLineContrItem.ID);
				statusLineManager.update(true);
			}
		}
	}

	/**
	 * 
	 * Change selection provider
	 * 
	 * @param provider
	 */
	public void changeSelectionProvider(ISelectionProvider provider) {
		if (provider != null) {
			if (this.provider != null) {
				this.provider.removeSelectionChangedListener(this);
			}
			this.provider = provider;
			this.provider.addSelectionChangedListener(this);
			changeStuatusLine(this.provider.getSelection());
		}
	}

	/**
	 * 
	 * Close all opened editor and view part
	 * 
	 */
	public static void closeAllEditorAndView() {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		window.getActivePage().closeAllEditors(true);
		IViewReference[] viewRef = window.getActivePage().getViewReferences();
		for (int i = 0, n = viewRef.length; i < n; i++) {
			window.getActivePage().hideView(viewRef[i]);
		}
	}

	/**
	 * 
	 * Close all opened editor and view part related with CUBRID Manager,not
	 * include query editor
	 * 
	 */
	public static void closeAllCubridEditorAndView() {
		IWorkbench workbench = PlatformUI.getWorkbench();
		if (workbench == null) {
			return;
		}
		IWorkbenchWindow window = workbench.getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return;
		}
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null && editorRefArr.length > 0) {
			for (IEditorReference editorRef : editorRefArr) {
				try {
					IEditorInput editorInput = editorRef.getEditorInput();
					if (editorInput instanceof ICubridNode) {
						window.getActivePage().closeEditor(
								editorRef.getEditor(false), true);
					}
				} catch (PartInitException e) {
					logger.error(e.getMessage());
				}
			}
		}
		IViewReference[] viewRefArr = page.getViewReferences();
		if (viewRefArr != null && viewRefArr.length > 0) {
			for (IViewReference viewRef : viewRefArr) {
				IViewPart viewPart = viewRef.getView(false);
				if (viewPart instanceof ICubridViewPart) {
					page.hideView(viewPart);
				}
			}
		}
	}

	/**
	 * 
	 * Close all editor and view part related with this CUBRID Manager server
	 * node,not include query editor
	 * 
	 * @param serverNode
	 * @param isSaved
	 */
	public static void closeAllEditorAndViewInServer(ICubridNode serverNode,
			boolean isSaved) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return;
		}
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null && editorRefArr.length > 0) {
			for (IEditorReference editorRef : editorRefArr) {
				try {
					IEditorInput editorInput = editorRef.getEditorInput();
					if (editorInput instanceof ICubridNode) {
						ICubridNode node = ((ICubridNode) editorInput).getServer();
						if (node.getId().equals(serverNode.getId())) {
							page.closeEditor(editorRef.getEditor(false),
									isSaved);
						}
					}
				} catch (PartInitException e) {
					logger.error(e.getMessage());
				}
			}
		}
		IViewReference[] viewRefArr = page.getViewReferences();
		if (viewRefArr != null && viewRefArr.length > 0) {
			for (IViewReference viewRef : viewRefArr) {
				IViewPart viewPart = viewRef.getView(false);
				if (viewPart instanceof CubridViewPart) {
					CubridViewPart cubridViewPart = (CubridViewPart) viewPart;
					ICubridNode cubridNode = cubridViewPart.getCubridNode();
					if (cubridNode == null) {
						continue;
					}
					ICubridNode cubridServerNode = cubridNode.getServer();
					if (cubridServerNode.getId().equals(serverNode.getId())) {
						page.hideView(viewPart);
					}
				}
			}
		}
	}

	/**
	 * 
	 * Close all editor and view part related with this CUBRID Manager database
	 * node,not include query editor
	 * 
	 * @param databaseNode
	 */
	public static void closeAllEditorAndViewInDatabase(
			CubridDatabase databaseNode) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return;
		}
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null) {
			for (IEditorReference editorRef : editorRefArr) {
				try {
					IEditorInput editorInput = editorRef.getEditorInput();
					if (editorInput instanceof ISchemaNode) {
						ISchemaNode node = ((ISchemaNode) editorInput).getDatabase();
						if (node.getId().equals(databaseNode.getId())) {
							page.closeEditor(editorRef.getEditor(false), true);
						}
					}
				} catch (PartInitException e) {
					logger.error(e.getMessage());
				}
			}
		}
		IViewReference[] viewRefArr = page.getViewReferences();
		if (viewRefArr != null && viewRefArr.length > 0) {
			for (IViewReference viewRef : viewRefArr) {
				IViewPart viewPart = viewRef.getView(false);
				if (viewPart instanceof CubridViewPart) {
					CubridViewPart cubridViewPart = (CubridViewPart) viewPart;
					ICubridNode cubridNode = cubridViewPart.getCubridNode();
					if (cubridNode == null) {
						continue;
					}
					CubridNodeType type = cubridNode.getType();
					boolean isDbSpaceEditor = (type == CubridNodeType.DATABASE
							|| type == CubridNodeType.DBSPACE_FOLDER
							|| type == CubridNodeType.GENERIC_VOLUME_FOLDER
							|| type == CubridNodeType.GENERIC_VOLUME
							|| type == CubridNodeType.DATA_VOLUME_FOLDER
							|| type == CubridNodeType.DATA_VOLUME
							|| type == CubridNodeType.INDEX_VOLUME_FOLDER
							|| type == CubridNodeType.INDEX_VOLUME
							|| type == CubridNodeType.TEMP_VOLUME_FOLDER
							|| type == CubridNodeType.TEMP_VOLUME
							|| type == CubridNodeType.LOG_VOLUEM_FOLDER
							|| type == CubridNodeType.ACTIVE_LOG_FOLDER
							|| type == CubridNodeType.ACTIVE_LOG
							|| type == CubridNodeType.ARCHIVE_LOG_FOLDER || type == CubridNodeType.ARCHIVE_LOG);
					if (databaseNode.isLogined() && isDbSpaceEditor) {
						continue;
					}
					if (cubridNode instanceof ISchemaNode) {
						ICubridNode cubridDatabaseNode = ((ISchemaNode) cubridNode).getDatabase();
						if (cubridDatabaseNode.getId().equals(
								databaseNode.getId())) {
							page.hideView(viewPart);
						}
					}

				}
			}
		}
	}

	/**
	 * Get navigator tree view part
	 * 
	 * @return
	 */
	public static CubridNavigatorView getNavigatorView() {
		return CubridNavigatorView.getNavigatorView();
	}

	/**
	 * Notifies of a double click.
	 * 
	 * @param event event object describing the double-click
	 */
	public void doubleClick(DoubleClickEvent event) {
		ISelection selection = event.getSelection();
		if (selection == null || selection.isEmpty()) {
			return;
		}
		Object obj = ((IStructuredSelection) selection).getFirstElement();
		if (!(obj instanceof ICubridNode)) {
			return;
		}
		ICubridNode cubridNode = (ICubridNode) obj;
		//popup up ConnectHostDialog
		if (cubridNode instanceof CubridServer) {
			CubridServer cubridServer = (CubridServer) cubridNode;
			ISelectionAction action = (ISelectionAction) ActionManager.getInstance().getAction(
					ConnectHostAction.ID);
			if (action != null && action.isSupported(cubridServer)) {
				action.run();
				return;
			}
		}
		CubridServer server = cubridNode.getServer();
		if (!server.isConnected()) {
			return;
		}
		//popup up LoginDatabaseDialog
		if (cubridNode instanceof CubridDatabase) {
			CubridDatabase database = (CubridDatabase) cubridNode;
			ISelectionAction action = (ISelectionAction) ActionManager.getInstance().getAction(
					LoginDatabaseAction.ID);
			if (action != null && action.isSupported(database)) {
				action.run();
				return;
			}
		}
		if (!isUseClickOnce) {
			openEditorOrView(cubridNode);
		}
	}

	/**
	 * 
	 * Get the editor part of this cubrid node
	 * 
	 * @param cubridNode
	 * @return
	 */
	public IEditorPart getEditorPart(ICubridNode cubridNode) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return null;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return null;
		}
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null) {
			for (IEditorReference editorRef : editorRefArr) {
				try {
					IEditorInput editorInput = editorRef.getEditorInput();
					if (editorInput instanceof ICubridNode) {
						ICubridNode node = (ICubridNode) editorInput;
						if (node.getId().equals(cubridNode.getId())) {
							return editorRef.getEditor(false);
						}
					}
				} catch (PartInitException e) {
					logger.error(e.getMessage());
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * Get the view part of this cubrid node
	 * 
	 * @param cubridNode
	 * @return
	 */
	public IViewPart getViewPart(ICubridNode cubridNode) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return null;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return null;
		}
		IViewReference[] viewRefArr = page.getViewReferences();
		if (viewRefArr != null && viewRefArr.length > 0) {
			for (IViewReference viewRef : viewRefArr) {
				IViewPart viewPart = viewRef.getView(false);
				if (viewPart instanceof CubridViewPart) {
					CubridViewPart cubridViewPart = (CubridViewPart) viewPart;
					ICubridNode node = cubridViewPart.getCubridNode();
					if (node != null && node.getId().equals(cubridNode.getId())) {
						return viewPart;
					}
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * Open the editor or view part of this cubrid node
	 * 
	 * @param cubridNode
	 */
	public void openEditorOrView(ICubridNode cubridNode) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		if (cubridNode instanceof ISchemaNode) {
			ISchemaNode schemaNode = (ISchemaNode) cubridNode;
			if (schemaNode.getDatabase() != null
					&& !schemaNode.getDatabase().isLogined()) {
				return;
			}
		}
		//close the editor part that has been open
		String editorId = cubridNode.getEditorId();
		String viewId = cubridNode.getViewId();
		if (editorId != null && editorId.trim().length() > 0) {
			IEditorPart editorPart = getEditorPart(cubridNode);
			if (editorPart != null) {
				window.getActivePage().closeEditor(editorPart, false);
			}
		} else if (viewId != null && viewId.trim().length() > 0) {
			IViewPart viewPart = getViewPart(cubridNode);
			if (viewPart != null) {
				window.getActivePage().hideView(viewPart);
			}
		}
		CubridNodeType nodeType = cubridNode.getType();
		ISelectionAction logViewAction = null;
		switch (nodeType) {
		case BROKER_SQL_LOG:
		case LOGS_BROKER_ACCESS_LOG:
		case LOGS_BROKER_ERROR_LOG:
		case LOGS_BROKER_ADMIN_LOG:
		case LOGS_SERVER_DATABASE_LOG:
			logViewAction = (ISelectionAction) ActionManager.getInstance().getAction(
					LogViewAction.ID);
			((LogViewAction) logViewAction).setCubridNode(cubridNode);
			break;
		case LOGS_MANAGER_ACCESS_LOG:
		case LOGS_MANAGER_ERROR_LOG:
			logViewAction = (ISelectionAction) ActionManager.getInstance().getAction(
					ManagerLogViewAction.ID);
			((ManagerLogViewAction) logViewAction).setCubridNode(cubridNode);
			break;
		}
		if (logViewAction != null && logViewAction.isSupported(cubridNode)) {
			logViewAction.run();
			return;
		}
		if (editorId != null && editorId.trim().length() > 0) {
			try {
				window.getActivePage().openEditor(cubridNode, editorId);
			} catch (PartInitException e) {
				logger.error(e.getMessage());
			}
		} else if (viewId != null && viewId.trim().length() > 0) {
			try {
				window.getActivePage().showView(viewId);
			} catch (PartInitException e) {
				logger.error(e.getMessage());
			}
		} else {

		}
	}

	/**
	 * When refresh container node,check the viewpart and the editorPart of it's
	 * children,if the children are deleted,close it,or refresh it
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode eventNode = e.getCubridNode();
		if (eventNode == null
				|| e.getType() != CubridNodeChangedEventType.CONTAINER_NODE_REFRESH) {
			return;
		}
		IWorkbench workbench = PlatformUI.getWorkbench();
		if (workbench == null) {
			return;
		}
		IWorkbenchWindow window = workbench.getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return;
		}
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null && editorRefArr.length > 0) {
			for (IEditorReference editorRef : editorRefArr) {
				try {
					IEditorInput editorInput = editorRef.getEditorInput();
					if (editorInput instanceof ICubridNode) {
						ICubridNode editorNode = (ICubridNode) editorInput;
						ICubridNode parentNode = editorNode.getParent();
						if (editorNode != null
								&& parentNode != null
								&& parentNode.getId().equals(eventNode.getId())
								&& eventNode.getChild(editorNode.getId()) == null) {
							window.getActivePage().closeEditor(
									editorRef.getEditor(false), true);
						}
					}
				} catch (PartInitException e1) {
					logger.error(e1.getMessage());
				}
			}
		}
		IViewReference[] viewRefArr = page.getViewReferences();
		if (viewRefArr != null && viewRefArr.length > 0) {
			for (IViewReference viewRef : viewRefArr) {
				IViewPart viewPart = viewRef.getView(false);
				if (viewPart instanceof CubridViewPart) {
					ICubridNode viewPartNode = ((CubridViewPart) viewPart).getCubridNode();
					if (viewPartNode == null) {
						continue;
					}
					ICubridNode parentNode = viewPartNode.getParent();
					if (viewPartNode != null && parentNode != null
							&& parentNode.getId().equals(eventNode.getId())
							&& eventNode.getChild(viewPartNode.getId()) == null) {
						page.hideView(viewPart);
					}
				}
			}
		}
	}

	/**
	 * 
	 * When database logout or stop,check query editor whether some transaction
	 * are not commit
	 * 
	 * @param databaseNode
	 * @return
	 */
	public static boolean checkAllQueryEditor(CubridDatabase databaseNode) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return true;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return true;
		}
		boolean isContinue = true;
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null) {
			for (IEditorReference editorRef : editorRefArr) {
				String editorId = editorRef.getId();
				if (editorId != null && editorId.equals(QueryEditorPart.ID)) {
					QueryEditorPart queryEditor = (QueryEditorPart) editorRef.getEditor(false);
					CubridDatabase db = queryEditor.getSelectedDatabase();
					if (db != null && db.getId().equals(databaseNode.getId())) {
						isContinue = queryEditor.resetJDBCConnection();
					}
				}
			}
		}
		return isContinue;
	}

	/**
	 * 
	 * When server disconnect or delete,check query editor whether some
	 * transaction are not commit
	 * 
	 * @param cubridServer
	 * @return
	 */
	public static boolean checkAllQueryEditor(CubridServer cubridServer) {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return true;
		}
		IWorkbenchPage page = window.getActivePage();
		if (page == null) {
			return true;
		}
		boolean isContinue = true;
		IEditorReference[] editorRefArr = page.getEditorReferences();
		if (editorRefArr != null) {
			for (IEditorReference editorRef : editorRefArr) {
				String editorId = editorRef.getId();
				if (editorId != null && editorId.equals(QueryEditorPart.ID)) {
					QueryEditorPart queryEditor = (QueryEditorPart) editorRef.getEditor(false);
					CubridDatabase db = queryEditor.getSelectedDatabase();
					if (db != null
							&& db.getServer() != null
							&& db.getServer().getId().equals(
									cubridServer.getId())) {
						isContinue = queryEditor.resetJDBCConnection();
					}
				}
			}
		}
		return isContinue;
	}
}
