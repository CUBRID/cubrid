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
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This action is responsible to disconnect host from navigator
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DisConnectHostAction extends
		SelectionAction {

	public static final String ID = DisConnectHostAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DisConnectHostAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DisConnectHostAction(Shell shell, ISelectionProvider provider,
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
		return true;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof CubridServer) {
			CubridServer server = (CubridServer) obj;
			return server.isConnected();
		}
		return false;
	}

	/**
	 * Disconnect host
	 */
	public void run() {
		Object[] objArr = this.getSelectedObj();
		if (objArr == null || objArr.length <= 0) {
			setEnabled(false);
			return;
		}
		String hostNames = "";
		for (int i = 0; objArr != null && i < objArr.length; i++) {
			if (!isSupported(objArr[i])) {
				setEnabled(false);
				return;
			}
			ICubridNode node = (ICubridNode) objArr[i];
			hostNames += node.getLabel();
			if (i != objArr.length - 1) {
				hostNames += ",";
			}
		}
		boolean isDisconnectHost = CommonTool.openConfirmBox(getShell(),
				Messages.bind(Messages.msgConfirmDisconnectHost, hostNames));
		if (!isDisconnectHost) {
			return;
		}
		ISelectionProvider provider = this.getSelectionProvider();
		if (provider instanceof TreeViewer) {
			TreeViewer viewer = (TreeViewer) provider;
			boolean isContinue = true;
			for (int i = 0; i < objArr.length; i++) {
				CubridServer server = (CubridServer) objArr[i];
				if (!LayoutManager.checkAllQueryEditor(server)) {
					isContinue = false;
					break;
				}
			}
			if (!isContinue) {
				return;
			}
			for (int i = 0; i < objArr.length; i++) {
				CubridServer server = (CubridServer) objArr[i];
				boolean isSaved = server.getServerInfo().isConnected();
				LayoutManager.closeAllEditorAndViewInServer(server, isSaved);
				server.removeAllChild();
				ServerManager.getInstance().setConnected(
						server.getHostAddress(),
						server.getMonPort() != null ? Integer.parseInt(server.getMonPort())
								: 0, false);
				viewer.refresh(server, true);
				ActionManager.getInstance().fireSelectionChanged(getSelection());
				CubridNodeManager.getInstance().fireCubridNodeChanged(
						new CubridNodeChangedEvent(server,
								CubridNodeChangedEventType.SERVER_DISCONNECTED));
			}
		}

	}
}
