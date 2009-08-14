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
package com.cubrid.cubridmanager.ui.spi.model.loader.jobauto;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;

/**
 * 
 * This class is responsible to load all children of CUBRID database Job
 * automation folder,these children include Backup plan and Query plan folder.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridJobAutoFolderLoader extends
		CubridNodeLoader {

	private static final String BACKUP_PLAN_FOLDER_NAME = Messages.msgBackupPlanFolderName;
	private static final String QUERY_PLAN_FOLDER_NAME = Messages.msgQueryPlanFolderName;

	public static final String BACKUP_PLAN_FOLDER_ID = "Backup plan";
	public static final String QUERY_PLAN_FOLDER_ID = "Query plan";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		// add backup plan folder
		String backupPlanFolderId = parent.getId() + NODE_SEPARATOR
				+ BACKUP_PLAN_FOLDER_ID;
		ICubridNode backupPlanFolder = parent.getChild(backupPlanFolderId);
		if (backupPlanFolder == null) {
			backupPlanFolder = new DefaultSchemaNode(backupPlanFolderId,
					BACKUP_PLAN_FOLDER_NAME, "icons/navigator/folder.png");
			backupPlanFolder.setType(CubridNodeType.BACKUP_PLAN_FOLDER);
			backupPlanFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridBackupPlanFolderLoader();
			loader.setLevel(getLevel());
			backupPlanFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				backupPlanFolder.getChildren(monitor);
			}
			parent.addChild(backupPlanFolder);
		} else {
			if (backupPlanFolder.getLoader() != null
					&& backupPlanFolder.getLoader().isLoaded()) {
				backupPlanFolder.getLoader().setLoaded(false);
				backupPlanFolder.getChildren(monitor);
			}
		}
		// add query plan folder
		String queryPlanFolderId = parent.getId() + NODE_SEPARATOR
				+ QUERY_PLAN_FOLDER_ID;
		ICubridNode queryPlanFolder = parent.getChild(queryPlanFolderId);
		if (queryPlanFolder == null) {
			queryPlanFolder = new DefaultSchemaNode(queryPlanFolderId,
					QUERY_PLAN_FOLDER_NAME, "icons/navigator/folder.png");
			queryPlanFolder.setType(CubridNodeType.QUERY_PLAN_FOLDER);
			queryPlanFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridQueryPlanFolderLoader();
			loader.setLevel(getLevel());
			queryPlanFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				queryPlanFolder.getChildren(monitor);
			}
			parent.addChild(queryPlanFolder);
		} else {
			if (queryPlanFolder.getLoader() != null
					&& queryPlanFolder.getLoader().isLoaded()) {
				queryPlanFolder.getLoader().setLoaded(false);
				queryPlanFolder.getChildren(monitor);
			}
		}
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
