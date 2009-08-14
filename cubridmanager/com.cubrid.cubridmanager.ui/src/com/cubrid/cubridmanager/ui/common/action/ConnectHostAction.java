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
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.ui.common.dialog.HostDialog;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * 
 * This action is responsible to connect host
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ConnectHostAction extends
		SelectionAction {

	public static final String ID = ConnectHostAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public ConnectHostAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	public ConnectHostAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof CubridServer) {
			CubridServer server = (CubridServer) obj;
			ServerInfo serverInfo = server.getServerInfo();
			if (!serverInfo.isConnected())
				return true;
		}
		return false;
	}

	/**
	 * Open the host dialog and connect to host
	 */
	public void run() {
		HostDialog dialog = new HostDialog(getShell());
		CubridServer server = (CubridServer) getSelectedObj()[0];
		dialog.setServer(server);
		if (dialog.open() == HostDialog.CONNECT_ID) {
			ServerInfo serverInfo = dialog.getServerInfo();
			server.setId(serverInfo.getServerName());
			server.setLabel(serverInfo.getServerName());
			server.setServerInfo(serverInfo);
			ServerManager.getInstance().addServer(serverInfo.getHostAddress(),
					serverInfo.getHostMonPort(), serverInfo);
			ISelectionProvider provider = getSelectionProvider();
			if (provider instanceof TreeViewer) {
				server.removeAllChild();
				if (server.getLoader() != null) {
					server.getLoader().setLoaded(false);
				}
				TreeViewer treeViewer = (TreeViewer) provider;
				treeViewer.refresh(server, true);
				treeViewer.expandToLevel(server, 1);
				setEnabled(false);
			}
			CubridNodeManager.getInstance().saveServers();
			ActionManager.getInstance().fireSelectionChanged(getSelection());
			LayoutManager.getInstance().fireSelectionChanged(getSelection());
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent(server,
							CubridNodeChangedEventType.SERVER_CONNECTED));
		}
	}
}
