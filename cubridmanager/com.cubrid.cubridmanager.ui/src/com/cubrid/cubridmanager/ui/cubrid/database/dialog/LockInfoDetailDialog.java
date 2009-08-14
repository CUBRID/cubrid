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
package com.cubrid.cubridmanager.ui.cubrid.database.dialog;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;

import com.cubrid.cubridmanager.core.cubrid.database.model.lock.BlockedHolders;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DbLotEntry;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockHolders;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockWaiters;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Lock information of detailed
 * 
 * @author robin 2009-3-11
 */
public class LockInfoDetailDialog extends
		CMTitleAreaDialog {

	private Table lockWaiterTable;
	private List<Map<String, String>> lockWaiterListData = new ArrayList<Map<String, String>>();
	private TableViewer lockWaiterTableViewer;

	private Table blockedHolderTable;
	private List<Map<String, String>> blockedHolderListData = new ArrayList<Map<String, String>>();
	private TableViewer blockedHolderTableViewer;

	private Table lockHolderTable;
	private List<Map<String, String>> lockHolderListData = new ArrayList<Map<String, String>>();
	private TableViewer lockHolderTableViewer;

	private Composite parentComp = null;
	private CubridDatabase database = null;
	private DbLotEntry dbLotEntry;
	private Label objectIdLabel;
	private Label objectTypeLabel;

	/**
	 * 
	 * @param parentShell
	 */
	public LockInfoDetailDialog(Shell parentShell) {
		super(parentShell);

	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);

		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayout(new GridLayout());
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createTitleInfo(composite);
		createLockHoldersGroup(composite);
		createBlockedLockHolersGroupGroup(composite);
		createLockWaitersLabel(composite);

		setTitle(Messages.titleLockInfoDetailDialog);
		setMessage(Messages.msgLockInfoDetailDialog);

		initial();
		return parentComp;
	}

	private void createTitleInfo(Composite composite) {
		objectIdLabel = new Label(composite, SWT.NONE);
		objectTypeLabel = new Label(composite, SWT.NONE);
	}

	private void createLockHoldersGroup(Composite composite) {
		final Group lockHoldersGroup = new Group(composite, SWT.NONE);
		lockHoldersGroup.setText(Messages.grpLockHolders);
		lockHoldersGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout(1, true);
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		lockHoldersGroup.setLayout(layout);

		final String[] columnNameArr = new String[] { Messages.tblColTranIndex,
				Messages.tblColGrantedMode, Messages.tblColCount,
				Messages.tblColNsubgranules };
		lockHolderTableViewer = CommonTool.createCommonTableViewer(
				lockHoldersGroup, new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		lockHolderTable = lockHolderTableViewer.getTable();
		lockHolderTableViewer.setInput(lockHolderListData);
	}

	private void createBlockedLockHolersGroupGroup(Composite composite) {
		final Group blockedLockHolersGroup = new Group(composite, SWT.NONE);
		blockedLockHolersGroup.setText(Messages.grpBlockedHolder);
		blockedLockHolersGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		blockedLockHolersGroup.setLayout(layout);

		final String[] columnNameArr = new String[] {
				Messages.tblColLockTranIndex, Messages.tblColLockGrantedMode,
				Messages.tblColLockCount, Messages.tblColLockBlockedMode,
				Messages.tblColLockStartWaitingAt,
				Messages.tblColLockWaitForNsecs };
		blockedHolderTableViewer = CommonTool.createCommonTableViewer(
				blockedLockHolersGroup, new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		blockedHolderTable = blockedHolderTableViewer.getTable();
		blockedHolderTableViewer.setInput(blockedHolderListData);

	}

	private void createLockWaitersLabel(Composite composite) {
		final Group lockWaitersGroup = new Group(composite, SWT.NONE);
		lockWaitersGroup.setText(Messages.grpLockWaiter);
		lockWaitersGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		lockWaitersGroup.setLayout(layout);
		final String[] columnNameArr = new String[] {

		Messages.tblColWaiterTranIndex, Messages.tblColWaiterBlockedMode,
				Messages.tblColWaiterStartWaitingAt,
				Messages.tblColWaiterWaitForNsecs };
		lockWaiterTableViewer = CommonTool.createCommonTableViewer(
				lockWaitersGroup, new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		lockWaiterTable = lockWaiterTableViewer.getTable();
		lockWaiterTableViewer.setInput(lockWaiterListData);

	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setSize(500, 600);
		getShell().setText(Messages.titleLockInfoDetailDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
			} else {
				return;
			}

		}
		super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {

		if (dbLotEntry == null)
			return;
		if (dbLotEntry.getLockHoldersList() != null) {

			for (LockHolders bean : dbLotEntry.getLockHoldersList()) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", String.valueOf(bean.getTran_index()));
				map.put("1", bean.getGranted_mode());
				map.put("2", String.valueOf(bean.getCount()));
				map.put("3", String.valueOf(bean.getNsubgranules()));
				lockHolderListData.add(map);
			}
			lockHolderTableViewer.refresh();
			for (int i = 0; i < lockHolderTable.getColumnCount(); i++) {
				lockHolderTable.getColumn(i).pack();
			}
		}
		if (dbLotEntry.getBlockHoldersList() != null) {
			for (BlockedHolders bean : dbLotEntry.getBlockHoldersList()) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", String.valueOf(bean.getTran_index()));
				map.put("1", bean.getGranted_mode());
				map.put("2", String.valueOf(bean.getCount()));
				map.put("3", String.valueOf(bean.getBlocked_mode()));
				map.put("4", String.valueOf(bean.getStart_at()));
				map.put("5", String.valueOf(bean.getWait_for_sec()));
				blockedHolderListData.add(map);
			}
			blockedHolderTableViewer.refresh();
			for (int i = 0; i < blockedHolderTable.getColumnCount(); i++) {
				blockedHolderTable.getColumn(i).pack();
			}
		}
		if (dbLotEntry.getLockWaitersList() != null) {
			for (LockWaiters bean : dbLotEntry.getLockWaitersList()) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", String.valueOf(bean.getTran_index()));
				map.put("1", bean.getB_mode());
				map.put("2", String.valueOf(bean.getStart_at()));
				map.put("3", String.valueOf(bean.getWaitfornsec()));
				lockWaiterListData.add(map);
			}
			lockWaiterTableViewer.refresh();
			for (int i = 0; i < lockWaiterTable.getColumnCount(); i++) {
				lockWaiterTable.getColumn(i).pack();
			}
		}
		objectIdLabel.setText(Messages.bind(Messages.lblObjectId , dbLotEntry.getOid()));
		objectTypeLabel.setText(Messages.bind(Messages.lblObjectType , dbLotEntry.getOb_type()));

	}

	/**
	 * 
	 * Check the data validation
	 * 
	 * @return
	 */
	public boolean valid() {
		return true;

	}

	/**
	 * 
	 * Get added CubridDatabase
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * 
	 * Set edited CubridDatabase
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	public DbLotEntry getDbLotEntry() {
		return dbLotEntry;
	}

	public void setDbLotEntry(DbLotEntry dbLotEntry) {
		this.dbLotEntry = dbLotEntry;
	}
}
