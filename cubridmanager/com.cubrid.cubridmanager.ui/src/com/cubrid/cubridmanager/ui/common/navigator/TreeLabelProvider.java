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

import org.eclipse.jface.viewers.LabelProvider;
import org.eclipse.swt.graphics.Image;
import org.eclipse.ui.progress.PendingUpdateAdapter;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.model.CubridBroker;
import com.cubrid.cubridmanager.ui.spi.model.CubridBrokerFolder;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * CUBIRD manager navigator treeviewer label provider
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class TreeLabelProvider extends
		LabelProvider {

	@Override
	public Image getImage(Object element) {
		String iconPath = "";
		if (element instanceof CubridServer) {
			CubridServer server = (CubridServer) element;
			if (!server.isConnected()) {
				iconPath = server.getDisConnectedIconPath();
			} else {
				iconPath = server.getConnectedIconPath();
			}
		} else if (element instanceof CubridDatabase) {
			CubridDatabase database = (CubridDatabase) element;
			if (database.getRunningType() == DbRunningType.STANDALONE
					&& database.isLogined()) {
				iconPath = database.getStopAndLoginIconPath();
			} else if (database.getRunningType() == DbRunningType.STANDALONE
					&& !database.isLogined()) {
				iconPath = database.getStopAndLogoutIconPath();
			} else if (database.getRunningType() == DbRunningType.CS
					&& database.isLogined()) {
				iconPath = database.getStartAndLoginIconPath();
			} else if (database.getRunningType() == DbRunningType.CS
					&& !database.isLogined()) {
				iconPath = database.getStartAndLogoutIconPath();
			}
		} else if (element instanceof CubridBrokerFolder) {
			CubridBrokerFolder brokerFolder = (CubridBrokerFolder) element;
			if (brokerFolder.isRunning()) {
				iconPath = brokerFolder.getStartedIconPath();
			} else {
				iconPath = brokerFolder.getStopedIconPath();
			}
		} else if (element instanceof CubridBroker) {
			CubridBroker broker = (CubridBroker) element;
			if (broker.isRunning()) {
				iconPath = broker.getStartedIconPath();
			} else {
				iconPath = broker.getStopedIconPath();
			}
		} else if (element instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) element;
			iconPath = node.getIconPath();
		}
		if (iconPath != null && iconPath.length() > 0) {
			return CubridManagerUIPlugin.getImage(iconPath.trim());
		}
		return super.getImage(element);
	}

	@Override
	public String getText(Object element) {
		if (element instanceof ICubridNode) {
			return ((ICubridNode) element).getLabel();
		} else if (element instanceof PendingUpdateAdapter) {
			return Messages.msgLoading;
		}
		return super.getText(element);
	}
}
