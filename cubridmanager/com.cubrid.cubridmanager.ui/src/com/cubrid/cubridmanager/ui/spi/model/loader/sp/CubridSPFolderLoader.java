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
package com.cubrid.cubridmanager.ui.spi.model.loader.sp;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
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
 * This class is responsible to load all stored procedure
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class CubridSPFolderLoader extends
		CubridNodeLoader {

	private static final String FUNCTION_FOLDER_NAME = Messages.msgFunctionFolderName;
	private static final String PROCEDURE_FOLDER_NAME = Messages.msgProcedureFolderName;

	public static final String FUNCTION_FOLDER_ID = "Function";
	public static final String PROCEDURE_FOLDER_ID = "Procedure";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		CubridDatabase database = ((ISchemaNode) parent).getDatabase();
		if (!database.isLogined()
				|| database.getRunningType() == DbRunningType.STANDALONE) {
			parent.removeAllChild();
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent((ICubridNode) parent,
							CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
			return;
		}
		// add function folder
		String functionFolderId = parent.getId() + NODE_SEPARATOR
				+ FUNCTION_FOLDER_ID;
		ICubridNode functionFolder = parent.getChild(functionFolderId);
		if (functionFolder == null) {
			functionFolder = new DefaultSchemaNode(functionFolderId,
					FUNCTION_FOLDER_NAME, "icons/navigator/folder.png");
			functionFolder.setType(CubridNodeType.STORED_PROCEDURE_FUNCTION_FOLDER);
			functionFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridFunctionFolderLoader();
			loader.setLevel(getLevel());
			functionFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				functionFolder.getChildren(monitor);
			}
			parent.addChild(functionFolder);
		} else {
			if (functionFolder.getLoader() != null
					&& functionFolder.getLoader().isLoaded()) {
				functionFolder.getLoader().setLoaded(false);
				functionFolder.getChildren(monitor);
			}
		}
		// add procedure folder
		String procedureFolderId = parent.getId() + NODE_SEPARATOR
				+ PROCEDURE_FOLDER_ID;
		ICubridNode procedureFolder = parent.getChild(procedureFolderId);
		if (procedureFolder == null) {
			procedureFolder = new DefaultSchemaNode(procedureFolderId,
					PROCEDURE_FOLDER_NAME, "icons/navigator/folder.png");
			procedureFolder.setType(CubridNodeType.STORED_PROCEDURE_PROCEDURE_FOLDER);
			procedureFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridProcedureFolderLoader();
			loader.setLevel(getLevel());
			procedureFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				procedureFolder.getChildren(monitor);
			}
			parent.addChild(procedureFolder);
		} else {
			if (procedureFolder.getLoader() != null
					&& procedureFolder.getLoader().isLoaded()) {
				procedureFolder.getLoader().setLoaded(false);
				procedureFolder.getChildren(monitor);
			}
		}
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
