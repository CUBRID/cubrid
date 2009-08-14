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
package com.cubrid.cubridmanager.ui.cubrid.user.editor;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.PartInitException;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassAuthorizations;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllPartitionClassTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ClassType;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.ui.cubrid.user.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridEditorPart;
import com.cubrid.cubridmanager.ui.spi.TableContentProvider;
import com.cubrid.cubridmanager.ui.spi.TableLabelProvider;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJob;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJobExecutor;

/**
 * 
 * The editor of showed the user information
 * 
 * @author robin 2009-7-14
 */
public class UserEditor extends
		CubridEditorPart {
	public static final String ID = "com.cubrid.cubridmanager.ui.cubrid.user.editor.UserEditor";

	private List<Map<String, Object>> authListData = new ArrayList<Map<String, Object>>();
	private TableViewer authTableViewer;
	public final static String DB_DEFAULT_USERNAME = "public";
	public final static String DB_DBA_USERNAME = "dba";
	private String userName;
	private CubridDatabase database;
	private ICubridNode node;
	private Composite top = null;
	private CLabel lblUser = null;
	private DbUserInfo userInfo = null;
	private CLabel lblUserGroup = null;
	private CLabel lblUserMember = null;
	private List<ClassInfo> allClassInfoList;
	private Map<String, String> partitionClassMap;
	private DbUserInfoList userListInfo;
	private Map<String, String> classGrantMap;
	private List<Map<String, String>> ownerClassListData = new ArrayList<Map<String, String>>();
	private TableViewer ownerClassTableViewer;
	private List<String> memberList = new ArrayList<String>();
	private List<String> groupList = new ArrayList<String>();
	private Composite ownerComp;
	private Composite authComp;

	@Override
	public void init(IEditorSite site, IEditorInput input) throws PartInitException {
		super.init(site, input);
		if (input instanceof DefaultSchemaNode) {
			node = (DefaultSchemaNode) input;
			if (null == node || node.getType() != CubridNodeType.USER) {
				return;
			}
			userName = node.getLabel().trim();
			database = ((DefaultSchemaNode) node).getDatabase();
			this.setTitleImage(node.getImageDescriptor().createImage());
			this.setPartName(userName);
		}
	}

	public void createPartControl(Composite parent) {
		GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = 5;
		gridLayout.marginWidth = 5;
		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);
		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setFont(new Font(top.getDisplay(),
				lblUser.getFont().toString(), 14, SWT.BOLD));
		lblUser.setBackground(top.getBackground());
		lblUser.setLayoutData(gridData);
		lblUser.setText(userName);

		lblUserGroup = new CLabel(top, SWT.NONE);
		lblUserGroup.setBackground(top.getBackground());
		lblUserGroup.setText("                                         ");

		lblUserMember = new CLabel(top, SWT.NONE);
		lblUserMember.setBackground(top.getBackground());
		lblUserMember.setText("                                         ");

		GridData gridData1 = new GridData(GridData.FILL_HORIZONTAL);
		gridData1.heightHint = 4;

		CLabel lbl = new CLabel(top, SWT.SHADOW_IN);
		lbl.setLayoutData(gridData1);

		lbl = new CLabel(top, SWT.NONE);
		lbl.setBackground(top.getBackground());
		lbl.setText(Messages.lblOwnerClassList);
		ownerComp = new Composite(top, SWT.NONE);
		ownerComp.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 1;
		layout.marginHeight = 1;

		ownerComp.setLayout(layout);
		final String[] columnNameArr = new String[] {
				Messages.tblColOwnerClassName, Messages.tblColOwnerClassSchema,
				Messages.tblColOwnerClassType };
		ownerClassTableViewer = CommonTool.createCommonTableViewer(ownerComp,
				new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		ownerClassTableViewer.setInput(ownerClassListData);

		lbl = new CLabel(top, SWT.SHADOW_IN);
		lbl.setLayoutData(gridData1);

		lbl = new CLabel(top, SWT.NONE);
		lbl.setBackground(top.getBackground());
		lbl.setText(Messages.lblAuthorizationList);
		authComp = new Composite(top, SWT.NONE);
		authComp.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		layout.marginWidth = 1;
		layout.marginHeight = 1;
		authComp.setLayout(layout);
		authComp.setBackground(top.getBackground());
		if (DB_DBA_USERNAME.equalsIgnoreCase(userName)) {
			CLabel clbl = new CLabel(authComp, SWT.NONE);
			clbl.setBackground(top.getBackground());
			clbl.setText(Messages.lblDbaAllAuth);
		} else {

			final String[] authColumnNameArr = new String[] {
					Messages.tblColAuthTable, Messages.tblColAuthSelect,
					Messages.tblColAuthInsert, Messages.tblColAuthUpdate,
					Messages.tblColAuthDelete, Messages.tblColAuthAlter,
					Messages.tblColAuthIndex, Messages.tblColAuthExecute,
					Messages.tblColAuthGrantselect,
					Messages.tblColAuthGrantinsert,
					Messages.tblColAuthGrantupdate,
					Messages.tblColAuthGrantdelete,
					Messages.tblColAuthGrantalter,
					Messages.tblColAuthGrantindex,
					Messages.tblColAuthGrantexecute

			};
			authTableViewer = createCommonTableViewer(authComp,
					authColumnNameArr, CommonTool.createGridData(
							GridData.FILL_BOTH, 3, 1, -1, 200));
			authTableViewer.setLabelProvider(new MyTableLabelProvider());
			authTableViewer.setInput(authListData);
		}
		loadData();
	}

	private void initial() {
		while (memberList.size() > 0)
			memberList.remove(0);
		for (DbUserInfo bean : userListInfo.getUserList()) {
			if (bean.getName().equalsIgnoreCase(userName)) {
				userInfo = bean;
			}
			List<String> groups = bean.getGroups().getGroup();
			if (groups != null)
				for (String g : groups) {
					if (userName != null && userName.equalsIgnoreCase(g)) {
						memberList.add(bean.getName());
						break;
					}
				}
		}
		groupList = userInfo.getGroups().getGroup();
		while (ownerClassListData.size() > 0)
			ownerClassListData.remove(0);
		if (allClassInfoList != null)
			for (ClassInfo c : allClassInfoList) {
				if (c.getOwnerName().equalsIgnoreCase(userInfo.getName())) {
					Map<String, String> map = new HashMap<String, String>();
					map.put("0", c.getClassName());
					map.put("1", c.isSystemClass() ? Messages.msgSystemSchema
							: Messages.msgUserSchema);
					map.put(
							"2",
							c.getClassType() == ClassType.VIEW ? Messages.msgVirtualClass
									: Messages.msgClass);
					ownerClassListData.add(map);
				}
			}
		while (authListData.size() > 0)
			authListData.remove(0);

		classGrantMap = userInfo.getAuthorization();
		Iterator<String> authIter = classGrantMap.keySet().iterator();
		while (authIter.hasNext()) {
			String className = authIter.next();
			if (!partitionClassMap.containsKey(className)) {
				String authNum = classGrantMap.get(className);
				authListData.add(getItemAuthMap(new ClassAuthorizations(
						className, CommonTool.str2Int(authNum))));
			}
		}
		ownerClassTableViewer.refresh();
		if (!DB_DBA_USERNAME.equalsIgnoreCase(userName))
			authTableViewer.refresh();
		for (int i = 0; i < ownerClassTableViewer.getTable().getColumnCount(); i++)
			ownerClassTableViewer.getTable().getColumn(i).pack();
		if (!DB_DBA_USERNAME.equalsIgnoreCase(userName))
			for (int i = 0; i < authTableViewer.getTable().getColumnCount(); i++)
				authTableViewer.getTable().getColumn(i).pack();

		StringBuffer sb = new StringBuffer();
		if (groupList != null)
			for (int i = 0, n = groupList.size(); i < n; i++) {
				if (i > 0)
					sb.append(", ");
				sb.append(groupList.get(i));
			}
		lblUserGroup.setText(Messages.bind(Messages.lblGroupList,
				sb.length() < 1 ? Messages.lblGroupNotExist : sb.toString()));
		sb = new StringBuffer();
		if (memberList != null)
			for (int i = 0, n = memberList.size(); i < n; i++) {
				if (i > 0)
					sb.append(", ");
				sb.append(memberList.get(i));
			}

		lblUserMember.setText(Messages.bind(Messages.lblMemberList,
				sb.length() < 1 ? Messages.lblMemberNotExist : sb.toString()));
	}

	public boolean loadData() {
		final CommonQueryTask<DbUserInfoList> userTask = new CommonQueryTask<DbUserInfoList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, new DbUserInfoList());
		userTask.setDbName(database.getName());
		final GetAllClassListTask classInfoTask = new GetAllClassListTask(
				database.getDatabaseInfo());
		final GetAllPartitionClassTask partitionTask = new GetAllPartitionClassTask(
				database.getDatabaseInfo());
		TaskJobExecutor taskJobExecutor = new TaskJobExecutor() {
			@SuppressWarnings("unchecked")
			@Override
			public IStatus exec(IProgressMonitor monitor) {

				if (monitor.isCanceled()) {
					return Status.CANCEL_STATUS;
				}
				for (ITask t : taskList) {
					if (t instanceof GetAllClassListTask) {
						allClassInfoList = ((GetAllClassListTask) t).getAllClassInfoList();
					} else
						t.execute();
					if (t instanceof CommonQueryTask) {
						userListInfo = ((CommonQueryTask<DbUserInfoList>) t).getResultModel();
					}
					if (t instanceof GetAllPartitionClassTask) {
						partitionClassMap = ((GetAllPartitionClassTask) t).getPartitionClassMap();
					}
					final String msg = t.getErrorMsg();

					if (monitor.isCanceled()) {
						return Status.CANCEL_STATUS;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						Display.getDefault().syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(msg);

							}
						});
						return Status.CANCEL_STATUS;
					}
					if (monitor.isCanceled()) {
						return Status.CANCEL_STATUS;
					}
				}
				Display.getDefault().syncExec(new Runnable() {
					public void run() {
						initial();
					}
				});

				return Status.OK_STATUS;
			}

		};
		taskJobExecutor.addTask(userTask);
		taskJobExecutor.addTask(classInfoTask);
		taskJobExecutor.addTask(partitionTask);
		TaskJob job = new TaskJob(Messages.msgTaskJobName, taskJobExecutor);
		job.setUser(false);
		job.schedule();
		return true;

	}

	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode eventNode = e.getCubridNode();
		if (eventNode == null
				|| e.getType() != CubridNodeChangedEventType.CONTAINER_NODE_REFRESH) {
			return;
		}
		if (eventNode.getChild(node != null ? node.getId() : "") == null) {
			return;
		}
		loadData();
	}

	@Override
	public void doSave(IProgressMonitor monitor) {
		firePropertyChange(PROP_DIRTY);
	}

	@Override
	public void doSaveAs() {

	}

	@Override
	public boolean isDirty() {
		return false;
	}

	@Override
	public boolean isSaveAsAllowed() {
		return false;
	}

	@Override
	public void setFocus() {

	}

	/**
	 * Get item map
	 * 
	 * @param auth
	 * @return
	 */
	private Map<String, Object> getItemAuthMap(ClassAuthorizations auth) {
		Map<String, Object> map = new HashMap<String, Object>();
		map.put("0", auth.getClassName());
		map.put("1", auth.isSelectPriv());
		map.put("2", auth.isInsertPriv());
		map.put("3", auth.isUpdatePriv());
		map.put("4", auth.isDeletePriv());
		map.put("5", auth.isAlterPriv());
		map.put("6", auth.isIndexPriv());
		map.put("7", auth.isExecutePriv());
		map.put("8", auth.isGrantSelectPriv());
		map.put("9", auth.isGrantInsertPriv());
		map.put("10", auth.isGrantUpdatePriv());
		map.put("11", auth.isGrantDeletePriv());
		map.put("12", auth.isGrantAlterPriv());
		map.put("13", auth.isGrantIndexPriv());
		map.put("14", auth.isGrantExecutePriv());
		return map;
	}

	/**
	 * Create common tableViewer
	 * 
	 * @param parent
	 * @param columnNameArr
	 * @param gridData
	 * @return
	 */
	public TableViewer createCommonTableViewer(Composite parent,
			final String[] columnNameArr, GridData gridData) {
		final TableViewer tableViewer = new TableViewer(parent, SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER | SWT.H_SCROLL | SWT.FULL_SELECTION);
		tableViewer.setContentProvider(new TableContentProvider());
		tableViewer.setLabelProvider(new MyTableLabelProvider());
		tableViewer.setSorter(new TableViewerSorter());

		tableViewer.getTable().setLinesVisible(true);
		tableViewer.getTable().setHeaderVisible(true);

		tableViewer.getTable().setLayoutData(gridData);

		for (int i = 0; i < columnNameArr.length; i++) {
			final TableColumn tblColumn = new TableColumn(
					tableViewer.getTable(), SWT.CHECK);
			tblColumn.setData(false);
			tblColumn.setText(columnNameArr[i]);
			tblColumn.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					TableColumn column = (TableColumn) event.widget;

					int j = 0;
					for (j = 0; j < columnNameArr.length; j++) {
						if (column.getText().equals(columnNameArr[j])) {
							break;
						}
					}
					TableViewerSorter sorter = ((TableViewerSorter) tableViewer.getSorter());
					if (sorter == null) {
						return;
					}
					sorter.doSort(j);
					tableViewer.getTable().setSortColumn(column);
					tableViewer.getTable().setSortDirection(
							sorter.isAsc() ? SWT.UP : SWT.DOWN);
					tableViewer.refresh();
					for (int k = 0; k < tableViewer.getTable().getColumnCount(); k++) {
						tableViewer.getTable().getColumn(k).pack();
					}
					return;

				}
			});

			tblColumn.pack();
		}
		return tableViewer;
	}

	/**
	 * 
	 * The provider is get table colume image
	 * 
	 * @author robin 2009-6-4
	 */
	static class MyTableLabelProvider extends
			TableLabelProvider {

		@SuppressWarnings("unchecked")
		@Override
		public Image getColumnImage(Object element, int columnIndex) {
			return null;
		}

		@SuppressWarnings("unchecked")
		@Override
		public String getColumnText(Object element, int columnIndex) {
			if (!(element instanceof Map)) {
				return "";
			}
			Map<String, Object> map = (Map<String, Object>) element;
			if (columnIndex == 0) {
				return map.get("" + columnIndex).toString();
			} else {
				Boolean val = (Boolean) map.get("" + columnIndex);
				return val ? "Y" : "N";
			}
		}

		@Override
		public boolean isLabelProperty(Object element, String property) {
			return true;
		}
	}

	public boolean isSystemClass(String name) {
		if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
			return false;
		for (ClassInfo bean : allClassInfoList) {
			if (bean.getClassName().equals(name) && bean.isSystemClass()) {
				return false;
			}
		}
		return true;
	}
}
