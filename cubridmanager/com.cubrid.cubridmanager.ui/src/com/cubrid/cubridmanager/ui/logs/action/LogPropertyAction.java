/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.ui.logs.action;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.ui.logs.dialog.LogPropertyDialog;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This action is responsible to show log property.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class LogPropertyAction extends
		SelectionAction {

	public static final String ID = LogPropertyAction.class.getName();

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public LogPropertyAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	public LogPropertyAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.logs.action.LogProperty");
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
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			if (node.getType() == CubridNodeType.BROKER_SQL_LOG
					|| node.getType() == CubridNodeType.LOGS_BROKER_ADMIN_LOG
					|| node.getType() == CubridNodeType.LOGS_BROKER_ACCESS_LOG
					|| node.getType() == CubridNodeType.LOGS_BROKER_ERROR_LOG
					|| node.getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Open dialog
	 */
	public void run() {

		Object[] selected = getSelectedObj();
		DefaultCubridNode node = (DefaultCubridNode) selected[0];
		LogInfo logInfo = (LogInfo) ((DefaultCubridNode) selected[0]).getAdapter(LogInfo.class);
		LogPropertyDialog logPropertyDialog = new LogPropertyDialog(getShell());
		logPropertyDialog.setLogInfo(logInfo, node);
		logPropertyDialog.create();
		logPropertyDialog.getShell().setSize(580, 330);
		logPropertyDialog.open();
	}
}
