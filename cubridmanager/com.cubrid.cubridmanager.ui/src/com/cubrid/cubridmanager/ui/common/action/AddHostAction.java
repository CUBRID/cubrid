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
package com.cubrid.cubridmanager.ui.common.action;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeItem;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.ui.common.dialog.HostDialog;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.loader.CubridServerLoader;

/**
 * 
 * This action is responsible to add host to navigator
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class AddHostAction extends
		SelectionAction {

	public static final String ID = AddHostAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public AddHostAction(Shell shell, String text, ImageDescriptor enabledIcon,
			ImageDescriptor disabledIcon) {
		this(shell, null, text, enabledIcon, disabledIcon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public AddHostAction(Shell shell, ISelectionProvider provider, String text,
			ImageDescriptor enabledIcon, ImageDescriptor disabledIcon) {
		super(shell, provider, text, enabledIcon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
		this.setDisabledImageDescriptor(disabledIcon);
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return true;
	}

	/**
	 * Override
	 */
	protected void selectionChanged(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			setEnabled(true);
			return;
		}
		super.selectionChanged(selection);
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ICubridNode)
			return true;
		return false;
	}

	/**
	 * Open the host dialog and add host
	 */
	public void run() {
		HostDialog dialog = new HostDialog(getShell());
		int returnCode = dialog.open();
		if (returnCode == HostDialog.ADD_ID
				|| returnCode == HostDialog.CONNECT_ID) {
			ServerInfo serverInfo = dialog.getServerInfo();
			CubridServer server = new CubridServer(serverInfo.getServerName(),
					serverInfo.getServerName(), "icons/navigator/host.png",
					"icons/navigator/host_connected.png");
			server.setServerInfo(serverInfo);
			server.setLoader(new CubridServerLoader());		
			CubridNodeManager.getInstance().addServer(server);
			ISelectionProvider provider = getSelectionProvider();
			if (provider instanceof TreeViewer) {
				TreeViewer treeViewer = (TreeViewer) provider;
				Tree tree = treeViewer.getTree();
				TreeItem item = new TreeItem(tree, SWT.NONE);
				item.setText(server.getLabel());
				item.setData(server);
				treeViewer.refresh(server, true);
				if (returnCode == HostDialog.ADD_ID) {
					treeViewer.expandToLevel(server, 1);
					CubridNodeManager.getInstance().fireCubridNodeChanged(
							new CubridNodeChangedEvent(server,
									CubridNodeChangedEventType.NODE_ADD));
				}
				if (returnCode == HostDialog.CONNECT_ID) {
					treeViewer.expandToLevel(server, 1);
					CubridNodeManager.getInstance().fireCubridNodeChanged(
							new CubridNodeChangedEvent(server,
									CubridNodeChangedEventType.SERVER_CONNECTED));
				}
			}
		}
	}
}
