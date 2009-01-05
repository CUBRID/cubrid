/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.cas.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TreeItem;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.ITreeViewerListener;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.TreeExpansionEvent;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.action.LogViewAction;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.OnlyQueryEditor;
import cubridmanager.TreeObject;
import cubridmanager.TreeParent;
import cubridmanager.ViewContentProvider;
import cubridmanager.ViewLabelProvider;
import cubridmanager.WorkView;

public class CASView extends ViewPart {
	public static final String ID = "navigator.cas";
	public static String Current_broker = new String("");
	public static String Current_select = BrokerList.ID;
	public static CASView myNavi = null;
	public static TreeViewer viewer = null;
	private static TreeParent root = null;
	private static TreeObject oldobj = null;

	public CASView() {
		myNavi = this;
	}

	/**
	 * We will set up a dummy model to initialize tree heararchy. In real code,
	 * you will connect to a real model and expose its hierarchy.
	 */
	public TreeObject createModel() {
		if (root == null)
			root = new TreeParent(null, "root", null, null);

		TreeObject[] rootchild = root.getChildren();
		if (MainRegistry.IsConnected) {
			if (!MainRegistry.IsCASinfoReady) {
				Shell psh = viewer.getControl().getShell();
				ClientSocket cs = new ClientSocket();
				if (!cs.SendClientMessage(psh, "", "getbrokersinfo")) {
					CommonTool.ErrorBox(psh, cs.ErrorMsg);
					MainRegistry.IsConnected = false;
					return null;
				}
				ClientSocket cs2 = new ClientSocket();
				if (!cs2.SendClientMessage(psh, "", "getadminloginfo")) {
					CommonTool.ErrorBox(psh, cs2.ErrorMsg);
					MainRegistry.IsConnected = false;
					return null;
				}
				MainRegistry.IsCASinfoReady = true;
			}
			TreeParent host1;
			if (rootchild.length <= 0) {
				host1 = new TreeParent(BrokerList.ID, MainRegistry.HostDesc,
						"icons/cas.png", BrokerList.ID);
			} else
				host1 = (TreeParent) rootchild[0];
			TreeObject[] hostchild = host1.getChildren();
			if (MainRegistry.CASAuth != MainConstants.AUTH_NONE) {
				boolean addflag = false;
				TreeParent brokers = (TreeParent) TreeObject.FindID(hostchild,
						null, BrokerStatus.ID);
				if (brokers == null) {
					brokers = new TreeParent(BrokerStatus.ID, Messages
							.getString("TREE.BROKER"), "icons/broker.png",
							BrokerStatus.ID);
					addflag = true;
				}

				TreeObject[] brkchild = brokers.getChildren();
				boolean[] chk_brkchild = new boolean[brkchild.length];
				TreeObject.FindReset(chk_brkchild);
				CASItem casrec;
				for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
					casrec = (CASItem) MainRegistry.CASinfo.get(i);
					TreeParent broker1 = null;
					for (int ci = 0, cn = brkchild.length; ci < cn; ci++) {
						if (brkchild[ci].getName().equals(casrec.broker_name)) {
							broker1 = (TreeParent) brkchild[ci];
							broker1
									.setImage((casrec.status == MainConstants.STATUS_START) ? "icons/broker_start.png"
											: (casrec.status == MainConstants.STATUS_WAIT) ? "icons/broker_suspend.png"
													: "icons/broker_stop.png");
							chk_brkchild[ci] = true;
						}
					}
					if (broker1 == null)
						broker1 = new TreeParent(
								BrokerJob.ID,
								casrec.broker_name,
								(casrec.status == MainConstants.STATUS_START) ? "icons/broker_start.png"
										: (casrec.status == MainConstants.STATUS_WAIT) ? "icons/broker_suspend.png"
												: "icons/broker_stop.png",
								BrokerJob.ID);

					TreeObject[] bchild = broker1.getChildren();
					TreeParent logs = (TreeParent) TreeObject.FindID(bchild,
							null, CASLogs.LOGS_ALL);
					TreeParent log_access;
					TreeParent log_error;
					TreeParent log_script;
					boolean logs_add = false;
					if (logs == null) {
						logs = new TreeParent(CASLogs.LOGS_ALL, Messages
								.getString("TREE.LOGS"), "icons/logs.png", null);
						log_access = new TreeParent(CASLogs.LOGS_ACCESS,
								Messages.getString("TREE.ACCESS"),
								"icons/cas_access_log.png", null);
						log_error = new TreeParent(CASLogs.LOGS_ERROR, Messages
								.getString("TREE.ERROR"),
								"icons/cas_err_log.png", null);
						log_script = new TreeParent(CASLogs.LOGS_SCRIPT,
								Messages.getString("TREE.SCRIPT"),
								"icons/cas_script_log.png", null);
						logs_add = true;
					} else {
						TreeObject[] logcld = logs.getChildren();
						log_access = (TreeParent) TreeObject.FindID(logcld,
								null, CASLogs.LOGS_ACCESS);
						log_error = (TreeParent) TreeObject.FindID(logcld,
								null, CASLogs.LOGS_ERROR);
						log_script = (TreeParent) TreeObject.FindID(logcld,
								null, CASLogs.LOGS_SCRIPT);
					}
					TreeObject[] acctree = log_access.getChildren();
					boolean[] accchk = new boolean[acctree.length];
					TreeObject.FindReset(accchk);
					TreeObject[] errtree = log_error.getChildren();
					boolean[] errchk = new boolean[errtree.length];
					TreeObject.FindReset(errchk);
					TreeObject[] scrtree = log_script.getChildren();
					boolean[] scrchk = new boolean[scrtree.length];
					TreeObject.FindReset(scrchk);
					ArrayList loginfo = LogFileInfo
							.BrokerLog_get(casrec.broker_name);
					LogFileInfo log;
					for (int ai = 0, an = loginfo.size(); ai < an; ai++) {
						log = (LogFileInfo) loginfo.get(ai);
						if (log.type.equals("access")) {
							TreeObject obj = TreeObject.FindName(acctree,
									accchk, log.filename);
							if (obj == null)
								log_access.addChild(new TreeObject(
										CASLogs.LOGSOBJ, log.filename,
										"icons/cas_access_log_obj.png",
										CASLogs.ID));
						}
						if (log.type.equals("error")) {
							TreeObject obj = TreeObject.FindName(errtree,
									errchk, log.filename);
							if (obj == null)
								log_error
										.addChild(new TreeObject(
												CASLogs.LOGSOBJ, log.filename,
												"icons/cas_err_log_obj.png",
												CASLogs.ID));
						}
						if (log.type.equals("script")) {
							TreeObject obj = TreeObject.FindName(scrtree,
									scrchk, log.filename);
							if (obj == null)
								log_script.addChild(new TreeObject(
										CASLogs.SCRIPTOBJ, log.filename,
										"icons/cas_script_log_obj.png",
										CASLogs.ID));
						}
					}
					TreeObject.FindRemove(log_access, acctree, accchk);
					TreeObject.FindRemove(log_error, errtree, errchk);
					TreeObject.FindRemove(log_script, scrtree, scrchk);

					if (logs_add) {
						logs.addChild(log_access);
						logs.addChild(log_error);
						logs.addChild(log_script);
						broker1.addChild(logs);
					}

					if (TreeObject.FindName(brkchild, null, casrec.broker_name) == null)
						brokers.addChild(broker1);
				} // end for
				TreeObject.FindRemove(brokers, brkchild, chk_brkchild);
				if (addflag)
					host1.addChild(brokers);

				TreeParent admin_log;
				if (addflag)
					admin_log = new TreeParent(CASLogs.LOGS_ADMIN, Messages
							.getString("TREE.ADMINLOG"),
							"icons/cas_adminlog.png", null);
				else
					admin_log = (TreeParent) TreeObject.FindID(hostchild, null,
							CASLogs.LOGS_ADMIN);
				TreeObject[] admtree = admin_log.getChildren();
				boolean[] admchk = new boolean[admtree.length];
				TreeObject.FindReset(admchk);
				ArrayList loginfo = MainRegistry.CASadminlog;
				LogFileInfo log;
				for (int ai = 0, an = loginfo.size(); ai < an; ai++) {
					log = (LogFileInfo) loginfo.get(ai);
					TreeObject obj = TreeObject.FindName(admtree, admchk,
							log.filename);
					if (obj == null)
						admin_log.addChild(new TreeObject(CASLogs.ADMINOBJ,
								log.filename, "icons/cas_adminlog_obj.png",
								CASLogs.ID));
				}
				TreeObject.FindRemove(admin_log, admtree, admchk);
				if (addflag)
					host1.addChild(admin_log);
			} else {
				TreeObject.FindClear(host1, hostchild);
			}
			if (rootchild.length <= 0)
				root.addChild(host1);
		} // end isconnected
		else {
			if (rootchild.length > 0)
				root.removeChild(rootchild[0]);
		}
		return root;
	}

	/**
	 * This is a callback that will allow us to create the viewer and initialize
	 * it.
	 */
	public void createPartControl(Composite parent) {
		viewer = new TreeViewer(parent, SWT.SINGLE | SWT.H_SCROLL
				| SWT.V_SCROLL | SWT.BORDER);
		viewer.setContentProvider(new ViewContentProvider());
		viewer.setLabelProvider(new ViewLabelProvider());
		viewer.setAutoExpandLevel(3);
		viewer.addSelectionChangedListener(new ISelectionChangedListener() {
			public void selectionChanged(SelectionChangedEvent event) {
				if (OnlyQueryEditor.connectOldServer)
					return;

				String selbr = "";
				if (event.getSelection().isEmpty()) {
					return;
				}
				if (event.getSelection() instanceof IStructuredSelection) {
					IStructuredSelection selection = (IStructuredSelection) event
							.getSelection();
					TreeObject selobj = (TreeObject) selection
							.getFirstElement();
					Current_select = selobj.getID();
					if (selobj.getID().equals(BrokerList.ID)
							|| selobj.getID().equals(BrokerStatus.ID)
							|| selobj.getID().equals(CASLogs.LOGS_ADMIN)
							|| selobj.getParent().getID().equals(
									CASLogs.LOGS_ADMIN))
						selbr = "";
					else if (selobj.getID().equals(BrokerJob.ID))
						selbr = selobj.toString();
					else if (selobj.getID().equals(CASLogs.LOGS_ALL))
						selbr = selobj.getParent().toString();
					else if (selobj.getID().equals(CASLogs.LOGS_ACCESS)
							|| selobj.getID().equals(CASLogs.LOGS_ERROR)
							|| selobj.getID().equals(CASLogs.LOGS_SCRIPT))
						selbr = selobj.getParent().getParent().toString();
					else if (selobj.getParent().getID().equals(
							CASLogs.LOGS_ACCESS)
							|| selobj.getParent().getID().equals(
									CASLogs.LOGS_ERROR)
							|| selobj.getParent().getID().equals(
									CASLogs.LOGS_SCRIPT))
						selbr = selobj.getParent().getParent().getParent()
								.toString();
					else
						selbr = "";

					if (oldobj != null && selobj.equals(oldobj))
						return;

					oldobj = null;

					if (!Current_broker.equals(selbr)) {
						Current_broker = selbr;
						GetBrokerInfo(Current_broker);
						ApplicationActionBarAdvisor
								.AdjustToolbar(MainConstants.NAVI_CAS);
					}
					if (Current_select.equals(CASLogs.ADMINOBJ)
							|| Current_select.equals(CASLogs.LOGSOBJ)
							|| Current_select.equals(CASLogs.SCRIPTOBJ)) {
						CASLogs.CurrentObj = selobj.getName();
						if (Current_select.equals(CASLogs.ADMINOBJ))
							LogViewAction.viewlist = MainRegistry.CASadminlog;
						else
							LogViewAction.viewlist = LogFileInfo
									.BrokerLog_get(Current_broker);
						LogViewAction.viewitem = selobj.getName();
					}
					oldobj = selobj;
					ApplicationActionBarAdvisor
							.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
					if (selobj.getViewID() != null)
						WorkView.SetView(selobj.getViewID(), selobj.toString(),
								selobj.getID());
				}
			}
		});

		viewer.addDoubleClickListener(new IDoubleClickListener() {
			public void doubleClick(DoubleClickEvent event) {
				Object obj = ((IStructuredSelection) event.getSelection())
						.getFirstElement();
				if (obj instanceof TreeParent) {
					// viewer.setExpandedState(obj,(viewer.getExpandedState(obj))
					// ? false : true);
				} else {
					TreeObject tobj = (TreeObject) obj;
					if (tobj.getID().equals(CASLogs.ADMINOBJ)
							|| tobj.getID().equals(CASLogs.LOGSOBJ)
							|| tobj.getID().equals(CASLogs.SCRIPTOBJ)) {
						ApplicationActionBarAdvisor.logViewAction.run();
					}
				}
			}
		});

		viewer.addTreeListener(new ITreeViewerListener() {
			public void treeExpanded(TreeExpansionEvent e) {
				TreeObject tobj = (TreeObject) e.getElement();
				if (tobj.getID().equals(BrokerJob.ID)) {
					GetBrokerInfo(tobj.toString());
					viewer.setExpandedState(tobj, true);
				}
			}

			public void treeCollapsed(TreeExpansionEvent e) {

			}
		});
	}

	public void DrawTree() {
		viewer.setInput(createModel());

		hookContextMenu(viewer.getControl());
		MainRegistry.NaviDraw_CAS = true;
	}

	public static void hookContextMenu(Control popctrl) {
		if (OnlyQueryEditor.connectOldServer)
			return;

		MenuManager menuMgr = new MenuManager("PopupMenu", "contextMenu");
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				ApplicationActionBarAdvisor.setActionsMenu(manager);
			}
		});
		Menu menu = menuMgr.createContextMenu(popctrl);
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("context.item");

		popctrl.setMenu(menu);
		// getSite().registerContextMenu(menuMgr, treeViewer);
	}

	/**
	 * Passing the focus request to the viewer's control.
	 */
	public void setFocus() {
		if (!MainRegistry.NaviDraw_CAS)
			DrawTree();
		viewer.getControl().setFocus();
		if (MainRegistry.Current_Navigator != MainConstants.NAVI_CAS) {
			MainRegistry.Current_Navigator = MainConstants.NAVI_CAS;
			updateWorkView();
		}
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CAS);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
	}

	public static void refresh() {
		if (myNavi == null)
			return;
		viewer.setInput(myNavi.createModel());
		viewer.refresh();
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CAS);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
		myNavi.updateWorkView();
	}

	public static TreeObject SelectBroker(String brkname) {
		if (brkname == null)
			return null;
		TreeItem rootitem = viewer.getTree().getItem(0);
		TreeItem[] items = rootitem.getItems();
		for (int i = 0, n = items.length; i < n; i++) {
			TreeObject obj = (TreeObject) items[i].getData();
			if (obj.getID().equals(BrokerStatus.ID)) {
				items = items[i].getItems();
				break;
			}
		}
		for (int i = 0, n = items.length; i < n; i++) {
			TreeObject obj = (TreeObject) items[i].getData();
			if (obj.getID().equals(BrokerJob.ID)
					&& obj.getName().equals(brkname)) {
				ArrayList newset = new ArrayList();
				newset.add(items[i]);
				viewer.getTree().setSelection(
						(TreeItem[]) newset.toArray(new TreeItem[1]));
				Current_broker = brkname;
				Current_select = BrokerJob.ID;
				myNavi.GetBrokerInfo(Current_broker);
				ApplicationActionBarAdvisor
						.AdjustToolbar(MainConstants.NAVI_CAS);
				ApplicationActionBarAdvisor
						.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
				return obj;
			}
		}
		return null;
	}

	public void GetBrokerInfo(String brkname) {
		if (brkname == null || brkname.length() <= 0)
			return;
		Shell psh = viewer.getControl().getShell();
		psh.setEnabled(false);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
		ClientSocket cs3 = new ClientSocket();
		boolean rst = cs3.SendClientMessage(psh, "broker:" + brkname,
				"getlogfileinfo");
		psh.setEnabled(true);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
		if (!rst) {
			CommonTool.ErrorBox(psh, cs3.ErrorMsg);
			MainRegistry.IsConnected = false;
			return;
		}
		psh.setEnabled(false);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
		cs3 = new ClientSocket();
		cs3.tempcmd = brkname;
		rst = cs3.SendClientMessage(psh, "bname:" + brkname, "getaslimit");
		psh.setEnabled(true);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
		if (!rst) {
			CommonTool.ErrorBox(psh, cs3.ErrorMsg);
			MainRegistry.IsConnected = false;
			return;
		}
		createModel();
		viewer.refresh();
		updateWorkView();
	}

	public static void removeAll() {
		if (root != null)
			viewer.remove(root.getChildren());
	}

	public void updateWorkView() {
		if (MainRegistry.IsConnected) {
			if (oldobj != null && oldobj.getViewID() != null)
				WorkView.SetView(oldobj.getViewID(), oldobj.toString(), oldobj
						.getID());
		}
	}

	public void SelectBroker_UpdateView(String brokername) {
		if (MainRegistry.IsConnected) {
			TreeObject brObj = SelectBroker(brokername);
			oldobj = brObj;
			updateWorkView();
		}
	}
}
