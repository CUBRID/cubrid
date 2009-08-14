/*
 * Copyright (C) 2009 NHN Corporation. All rights reserved by NHN.
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

package com.cubrid.cubridmanager.ui.cubrid.jobauto.action;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfoHelp;
import com.cubrid.cubridmanager.core.cubrid.jobauto.task.SetQueryPlanListTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This is an action to listen to deleting query plan selection event and
 * execute the operation of deleting query plan.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-12 created by lizhiqiang
 */
public class DeleteQueryPlanAction extends
		SelectionAction {

	public static final String ID = DeleteQueryPlanAction.class.getName();

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DeleteQueryPlanAction(Shell shell, String text, ImageDescriptor icon) {
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
	protected DeleteQueryPlanAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.common.action.deletequeryplan");
	}

	public void run() {
		Object[] objs = this.getSelectedObj();
		assert (objs != null);
		List<String> nodeNames = new ArrayList<String>();
		for (Object obj : objs) {
			ICubridNode selection = (ICubridNode) obj;
			nodeNames.add(selection.getLabel());
		}
		if (!CommonTool.openConfirmBox(Messages.bind(
				Messages.delQueryPlanConfirmContent, nodeNames)))
			return;

		CubridDatabase database = null;
		for (Object obj : objs) {
			if (obj instanceof DefaultSchemaNode) {
				database = ((DefaultSchemaNode) obj).getDatabase();
			}

			TreeViewer treeViewer = (TreeViewer) getSelectionProvider();
			DefaultSchemaNode delNode = ((DefaultSchemaNode) obj);
			ICubridNode parentNode = delNode.getParent();
			parentNode.removeChild(delNode);
			treeViewer.remove(delNode);
		}
		if (database == null) {
			CommonTool.openErrorBox(Messages.msgSelectDB);
			return;
		}
		ICubridNode parentSelection = ((DefaultSchemaNode) objs[0]).getParent();
		List<ICubridNode> list = parentSelection.getChildren();
		List<String> msgList = new ArrayList<String>();
		QueryPlanInfo queryPlan = null;
		QueryPlanInfoHelp qHelp = new QueryPlanInfoHelp();
		for (ICubridNode icn : list) {
			if (!icn.isContainer()) {
				queryPlan = (QueryPlanInfo) icn.getAdapter(QueryPlanInfo.class);
				qHelp.setQueryPlanInfo(queryPlan);
				msgList.add(qHelp.buildMsg());
			}
		}

		ServerInfo serverInfo = database.getServer().getServerInfo();
		SetQueryPlanListTask task = new SetQueryPlanListTask(serverInfo);
		task.setDbname(database.getName());
		task.buildMsg(msgList);

		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();

	}

	/**
	 * Makes this action not support to select multi object
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return true;
	}

	/**
	 * Return whether this action support this object,if not support,this action
	 * will be disabled
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ISchemaNode) {
			ISchemaNode node = (ISchemaNode) obj;
			CubridDatabase database = node.getDatabase();
			if (node.getType() == CubridNodeType.QUERY_PLAN && database != null
					&& database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (dbUserInfo != null && dbUserInfo.isDbaAuthority())
					return true;
			}
		}
		return false;
	}

}
