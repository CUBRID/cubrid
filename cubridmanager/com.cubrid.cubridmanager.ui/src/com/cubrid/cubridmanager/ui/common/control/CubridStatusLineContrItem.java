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

import java.util.List;

import org.eclipse.jface.action.ContributionItem;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * CUBRID Manager status line contribution item,it show server
 * info(serverName:serverUser) and database info(dbName:dbUser)
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-21 created by pangqiren
 */
public class CubridStatusLineContrItem extends
		ContributionItem {

	public static String ID = "CubridStatusLineContrItem";
	private ICubridNode cubridNode = null;

	/**
	 * The constructor
	 * 
	 * @param cubridNode
	 */
	public CubridStatusLineContrItem(ICubridNode cubridNode) {
		super(ID);
		this.cubridNode = cubridNode;
	}

	@Override
	public void fill(Composite parent) {
		int numColumns = 3;
		int width = 180;
		String serverInfo = cubridNode.getServer().getLabel();
		ServerUserInfo userInfo = cubridNode.getServer().getServerInfo().getLoginedUserInfo();
		if (userInfo != null && userInfo.getUserName() != null
				&& userInfo.getUserName().trim().length() > 0) {
			serverInfo = userInfo.getUserName() + "@" + serverInfo;
		}
		String monPort = cubridNode.getServer().getMonPort();
		if (monPort != null && monPort.trim().length() > 0) {
			serverInfo = serverInfo + ":" + monPort;
		}
		String databaseInfo = "";
		if (cubridNode instanceof ISchemaNode) {
			numColumns = 5;
			ISchemaNode schemaNode = (ISchemaNode) cubridNode;
			CubridDatabase database = schemaNode.getDatabase();
			databaseInfo += database.getLabel();
			DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
			if (database.isLogined() && dbUserInfo != null
					&& dbUserInfo.getName() != null
					&& dbUserInfo.getName().trim().length() > 0) {
				databaseInfo = dbUserInfo.getName() + "@" + databaseInfo;
				width = 100;
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
									+ brokerInfo.getPort() + "/" + status + "]";
							databaseInfo += ":" + text;
							width = 20;
							break;
						}
					}
					if (!isExist) {
						if (brokerPort != null
								&& brokerPort.trim().length() > 0) {
							databaseInfo += ":" + brokerPort;
							width = 20;
						}
					}
				}
			} else if (brokerPort != null && brokerPort.trim().length() > 0) {
				width = 20;
				databaseInfo += ":" + brokerPort;
			}
		}
		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.horizontalSpacing = 10;
		layout.verticalSpacing = 0;
		layout.marginHeight = 0;
		layout.marginWidth = width;
		layout.numColumns = numColumns;
		composite.setLayout(layout);

		CLabel emptyLabel = new CLabel(composite, SWT.NONE | SWT.CENTER
				| SWT.SEPARATOR);
		emptyLabel.setImage(CubridManagerUIPlugin.getImage("icons/bar.png"));
		emptyLabel.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
				| GridData.GRAB_VERTICAL));

		CLabel hostLabel = new CLabel(composite, SWT.NONE | SWT.CENTER
				| SWT.SEPARATOR);
		hostLabel.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
				| GridData.GRAB_VERTICAL));
		hostLabel.setText(serverInfo);

		emptyLabel = new CLabel(composite, SWT.NONE | SWT.CENTER
				| SWT.SEPARATOR);
		emptyLabel.setImage(CubridManagerUIPlugin.getImage("icons/bar.png"));
		emptyLabel.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
				| GridData.GRAB_VERTICAL));

		if (numColumns > 3) {
			CLabel databaseLabel = new CLabel(composite, SWT.NONE | SWT.CENTER
					| SWT.SEPARATOR);
			databaseLabel.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
					| GridData.GRAB_VERTICAL));
			databaseLabel.setText(databaseInfo);

			emptyLabel = new CLabel(composite, SWT.NONE | SWT.CENTER
					| SWT.SEPARATOR);
			emptyLabel.setImage(CubridManagerUIPlugin.getImage("icons/bar.png"));
			emptyLabel.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
					| GridData.GRAB_VERTICAL));
		}
	}

	@Override
	public boolean isGroupMarker() {
		return true;
	}
}
