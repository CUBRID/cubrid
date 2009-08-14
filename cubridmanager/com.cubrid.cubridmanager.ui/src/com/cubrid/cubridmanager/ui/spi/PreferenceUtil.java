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

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.preference.PreferenceManager;
import org.eclipse.jface.preference.PreferenceNode;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.broker.control.BrokerParameterPropertyPage;
import com.cubrid.cubridmanager.ui.broker.control.BrokersParameterPropertyPage;
import com.cubrid.cubridmanager.ui.common.control.CMServerPropertyPage;
import com.cubrid.cubridmanager.ui.common.control.CManagerPropertyPage;
import com.cubrid.cubridmanager.ui.common.control.DatabaseServerPropertyPage;
import com.cubrid.cubridmanager.ui.common.control.ServicePropertyPage;
import com.cubrid.cubridmanager.ui.query.dialog.Query4DatabasePreferencePage;
import com.cubrid.cubridmanager.ui.query.dialog.QueryOptionPreferencePage;
import com.cubrid.cubridmanager.ui.spi.dialog.CMPreferenceDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This class is responsible to create the common dialog with perference
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class PreferenceUtil {

	/**
	 * 
	 * Create property dialog related with CUBRID node
	 * 
	 * @param parentShell
	 * @param node
	 * @return
	 */
	public static Dialog createPropertyDialog(Shell parentShell,
			ICubridNode node) {

		PreferenceManager mgr = new PreferenceManager();
		CubridNodeType type = node.getType();
		ServerType serverType = node.getServer().getServerInfo().getServerType();
		if (type == CubridNodeType.SERVER) {
			//cubrid manager server property
			CMServerPropertyPage cmServerPropertyPage = new CMServerPropertyPage(
					node, Messages.msgCmServerPropertyPageName);
			PreferenceNode cmServerNode = new PreferenceNode(
					Messages.msgCmServerPropertyPageName);
			cmServerNode.setPage(cmServerPropertyPage);
			mgr.addToRoot(cmServerNode);

			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				//service node
				ServicePropertyPage servicePorpertyPage = new ServicePropertyPage(
						node, Messages.msgServicePropertyPageName);
				PreferenceNode serviceNode = new PreferenceNode(
						Messages.msgServicePropertyPageName);
				serviceNode.setPage(servicePorpertyPage);
				mgr.addToRoot(serviceNode);
				//database server node
				DatabaseServerPropertyPage databaseServerPorpertyPage = new DatabaseServerPropertyPage(
						node, Messages.msgDatabaseServerCommonPropertyPageName,
						true);
				PreferenceNode databaseServerNode = new PreferenceNode(
						Messages.msgDatabaseServerCommonPropertyPageName);
				databaseServerNode.setPage(databaseServerPorpertyPage);
				mgr.addToRoot(databaseServerNode);
			}
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.BROKER) {
				//brokers node
				BrokersParameterPropertyPage brokersParameterPorpertyPage = new BrokersParameterPropertyPage(
						node, Messages.msgBrokerPropertyPageName);
				PreferenceNode brokersParameterNode = new PreferenceNode(
						Messages.msgBrokerPropertyPageName);
				brokersParameterNode.setPage(brokersParameterPorpertyPage);
				mgr.addToRoot(brokersParameterNode);
			}
			//mananger node
			CManagerPropertyPage managerPorpertyPage = new CManagerPropertyPage(
					node, Messages.msgManagerPropertyPageName);
			PreferenceNode managerNode = new PreferenceNode(
					Messages.msgManagerPropertyPageName);
			managerNode.setPage(managerPorpertyPage);
			mgr.addToRoot(managerNode);
			//query editor node
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				CubridServer server = node.getServer();
				QueryOptionPreferencePage queryEditorPage = new QueryOptionPreferencePage(
						server);
				PreferenceNode queryEditorNode = new PreferenceNode(
						Messages.msgQueryPropertyPageName);
				queryEditorNode.setPage(queryEditorPage);
				mgr.addToRoot(queryEditorNode);
			}
		} else if (type == CubridNodeType.DATABASE_FOLDER) {
			//database server node
			DatabaseServerPropertyPage databaseServerPorpertyPage = new DatabaseServerPropertyPage(
					node, Messages.msgDatabaseServerCommonPropertyPageName,
					true);
			PreferenceNode databaseServerNode = new PreferenceNode(
					Messages.msgDatabaseServerCommonPropertyPageName);
			databaseServerNode.setPage(databaseServerPorpertyPage);
			mgr.addToRoot(databaseServerNode);
		} else if (type == CubridNodeType.DATABASE) {
			//database parameter
			DatabaseServerPropertyPage databaseParameterPorpertyPage = new DatabaseServerPropertyPage(
					node, Messages.msgDatabaseServerPropertyPageName, false);
			PreferenceNode databaseParameterNode = new PreferenceNode(
					Messages.msgDatabaseServerPropertyPageName);
			databaseParameterNode.setPage(databaseParameterPorpertyPage);
			//database query
			CubridDatabase database = (CubridDatabase) node;
			Query4DatabasePreferencePage page = new Query4DatabasePreferencePage(
					database, Messages.msgQueryPropertyPageName);
			PreferenceNode queryNode = new PreferenceNode(
					Messages.msgQueryPropertyPageName);
			queryNode.setPage(page);
			mgr.addToRoot(queryNode);
			mgr.addToRoot(databaseParameterNode);

		} else if (type == CubridNodeType.BROKER_FOLDER) {
			//brokers node
			BrokersParameterPropertyPage brokersParameterPorpertyPage = new BrokersParameterPropertyPage(
					node, Messages.msgBrokerPropertyPageName);
			PreferenceNode brokersParameterNode = new PreferenceNode(
					Messages.msgBrokerPropertyPageName);
			brokersParameterNode.setPage(brokersParameterPorpertyPage);

			mgr.addToRoot(brokersParameterNode);
		}
		if (node.getType() == CubridNodeType.BROKER) {
			BrokerParameterPropertyPage brokerParameterPorpertyPage = new BrokerParameterPropertyPage(
					node, node.getLabel());
			PreferenceNode brokerParameterNode = new PreferenceNode(
					node.getLabel());
			brokerParameterNode.setPage(brokerParameterPorpertyPage);
			mgr.addToRoot(brokerParameterNode);
		}
		CMPreferenceDialog dlg = new CMPreferenceDialog(parentShell, mgr,
				Messages.titlePropertiesDialog);
		dlg.setPreferenceStore(CubridManagerUIPlugin.getDefault().getPreferenceStore());
		return dlg;
	}
}
