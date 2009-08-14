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
package com.cubrid.cubridmanager.ui.spi.model.loader;

import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.VolumeType;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.editor.VolumeFolderInfoEditor;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.editor.VolumeInformationEditor;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * This class is responsible to load all children of CUBRID database space
 * folder,these children include Generic volume,Data volume,Index volume,Temp
 * volume,Log volume(including Active log folder and Archive log folder) folder
 * and all volume.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDbSpaceFolderLoader extends
		CubridNodeLoader {

	private static final String GENERIC_VOLUME_FOLDER_NAME = Messages.msgGenerialVolumeFolderName;
	private static final String DATA_VOLUME_FOLDER_NAME = Messages.msgDataVolumeFolderName;
	private static final String INDEX_VOLUME_FOLDER_NAME = Messages.msgIndexVolumeFolderName;
	private static final String TEMP_VOLUME_FOLDER_NAME = Messages.msgTempVolumeFolderName;
	private static final String LOG_VOLUME_FOLDER_NAME = Messages.msgLogVolumeFolderName;
	private static final String ACTIVE_LOG_FOLDER_NAME = Messages.msgActiveLogFolderName;
	private static final String ARCHIVE_LOG_FOLDER_NAME = Messages.msgArchiveLogFolderName;

	public static final String GENERIC_VOLUME_FOLDER_ID = "Generic";
	public static final String DATA_VOLUME_FOLDER_ID = "Data";
	public static final String INDEX_VOLUME_FOLDER_ID = "Index";
	public static final String TEMP_VOLUME_FOLDER_ID = "Temp";
	public static final String LOG_VOLUME_FOLDER_ID = "Log";
	public static final String ACTIVE_LOG_FOLDER_ID = "Active";
	public static final String ARCHIVE_LOG_FOLDER_ID = "Archive";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		// add generic volume folder
		String genericVolumeFolderId = parent.getId() + NODE_SEPARATOR
				+ GENERIC_VOLUME_FOLDER_ID;
		ICubridNode genericVolumeFolder = parent.getChild(genericVolumeFolderId);
		if (genericVolumeFolder == null) {
			genericVolumeFolder = new DefaultSchemaNode(genericVolumeFolderId,
					GENERIC_VOLUME_FOLDER_NAME, "icons/navigator/folder.png");
			genericVolumeFolder.setType(CubridNodeType.GENERIC_VOLUME_FOLDER);
			genericVolumeFolder.setContainer(true);
			genericVolumeFolder.setEditorId(VolumeFolderInfoEditor.ID);
			parent.addChild(genericVolumeFolder);
		}
		// add data volume folder
		String dataVolumeFolderId = parent.getId() + NODE_SEPARATOR
				+ DATA_VOLUME_FOLDER_ID;
		ICubridNode dataVolumeFolder = parent.getChild(dataVolumeFolderId);
		if (dataVolumeFolder == null) {
			dataVolumeFolder = new DefaultSchemaNode(dataVolumeFolderId,
					DATA_VOLUME_FOLDER_NAME, "icons/navigator/folder.png");
			dataVolumeFolder.setType(CubridNodeType.DATA_VOLUME_FOLDER);
			dataVolumeFolder.setContainer(true);
			dataVolumeFolder.setEditorId(VolumeFolderInfoEditor.ID);
			parent.addChild(dataVolumeFolder);
		}
		// add index volume folder
		String indexVolumeFolderId = parent.getId() + NODE_SEPARATOR
				+ INDEX_VOLUME_FOLDER_ID;
		ICubridNode indexVolumeFolder = parent.getChild(indexVolumeFolderId);
		if (indexVolumeFolder == null) {
			indexVolumeFolder = new DefaultSchemaNode(indexVolumeFolderId,
					INDEX_VOLUME_FOLDER_NAME, "icons/navigator/folder.png");
			indexVolumeFolder.setType(CubridNodeType.INDEX_VOLUME_FOLDER);
			indexVolumeFolder.setContainer(true);
			indexVolumeFolder.setEditorId(VolumeFolderInfoEditor.ID);
			parent.addChild(indexVolumeFolder);
		}
		// add temp volume folder
		String tempVolumeFolderId = parent.getId() + NODE_SEPARATOR
				+ TEMP_VOLUME_FOLDER_ID;
		ICubridNode tempVolumeFolder = parent.getChild(tempVolumeFolderId);
		if (tempVolumeFolder == null) {
			tempVolumeFolder = new DefaultSchemaNode(tempVolumeFolderId,
					TEMP_VOLUME_FOLDER_NAME, "icons/navigator/folder.png");
			tempVolumeFolder.setType(CubridNodeType.TEMP_VOLUME_FOLDER);
			tempVolumeFolder.setContainer(true);
			tempVolumeFolder.setEditorId(VolumeFolderInfoEditor.ID);
			parent.addChild(tempVolumeFolder);
		}
		// add log volume folder
		String logVolumeFolderId = parent.getId() + NODE_SEPARATOR
				+ LOG_VOLUME_FOLDER_ID;
		ICubridNode logVolumeFolder = parent.getChild(logVolumeFolderId);
		if (logVolumeFolder == null) {
			logVolumeFolder = new DefaultSchemaNode(logVolumeFolderId,
					LOG_VOLUME_FOLDER_NAME, "icons/navigator/folder.png");
			logVolumeFolder.setType(CubridNodeType.LOG_VOLUEM_FOLDER);
			logVolumeFolder.setContainer(true);
			//			logVolumeFolder.setEditorId(VolumeFolderInfoEditor.ID);
			parent.addChild(logVolumeFolder);
		}
		// add active log folder
		String activeLogFolderId = logVolumeFolder.getId() + NODE_SEPARATOR
				+ ACTIVE_LOG_FOLDER_ID;
		ICubridNode activeLogFolder = logVolumeFolder.getChild(activeLogFolderId);
		if (activeLogFolder == null) {
			activeLogFolder = new DefaultSchemaNode(activeLogFolderId,
					ACTIVE_LOG_FOLDER_NAME, "icons/navigator/folder.png");
			activeLogFolder.setType(CubridNodeType.ACTIVE_LOG_FOLDER);
			activeLogFolder.setContainer(true);
			activeLogFolder.setEditorId(VolumeFolderInfoEditor.ID);
			logVolumeFolder.addChild(activeLogFolder);
		}
		// add archive log folder
		String archiveLogFolderId = logVolumeFolder.getId() + NODE_SEPARATOR
				+ ARCHIVE_LOG_FOLDER_ID;
		ICubridNode archiveLogFolder = logVolumeFolder.getChild(archiveLogFolderId);
		if (archiveLogFolder == null) {
			archiveLogFolder = new DefaultSchemaNode(archiveLogFolderId,
					ARCHIVE_LOG_FOLDER_NAME, "icons/navigator/folder.png");
			archiveLogFolder.setType(CubridNodeType.ARCHIVE_LOG_FOLDER);
			archiveLogFolder.setContainer(true);
			archiveLogFolder.setEditorId(VolumeFolderInfoEditor.ID);
			logVolumeFolder.addChild(archiveLogFolder);
		}

		DbSpaceInfoList dbSpaceInfo = new DbSpaceInfoList();
		CubridDatabase database = ((ISchemaNode) parent).getDatabase();
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		final CommonQueryTask<DbSpaceInfoList> task = new CommonQueryTask<DbSpaceInfoList>(
				parent.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, dbSpaceInfo);
		task.setDbName(database.getLabel());
		Thread thread = new Thread() {
			public void run() {
				while (!monitor.isCanceled() && !isLoaded()) {
					try {
						sleep(WAIT_TIME);
					} catch (InterruptedException e) {
					}
				}
				if (monitor.isCanceled()) {
					task.cancel();
				}
			}
		};
		thread.start();
		task.execute();
		final String errorMsg = task.getErrorMsg();
		if (!monitor.isCanceled() && errorMsg != null
				&& errorMsg.trim().length() > 0) {
			genericVolumeFolder.removeAllChild();
			dataVolumeFolder.removeAllChild();
			indexVolumeFolder.removeAllChild();
			tempVolumeFolder.removeAllChild();
			archiveLogFolder.removeAllChild();
			activeLogFolder.removeAllChild();
			
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(null, errorMsg);
				}
			});
			setLoaded(true);
			return;
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}

		genericVolumeFolder.removeAllChild();
		dataVolumeFolder.removeAllChild();
		indexVolumeFolder.removeAllChild();
		tempVolumeFolder.removeAllChild();
		archiveLogFolder.removeAllChild();
		activeLogFolder.removeAllChild();
		
		dbSpaceInfo = task.getResultModel();
		if (dbSpaceInfo != null) {
			List<DbSpaceInfo> spaceInfoList = dbSpaceInfo.getSpaceinfo();
			if (spaceInfoList != null && spaceInfoList.size() > 0) {
				for (DbSpaceInfo spaceInfo : spaceInfoList) {
					ICubridNode volumeNode = new DefaultSchemaNode("",
							spaceInfo.getSpacename(), "");
					volumeNode.setContainer(false);
					volumeNode.setModelObj(spaceInfo);
					volumeNode.setEditorId(VolumeInformationEditor.ID);
					String type = spaceInfo.getType();
					if (type == null) {
						continue;
					}
					if (type.equals(VolumeType.GENERIC.getText())) {
						String id = genericVolumeFolder.getId()
								+ NODE_SEPARATOR + spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setType(CubridNodeType.GENERIC_VOLUME);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						genericVolumeFolder.addChild(volumeNode);
					} else if (type.equals(VolumeType.DATA.getText())) {
						String id = dataVolumeFolder.getId() + NODE_SEPARATOR
								+ spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						volumeNode.setType(CubridNodeType.DATA_VOLUME);
						dataVolumeFolder.addChild(volumeNode);
					} else if (type.equals(VolumeType.INDEX.getText())) {
						String id = indexVolumeFolder.getId() + NODE_SEPARATOR
								+ spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						volumeNode.setType(CubridNodeType.INDEX_VOLUME);
						indexVolumeFolder.addChild(volumeNode);
					} else if (type.equals(VolumeType.TEMP.getText())) {
						String id = tempVolumeFolder.getId() + NODE_SEPARATOR
								+ spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						volumeNode.setType(CubridNodeType.TEMP_VOLUME);
						tempVolumeFolder.addChild(volumeNode);
					} 
					else if (type.equals(VolumeType.ARCHIVE_LOG.getText())) {
						String id = archiveLogFolder.getId() + NODE_SEPARATOR
								+ spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setEditorId(null);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						volumeNode.setType(CubridNodeType.ARCHIVE_LOG);
						archiveLogFolder.addChild(volumeNode);
					} else if (type.equals(VolumeType.ACTIVE_LOG.getText())) {
						String id = activeLogFolder.getId() + NODE_SEPARATOR
								+ spaceInfo.getSpacename();
						volumeNode.setId(id);
						volumeNode.setEditorId(null);
						volumeNode.setIconPath("icons/navigator/volume_item.png");
						volumeNode.setType(CubridNodeType.ACTIVE_LOG);
						activeLogFolder.addChild(volumeNode);
					}
				}
			}
			Collections.sort(genericVolumeFolder.getChildren());
			Collections.sort(dataVolumeFolder.getChildren());
			Collections.sort(indexVolumeFolder.getChildren());
			Collections.sort(tempVolumeFolder.getChildren());
			Collections.sort(archiveLogFolder.getChildren());
			Collections.sort(activeLogFolder.getChildren());
		}
		databaseInfo.setDbSpaceInfoList(dbSpaceInfo);
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
