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
package com.cubrid.cubridmanager.ui.common.navigator;

import java.util.List;

import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.ITreeViewerListener;
import org.eclipse.jface.viewers.TreeExpansionEvent;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;
import org.eclipse.ui.part.ViewPart;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.action.AddHostAction;
import com.cubrid.cubridmanager.ui.common.action.ChangePasswordAction;
import com.cubrid.cubridmanager.ui.common.action.ConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.DeleteHostAction;
import com.cubrid.cubridmanager.ui.common.action.DisConnectHostAction;
import com.cubrid.cubridmanager.ui.common.action.RefreshAction;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.CubridNodeTypeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This view part is responsible for show all CUBRID database object as tree
 * structure way
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridNavigatorView extends
		ViewPart {

	private TreeViewer tv = null;

	private static CubridNavigatorView navigatorView;

	public void init(IViewSite site) throws PartInitException {
		super.init(site);
		navigatorView = this;
	}

	@Override
	public void createPartControl(Composite parent) {
		tv = new TreeViewer(parent, SWT.MULTI | SWT.H_SCROLL | SWT.V_SCROLL);
		tv.setContentProvider(new DeferredContentProvider());
		tv.setLabelProvider(new TreeLabelProvider());
		tv.addDoubleClickListener(LayoutManager.getInstance());
		tv.addTreeListener(new ITreeViewerListener() {
			public void treeCollapsed(TreeExpansionEvent event) {
				CommonTool.clearExpandedElements(tv);
			}

			public void treeExpanded(TreeExpansionEvent event) {
				CommonTool.clearExpandedElements(tv);
			}
		});
		CubridNodeManager cubridNodeManager = CubridNodeManager.getInstance();
		List<CubridServer> serverList = cubridNodeManager.getAllServer();
		tv.setInput(serverList);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(tv.getTree(), CubridManagerHelpContextIDs.hostAdd);
		// fill in context menu
		MenuManager menuManager = new MenuManager();
		menuManager.setRemoveAllWhenShown(true);
		menuManager.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				IStructuredSelection selection = (IStructuredSelection) tv.getSelection();
				if (selection == null || selection.isEmpty()) {
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									AddHostAction.ID));
					return;
				}
				ICubridNode node = null;
				Object obj = selection.getFirstElement();
				if (obj instanceof ICubridNode) {
					node = (ICubridNode) obj;
				} else {
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									AddHostAction.ID));
					return;
				}
				CubridNodeType type = node.getType();
				if (type == CubridNodeType.SERVER) {
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									ConnectHostAction.ID));
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									DisConnectHostAction.ID));
					manager.add(new Separator());
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									AddHostAction.ID));
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									DeleteHostAction.ID));
					manager.add(new Separator());
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									ChangePasswordAction.ID));
					manager.add(new Separator());
				}
				ActionManager.getInstance().setActionsMenu(manager);
				if (CubridNodeTypeManager.isCanRefresh(node.getType())) {
					manager.add(new Separator());
					ActionManager.addActionToManager(manager,
							ActionManager.getInstance().getAction(
									RefreshAction.ID));
				}
			}
		});
		Menu contextMenu = menuManager.createContextMenu(tv.getControl());
		tv.getControl().setMenu(contextMenu);
		tv.getTree().addMouseListener(new MouseAdapter() {
			public void mouseUp(MouseEvent e) {
				if (e.button == 1
						&& LayoutManager.getInstance().isUseClickOnce()) {
					ISelection selection = tv.getSelection();
					if (selection == null || selection.isEmpty()) {
						return;
					}
					Object obj = ((IStructuredSelection) selection).getFirstElement();
					if (!(obj instanceof ICubridNode)) {
						return;
					}
					ICubridNode cubridNode = (ICubridNode) obj;
					LayoutManager.getInstance().openEditorOrView(cubridNode);
				}
			}
		});
		ActionManager.getInstance().changeSelectionProvider(tv);
		LayoutManager.getInstance().changeSelectionProvider(tv);
		CubridNodeManager.getInstance().addCubridNodeChangeListener(
				LayoutManager.getInstance());
	}

	@Override
	public void dispose() {
		super.dispose();
	}

	@Override
	public void setFocus() {
		if (tv != null && tv.getControl() != null
				&& !tv.getControl().isDisposed())
			tv.getControl().setFocus();
	}

	/**
	 * 
	 * Get tree viewer
	 * 
	 * @return
	 */
	public TreeViewer getViewer() {
		return tv;
	}

	/**
	 * 
	 * Get navigator view part
	 * 
	 * @return
	 */
	public static CubridNavigatorView getNavigatorView() {
		return navigatorView;
	}
}
