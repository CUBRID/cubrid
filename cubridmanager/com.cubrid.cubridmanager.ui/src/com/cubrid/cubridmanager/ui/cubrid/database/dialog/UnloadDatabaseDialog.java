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

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckDirTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckFileTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.UnloadDatabaseTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * Unload database will use this dialog to fill in the information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class UnloadDatabaseDialog extends
		CMTitleAreaDialog implements
		ModifyListener {

	private Text databaseNameText = null;
	private CubridDatabase database = null;
	private List<String> allUserClassList = null;
	private Text targetDirText;
	private Button allSchemaButton;
	private Button selectedSchemaButton;
	private Button noSchemaButton;
	private Button selectedDataButton;
	private Button noDataButton;
	private Table schemaTable;
	private Button useTempHashFileButton;
	private Text hashFileText;
	private Button outputPrefixButton;
	private Text prefixText;
	private Button includeRefButton;
	private Button delimitedButton;
	private Button estimatedSizeButton;
	private Text estimatedSizeText;
	private Button cachedPageButton;
	private Text cachedPageText;
	private Button loFileCountButton;
	private Text loFileCountText;
	private Button selectTargetDirectoryButton;
	private Button selectHashFileButton;
	private boolean isCanFinished = true;
	private boolean isSchemaOnly = true;
	private String dbDir;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public UnloadDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseUnload);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createDatabaseInfoGroup(composite);
		createUnloadTargetInfoGroup(composite);
		createUnloadOptionGroup(composite);

		setTitle(Messages.titleUnloadDbDialog);
		setMessage(Messages.msgUnloadDbDialog);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create target database information group
	 * 
	 * @param parent
	 */
	private void createDatabaseInfoGroup(Composite parent) {
		Group databaseInfoGroup = new Group(parent, SWT.NONE);
		databaseInfoGroup.setText(Messages.grpDbInfo);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		databaseInfoGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		databaseInfoGroup.setLayout(layout);

		Label databaseNameLabel = new Label(databaseInfoGroup, SWT.LEFT
				| SWT.WRAP);
		databaseNameLabel.setText(Messages.lblTargetDbName);
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		databaseNameText = new Text(databaseInfoGroup, SWT.BORDER);
		databaseNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		databaseNameText.setEditable(false);

		Label targetDirLabel = new Label(databaseInfoGroup, SWT.LEFT
				| SWT.CHECK);
		targetDirLabel.setText(Messages.lblTargetDir);
		targetDirLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		targetDirText = new Text(databaseInfoGroup, SWT.BORDER);
		targetDirText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		selectTargetDirectoryButton = new Button(databaseInfoGroup, SWT.NONE);
		selectTargetDirectoryButton.setText(Messages.btnBrowse);
		selectTargetDirectoryButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectTargetDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				if (dbDir != null && dbDir.trim().length() > 0)
					dlg.setFilterPath(dbDir);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					targetDirText.setText(dir);
				}
			}
		});
		ServerInfo serverInfo = database.getServer().getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectTargetDirectoryButton.setEnabled(false);
		}
	}

	/**
	 * 
	 * Create unload target information group
	 * 
	 * @param parent
	 */
	private void createUnloadTargetInfoGroup(Composite parent) {
		Group unloadTargetGroup = new Group(parent, SWT.NONE);
		unloadTargetGroup.setText(Messages.grpUnloadTarget);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		unloadTargetGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		unloadTargetGroup.setLayout(layout);

		Group schemaGroup = new Group(unloadTargetGroup, SWT.NONE);
		schemaGroup.setText(Messages.grpSchema);
		schemaGroup.setLayoutData(CommonTool.createGridData(GridData.FILL_BOTH,
				2, 1, -1, -1));
		layout = new GridLayout();
		schemaGroup.setLayout(layout);

		allSchemaButton = new Button(schemaGroup, SWT.RADIO | SWT.LEFT);
		allSchemaButton.setText(Messages.btnAll);
		allSchemaButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (allSchemaButton.getSelection()) {
					for (int i = 0, n = schemaTable.getItemCount(); i < n; i++) {
						schemaTable.getItem(i).setChecked(true);
					}
					includeRefButton.setEnabled(false);
				} else {
					for (int i = 0, n = schemaTable.getItemCount(); i < n; i++) {
						schemaTable.getItem(i).setChecked(false);
					}
				}
				valid();
			}
		});

		selectedSchemaButton = new Button(schemaGroup, SWT.RADIO | SWT.LEFT);
		selectedSchemaButton.setText(Messages.btnSelectedTables);
		selectedSchemaButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
				includeRefButton.setEnabled(selectedSchemaButton.getSelection());
			}
		});

		noSchemaButton = new Button(schemaGroup, SWT.RADIO | SWT.LEFT);
		noSchemaButton.setText(Messages.btnNotInclude);
		noSchemaButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
				includeRefButton.setEnabled(false);
			}
		});

		Group dataGroup = new Group(unloadTargetGroup, SWT.NONE);
		dataGroup.setText(Messages.grpData);
		dataGroup.setLayoutData(CommonTool.createGridData(GridData.FILL_BOTH,
				2, 1, -1, -1));
		layout = new GridLayout();
		dataGroup.setLayout(layout);

		selectedDataButton = new Button(dataGroup, SWT.RADIO | SWT.LEFT);
		selectedDataButton.setText(Messages.btnSelectedTables);
		selectedDataButton.setSelection(true);
		selectedDataButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
			}
		});

		noDataButton = new Button(dataGroup, SWT.RADIO | SWT.LEFT);
		noDataButton.setText(Messages.btnNotInclude);
		noDataButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
			}
		});

		schemaTable = new Table(unloadTargetGroup, SWT.CHECK | SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER | SWT.H_SCROLL | SWT.FULL_SELECTION);
		schemaTable.setLinesVisible(false);
		schemaTable.setHeaderVisible(false);
		schemaTable.setLayoutData(CommonTool.createGridData(GridData.FILL_BOTH,
				4, 1, -1, 100));
		schemaTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
			}
		});

		if (this.allUserClassList != null) {
			for (int i = 0; i < allUserClassList.size(); i++) {
				TableItem item = new TableItem(schemaTable, SWT.MULTI);
				item.setText(allUserClassList.get(i));
			}
		}

		Composite composite = new Composite(unloadTargetGroup, SWT.NONE);
		RowLayout rowLayout = new RowLayout();
		rowLayout.spacing = 5;
		composite.setLayout(rowLayout);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 4;
		gridData.horizontalAlignment = GridData.END;
		composite.setLayoutData(gridData);
	}

	/**
	 * 
	 * Create unload option information group
	 * 
	 * @param parent
	 */
	private void createUnloadOptionGroup(Composite parent) {
		Group unloadOptionGroup = new Group(parent, SWT.NONE);
		unloadOptionGroup.setText(Messages.grpUnloadOption);
		unloadOptionGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		unloadOptionGroup.setLayout(layout);

		delimitedButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		delimitedButton.setText(Messages.btnUseDelimite);
		delimitedButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		includeRefButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		includeRefButton.setText(Messages.btnIncludeRef);
		includeRefButton.setEnabled(false);
		includeRefButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		outputPrefixButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		outputPrefixButton.setText(Messages.btnPrefix);
		outputPrefixButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		outputPrefixButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (outputPrefixButton.getSelection()) {
					prefixText.setText(database.getLabel());
					prefixText.setEnabled(true);
				} else {
					prefixText.setText("");
					prefixText.setEnabled(false);
				}
				valid();
			}
		});
		prefixText = new Text(unloadOptionGroup, SWT.BORDER);
		prefixText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		prefixText.setEnabled(false);

		useTempHashFileButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.CHECK);
		useTempHashFileButton.setText(Messages.btnHashFile);
		useTempHashFileButton.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		useTempHashFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (useTempHashFileButton.getSelection()) {
					hashFileText.setEnabled(true);
					ServerInfo serverInfo = database.getServer().getServerInfo();
					if (serverInfo != null && !serverInfo.isLocalServer()) {
						selectHashFileButton.setEnabled(false);
					} else {
						selectHashFileButton.setEnabled(true);
					}

				} else {
					hashFileText.setEnabled(false);
					selectHashFileButton.setEnabled(false);
				}
				valid();
			}
		});
		hashFileText = new Text(unloadOptionGroup, SWT.BORDER);
		hashFileText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		hashFileText.setEnabled(false);

		selectHashFileButton = new Button(unloadOptionGroup, SWT.NONE);
		selectHashFileButton.setText(Messages.btnBrowse);
		selectHashFileButton.setLayoutData(CommonTool.createGridData(1, 1, 80,
				-1));
		selectHashFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				if (dbDir != null && dbDir.trim().length() > 0)
					dlg.setFilterPath(dbDir);
				dlg.setText(Messages.msgSelectFile);
				String dir = dlg.open();
				if (dir != null) {
					hashFileText.setText(dir);
				}
			}
		});
		selectHashFileButton.setEnabled(false);

		cachedPageButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		cachedPageButton.setText(Messages.btnNumOfCachedPage);
		cachedPageButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		cachedPageButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (cachedPageButton.getSelection()) {
					cachedPageText.setText("100");
					cachedPageText.setEnabled(true);
				} else {
					cachedPageText.setText("");
					cachedPageText.setEnabled(false);
				}
				valid();
			}
		});

		cachedPageText = new Text(unloadOptionGroup, SWT.BORDER);
		cachedPageText.setTextLimit(8);
		cachedPageText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		cachedPageText.setEnabled(false);

		estimatedSizeButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.CHECK);
		estimatedSizeButton.setText(Messages.btnNumOfInstances);
		estimatedSizeButton.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		estimatedSizeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (estimatedSizeButton.getSelection()) {
					estimatedSizeText.setEnabled(true);
				} else {
					estimatedSizeText.setText("");
					estimatedSizeText.setEnabled(false);
				}
				valid();
			}
		});

		estimatedSizeText = new Text(unloadOptionGroup, SWT.BORDER);
		estimatedSizeText.setTextLimit(8);
		estimatedSizeText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		estimatedSizeText.setEnabled(false);

		loFileCountButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		loFileCountButton.setText(Messages.btnLoFileCount);
		loFileCountButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		loFileCountButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (loFileCountButton.getSelection()) {
					loFileCountText.setEnabled(true);
				} else {
					loFileCountText.setText("");
					loFileCountText.setEnabled(false);
				}
				valid();
			}
		});

		loFileCountText = new Text(unloadOptionGroup, SWT.BORDER);
		loFileCountText.setTextLimit(8);
		loFileCountText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		loFileCountText.setEnabled(false);

	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setText(Messages.titleUnloadDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		if (allUserClassList == null || allUserClassList.size() <= 0) {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
				unloadDatabase(buttonId);
			}
		} else {
			super.buttonPressed(buttonId);
		}
	}

	/**
	 * 
	 * Execute task and unload database
	 * 
	 * @param buttonId
	 */
	private void unloadDatabase(final int buttonId) {
		isCanFinished = true;
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						isCanFinished = false;
						return false;
					}
					if (!isCanFinished || monitor.isCanceled()) {
						return false;
					}
					if (task instanceof CheckDirTask) {
						CheckDirTask checkDirTask = (CheckDirTask) task;
						final String[] dirs = checkDirTask.getNoExistDirectory();
						if (dirs != null && dirs.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									CreateDirDialog dialog = new CreateDirDialog(
											getShell());
									dialog.setDirs(dirs);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isCanFinished = false;
									}
								}
							});
						}
					} else if (task instanceof CheckFileTask) {
						CheckFileTask checkFileTask = (CheckFileTask) task;
						final String[] files = checkFileTask.getExistFiles();
						if (files != null && files.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									OverrideFileDialog dialog = new OverrideFileDialog(
											getShell());
									dialog.setFiles(files);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isCanFinished = false;
									}
								}
							});
						}
					} else if (task instanceof UnloadDatabaseTask) {
						UnloadDatabaseTask unloadDatabaseTask = (UnloadDatabaseTask) task;
						if (isSchemaOnly) {
							display.syncExec(new Runnable() {
								public void run() {
									CommonTool.openInformationBox(getShell(),
											Messages.titleSuccess,
											Messages.msgSuccessUnload);
								}
							});
						} else {
							final List<String> list = unloadDatabaseTask.getUnloadDbResult();
							display.syncExec(new Runnable() {
								public void run() {
									UnloadDatabaseResultDialog dialog = new UnloadDatabaseResultDialog(
											getShell());
									dialog.setUnloadResulList(list);
									dialog.open();
								}
							});
						}
					}
					if (!isCanFinished || monitor.isCanceled()) {
						return false;
					}
				}
				if (!monitor.isCanceled()) {
					display.syncExec(new Runnable() {
						public void run() {
							setReturnCode(buttonId);
							close();
						}
					});
				}
				return true;
			}
		};
		CheckDirTask checkDirTask = new CheckDirTask(
				database.getServer().getServerInfo());
		CheckFileTask checkFileTask = new CheckFileTask(
				database.getServer().getServerInfo());
		UnloadDatabaseTask unloadDatabaseTask = new UnloadDatabaseTask(
				database.getServer().getServerInfo());
		fillTask(checkDirTask, checkFileTask, unloadDatabaseTask);
		taskExcutor.addTask(checkDirTask);
		taskExcutor.addTask(checkFileTask);
		taskExcutor.addTask(unloadDatabaseTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	/**
	 * 
	 * Fill in task information
	 * 
	 * @param checkDirTask
	 * @param checkFileTask
	 * @param unloadDatabaseTask
	 */
	private void fillTask(CheckDirTask checkDirTask,
			CheckFileTask checkFileTask, UnloadDatabaseTask unloadDatabaseTask) {
		String prefix = databaseNameText.getText();
		if (outputPrefixButton.getSelection()) {
			prefix = prefixText.getText();
		}
		String targetdir = targetDirText.getText()
				+ database.getServer().getServerInfo().getPathSeparator();
		isSchemaOnly = false;
		List<String> checkedDirs = new ArrayList<String>();
		List<String> checkedFiles = new ArrayList<String>();
		if (useTempHashFileButton.getSelection()) {
			String hashFile = hashFileText.getText();
			checkedFiles.add(hashFile);
		}
		checkedDirs.add(targetdir);
		List<String> unloadedClassList = new ArrayList<String>();
		if (allSchemaButton.getSelection()) {
			checkedFiles.add(targetdir + prefix + "_schema");
			checkedFiles.add(targetdir + prefix + "_indexes");
			checkedFiles.add(targetdir + prefix + "_trigger");
			unloadDatabaseTask.setClassOnly(false);
			if (selectedDataButton.getSelection()) {
				int count = getSelectedClassesCount();
				if (count == 1 || count == -1) {
					checkedFiles.add(targetdir + prefix + "_objects");
					unloadDatabaseTask.setUnloadType("both");
				} else {
					unloadDatabaseTask.setUnloadType("schema");
					isSchemaOnly = true;
				}
				if (count == -1) {
					makeClassList(unloadedClassList);
				}
			} else {
				isSchemaOnly = true;
				unloadDatabaseTask.setUnloadType("schema");
			}
		} else if (selectedSchemaButton.getSelection()) {
			checkedFiles.add(targetdir + prefix + "_schema");
			checkedFiles.add(targetdir + prefix + "_indexes");
			checkedFiles.add(targetdir + prefix + "_trigger");
			unloadDatabaseTask.setClassOnly(true);
			makeClassList(unloadedClassList);
			if (selectedDataButton.getSelection()) {
				checkedFiles.add(targetdir + prefix + "_objects");
				unloadDatabaseTask.setUnloadType("both");
			} else {
				isSchemaOnly = true;
				unloadDatabaseTask.setUnloadType("schema");
			}
		} else if (noSchemaButton.getSelection()) {
			unloadDatabaseTask.setClassOnly(true);
			if (selectedDataButton.getSelection()) {
				checkedFiles.add(targetdir + prefix + "_objects");
				unloadDatabaseTask.setUnloadType("object");
				makeClassList(unloadedClassList);
			}
		}
		String[] dirs = new String[checkedDirs.size()];
		checkDirTask.setDirectory(checkedDirs.toArray(dirs));

		String[] files = new String[checkedFiles.size()];
		checkFileTask.setFile(checkedFiles.toArray(files));

		unloadDatabaseTask.setDbName(databaseNameText.getText());
		unloadDatabaseTask.setUnloadDir(targetDirText.getText());
		if (useTempHashFileButton.getSelection()) {
			String hashFile = hashFileText.getText();
			unloadDatabaseTask.setUsedHash(true, hashFile);
		} else {
			unloadDatabaseTask.setUsedHash(false, "");
		}
		if (outputPrefixButton.getSelection()) {
			unloadDatabaseTask.setUsedPrefix(true, prefixText.getText());
		} else {
			unloadDatabaseTask.setUsedPrefix(false, "");
		}
		if (includeRefButton.getSelection()) {
			unloadDatabaseTask.setIncludeRef(true);
		} else {
			unloadDatabaseTask.setIncludeRef(false);
		}
		if (delimitedButton.getSelection()) {
			unloadDatabaseTask.setUsedDelimit(true);
		} else {
			unloadDatabaseTask.setUsedDelimit(false);
		}
		if (estimatedSizeButton.getSelection()) {
			unloadDatabaseTask.setUsedEstimate(true,
					estimatedSizeText.getText());
		} else {
			unloadDatabaseTask.setUsedEstimate(false, "");
		}
		if (cachedPageButton.getSelection()) {
			unloadDatabaseTask.setUsedCache(true, cachedPageText.getText());
		} else {
			unloadDatabaseTask.setUsedCache(false, "");
		}
		if (loFileCountButton.getSelection()) {
			unloadDatabaseTask.setUsedLoFile(true, loFileCountText.getText());
		} else {
			unloadDatabaseTask.setUsedLoFile(false, "");
		}

		if (unloadedClassList.size() > 0) {
			String[] unloadedClasses = new String[unloadedClassList.size()];
			unloadDatabaseTask.setClasses(unloadedClassList.toArray(unloadedClasses));
		}
	}

	/**
	 * 
	 * Make unloaded class list
	 * 
	 * @param unloadedClassList
	 */
	private void makeClassList(List<String> unloadedClassList) {
		for (int i = 0; i < schemaTable.getItemCount(); i++) {
			TableItem tableItem = schemaTable.getItem(i);
			if (tableItem.getChecked()) {
				unloadedClassList.add(tableItem.getText(0));
			}
		}
	}

	/**
	 * Selected class count
	 * 
	 * @return -1: something selected, 0: nothing selected, 1: all selected
	 */
	private int getSelectedClassesCount() {
		if (schemaTable.getItemCount() < 1)
			return 0;
		int checked = 0, n = schemaTable.getItemCount(), i;
		for (i = 0; i < n; i++) {
			if (schemaTable.getItem(i).getChecked())
				checked++;
			if (checked > 0 && !schemaTable.getItem(i).getChecked())
				return -1;
		}

		if (checked == 0)
			return 0;
		else if (checked == n)
			return 1;
		else
			return -1;
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		databaseNameText.setText(database.getLabel());
		dbDir = database.getDatabaseInfo().getDbDir();
		ServerInfo serverInfo = database.getServer().getServerInfo();
		if (serverInfo != null && serverInfo.getServerOsInfo() == OsInfoType.NT) {
			dbDir = FileNameUtils.separatorsToWindows(dbDir);
		}
		if (dbDir != null) {
			targetDirText.setText(dbDir);
			hashFileText.setText(dbDir
					+ database.getServer().getServerInfo().getPathSeparator()
					+ "hashfile");
		}
		allSchemaButton.setSelection(true);
		for (int i = 0, n = schemaTable.getItemCount(); i < n; i++) {
			schemaTable.getItem(i).setChecked(true);
		}
		targetDirText.addModifyListener(this);
		prefixText.addModifyListener(this);
		hashFileText.addModifyListener(this);
		cachedPageText.addModifyListener(this);
		estimatedSizeText.addModifyListener(this);
		loFileCountText.addModifyListener(this);
		valid();
	}

	public void modifyText(ModifyEvent e) {
		valid();
	}

	/**
	 * 
	 * Check the validation
	 * 
	 * @return
	 */
	private boolean valid() {
		String targetDir = targetDirText.getText();
		boolean isValidTargetDir = true;
		isValidTargetDir = ValidateUtil.isValidPathName(targetDir);
		String hashFileDir = hashFileText.getText();
		boolean isValidHashFileDir = true;
		if (useTempHashFileButton.getSelection()) {
			isValidHashFileDir = ValidateUtil.isValidPathName(hashFileDir);
			if (isValidHashFileDir
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(hashFileDir);
				if (!file.exists()) {
					isValidHashFileDir = false;
				}
			}
		}
		String prefix = prefixText.getText();
		boolean isValidOutputPrefix = true;
		if (outputPrefixButton.getSelection()) {
			isValidOutputPrefix = ValidateUtil.isValidDBName(prefix);
		}

		String estimatedSize = estimatedSizeText.getText();
		boolean isValidEstimatedSize = true;
		if (estimatedSizeButton.getSelection()) {
			isValidEstimatedSize = ValidateUtil.isNumber(estimatedSize)
					&& Integer.parseInt(estimatedSize) > 0;
		}

		String cachedPageCount = cachedPageText.getText();
		boolean isValidCachedPageCount = true;
		if (cachedPageButton.getSelection()) {
			isValidCachedPageCount = ValidateUtil.isNumber(cachedPageCount)
					&& Integer.parseInt(cachedPageCount) > 0;
		}
		boolean isSelectedClasses = true;
		if (allSchemaButton.getSelection()
				|| selectedSchemaButton.getSelection()
				|| selectedDataButton.getSelection()) {
			isSelectedClasses = false;
			for (int i = 0; i < schemaTable.getItemCount(); i++) {
				if (schemaTable.getItem(i).getChecked()) {
					isSelectedClasses = true;
					break;
				}
			}
		}
		if (noSchemaButton.getSelection() && noDataButton.getSelection()) {
			isSelectedClasses = false;
		}
		String loCount = loFileCountText.getText();
		boolean isValidLoCount = true;
		if (loFileCountButton.getSelection()) {
			isValidLoCount = ValidateUtil.isNumber(loCount)
					&& Integer.parseInt(loCount) > 0;
		}

		if (!isValidTargetDir) {
			setErrorMessage(Messages.errTargetDir);
		} else if (!isSelectedClasses) {
			setErrorMessage(Messages.errNoTable);
		} else if (!isValidOutputPrefix) {
			setErrorMessage(Messages.errPrefix);
		} else if (!isValidHashFileDir) {
			setErrorMessage(Messages.errHashFile);
		} else if (!isValidCachedPageCount) {
			setErrorMessage(Messages.errNumOfCachedPage);
		} else if (!isValidEstimatedSize) {
			setErrorMessage(Messages.errNumOfInstances);
		} else if (!isValidLoCount) {
			setErrorMessage(Messages.errLoFileCount);
		} else {
			setErrorMessage(null);
		}

		boolean isEnabled = isValidTargetDir && isValidHashFileDir
				&& isValidOutputPrefix && isValidEstimatedSize
				&& isValidCachedPageCount && isValidLoCount
				&& isSelectedClasses;
		if (getButton(IDialogConstants.OK_ID) != null)
			getButton(IDialogConstants.OK_ID).setEnabled(isEnabled);
		return isEnabled;
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

	/**
	 * 
	 * Set user class list
	 * 
	 * @param userClassList
	 */
	public void setUserClassList(List<String> userClassList) {
		this.allUserClassList = userClassList;
	}
}
