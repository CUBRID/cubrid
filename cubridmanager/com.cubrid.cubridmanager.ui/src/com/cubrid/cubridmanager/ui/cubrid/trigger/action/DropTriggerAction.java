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
package com.cubrid.cubridmanager.ui.cubrid.trigger.action;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.trigger.task.DropTriggerTask;
import com.cubrid.cubridmanager.ui.cubrid.trigger.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

public class DropTriggerAction extends
		SelectionAction {

	public static final String ID = DropTriggerAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DropTriggerAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DropTriggerAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, null, text, icon);
		this.setId(ID);
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
		if (obj instanceof DefaultSchemaNode) {
			return true;
		}
		return false;
	}

	public void run() {
		Object[] objects = this.getSelectedObj();
		if (!isSupported(objects[0])) {
			setEnabled(false);
			return;
		}
		List<String> triggerNameList = new ArrayList<String>();
		StringBuffer bf = new StringBuffer();
		for (Object obj : objects) {
			DefaultSchemaNode trigger = (DefaultSchemaNode) obj;
			triggerNameList.add(trigger.getName());
			bf.append(",\"").append(trigger.getName()).append("\"");
		}
		String droppedTriggers = bf.substring(1);
		String msg = null;
		if (triggerNameList.size() == 1) {
			msg = Messages.bind(Messages.dropTriggerWarnMSG1, droppedTriggers);
		} else {
			msg = Messages.bind(Messages.dropTriggerWarnMSG2, droppedTriggers);
		}

		boolean ret = CommonTool.openConfirmBox(msg);
		if (!ret) {
			return;
		}

		DefaultSchemaNode trigger = (DefaultSchemaNode) objects[0];
		ServerInfo serverInfo = trigger.getServer().getServerInfo();
		String dbName = trigger.getDatabase().getName();

		TaskExecutor taskExecutor = new CommonTaskExec();
		for (String triggerName : triggerNameList) {
			DropTriggerTask task = new DropTriggerTask(serverInfo);
			task.setDbName(dbName);
			task.setTriggerName(triggerName);
			taskExecutor.addTask(task);
		}

		new ExecTaskWithProgress(taskExecutor).exec();
		ISelectionProvider provider = this.getSelectionProvider();
		final TreeViewer viewer = (TreeViewer) provider;
		CommonTool.refreshNavigatorTree(viewer, trigger.getParent());

	}
}
