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
package com.cubrid.cubridmanager.ui.common.control;

import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This property page show the cubrid manager server information
 * 
 * CMServerPropertyPage Description
 * 
 * @author pangqiren
 * @version 1.0 - 2009-7-16 created by pangqiren
 */
public class CMServerPropertyPage extends
		PreferencePage {

	private ICubridNode node = null;
	private Label hostNameInfoLabel;
	private Label hostAddressInfoLabel;
	private Label portInfoLabel;
	private Label userNameInfoLabel;
	private Label serverTypeInfoLabel;

	/**
	 * The constructor
	 * 
	 * @param node
	 * @param name
	 */
	public CMServerPropertyPage(ICubridNode node, String name) {
		super(name, null);
		noDefaultAndApplyButton();
		this.node = node;
	}

	/**
	 * Creates the page content
	 */
	protected Control createContents(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp( parent, CubridManagerHelpContextIDs.hostInfoProperty);
		
		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		createGeneralGroup(composite);
		createServerTypeGroup(composite);
		initial();
		return composite;
	}

	/**
	 * 
	 * Create the general group composite
	 * 
	 * @param parent
	 */
	private void createGeneralGroup(Composite parent) {
		Group generalInfoGroup = new Group(parent, SWT.NONE);
		generalInfoGroup.setText(Messages.grpConnectInformation);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		generalInfoGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		generalInfoGroup.setLayout(layout);

		Label hostNameLabel = new Label(generalInfoGroup, SWT.LEFT);
		hostNameLabel.setText(Messages.lblHostName);
		hostNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		hostNameInfoLabel = new Label(generalInfoGroup, SWT.LEFT);
		hostNameInfoLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label hostAddressLabel = new Label(generalInfoGroup, SWT.LEFT);
		hostAddressLabel.setText(Messages.lblAddress);
		hostAddressLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		hostAddressInfoLabel = new Label(generalInfoGroup, SWT.LEFT);
		hostAddressInfoLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label portLabel = new Label(generalInfoGroup, SWT.LEFT);
		portLabel.setText(Messages.lblPort);
		portLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		portInfoLabel = new Label(generalInfoGroup, SWT.LEFT);
		portInfoLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label userNameLabel = new Label(generalInfoGroup, SWT.LEFT);
		userNameLabel.setText(Messages.lblUserName);
		userNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		userNameInfoLabel = new Label(generalInfoGroup, SWT.LEFT);
		userNameInfoLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
	}

	/**
	 * 
	 * Create the server type group composite
	 * 
	 * @param parent
	 */
	private void createServerTypeGroup(Composite parent) {
		Group serverTypeGroup = new Group(parent, SWT.NONE);
		serverTypeGroup.setText(Messages.grpServerType);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		serverTypeGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		serverTypeGroup.setLayout(layout);

		Label serverTypeLabel = new Label(serverTypeGroup, SWT.LEFT);
		serverTypeLabel.setText(Messages.lblServerType);
		serverTypeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		serverTypeInfoLabel = new Label(serverTypeGroup, SWT.LEFT);
		serverTypeInfoLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
	}

	/**
	 * 
	 * initial the page content
	 * 
	 */
	private void initial() {
		CubridServer server = node.getServer();
		ServerInfo serverInfo = server != null ? server.getServerInfo() : null;
		if (server != null && serverInfo != null) {
			hostNameInfoLabel.setText(server.getLabel());
			hostAddressInfoLabel.setText(server.getHostAddress());
			portInfoLabel.setText(server.getMonPort());
			userNameInfoLabel.setText(server.getUserName());
			ServerType serverType = serverInfo.getServerType();
			String type = "server,broker";
			if (serverType == ServerType.BROKER) {
				type = "broker";
			} else if (serverType == ServerType.DATABASE) {
				type = "server";
			}
			serverTypeInfoLabel.setText(type);
		}
	}

}