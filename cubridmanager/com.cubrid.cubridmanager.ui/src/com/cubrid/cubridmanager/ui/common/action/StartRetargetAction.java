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
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.ui.broker.action.StartBrokerAction;
import com.cubrid.cubridmanager.ui.broker.action.StartBrokerEnvAction;
import com.cubrid.cubridmanager.ui.cubrid.database.action.StartDatabaseAction;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridBroker;
import com.cubrid.cubridmanager.ui.spi.model.CubridBrokerFolder;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * This action is responsible to start database or broker
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-19 created by pangqiren
 */
public class StartRetargetAction extends
		SelectionAction {

	public static final String ID = StartRetargetAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public StartRetargetAction(Shell shell, String text,
			ImageDescriptor enabledIcon, ImageDescriptor disabledIcon) {
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
	public StartRetargetAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor enabledIcon,
			ImageDescriptor disabledIcon) {
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
		return false;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ISchemaNode) {
			StartDatabaseAction startDatabaseAction = (StartDatabaseAction) ActionManager.getInstance().getAction(
					StartDatabaseAction.ID);
			return startDatabaseAction.isSupported(obj);
		}
		if (obj instanceof CubridBroker) {
			StartBrokerAction startBrokerAction = (StartBrokerAction) ActionManager.getInstance().getAction(
					StartBrokerAction.ID);
			return startBrokerAction.isSupported(obj);
		}
		if (obj instanceof CubridBrokerFolder) {
			StartBrokerEnvAction startBrokerEnvAction = (StartBrokerEnvAction) ActionManager.getInstance().getAction(
					StartBrokerEnvAction.ID);
			return startBrokerEnvAction.isSupported(obj);
		}
		return false;
	}

	/**
	 * start database or broker
	 */
	public void run() {
		final Object[] obj = this.getSelectedObj();
		if (!isSupported(obj[0])) {
			return;
		}
		ICubridNode node = (ICubridNode) obj[0];
		if (node instanceof ISchemaNode) {
			StartDatabaseAction startDatabaseAction = (StartDatabaseAction) ActionManager.getInstance().getAction(
					StartDatabaseAction.ID);
			startDatabaseAction.run();
		}
		if (node instanceof CubridBroker) {
			StartBrokerAction startBrokerAction = (StartBrokerAction) ActionManager.getInstance().getAction(
					StartBrokerAction.ID);
			startBrokerAction.run();
		}
		if (node instanceof CubridBrokerFolder) {
			StartBrokerEnvAction startBrokerEnvAction = (StartBrokerEnvAction) ActionManager.getInstance().getAction(
					StartBrokerEnvAction.ID);
			startBrokerEnvAction.run();
		}
	}

}
