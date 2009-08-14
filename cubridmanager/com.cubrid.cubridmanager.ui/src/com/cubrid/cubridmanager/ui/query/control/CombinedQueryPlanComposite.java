/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.ui.query.control;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CTabFolder;
import org.eclipse.swt.custom.CTabItem;
import org.eclipse.swt.custom.ST;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.StyleRange;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.MouseListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeColumn;
import org.eclipse.swt.widgets.TreeItem;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.query.plan.model.PlanCost;
import com.cubrid.cubridmanager.core.query.plan.model.PlanNode;
import com.cubrid.cubridmanager.core.query.plan.model.PlanRoot;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTermItem;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.StructQueryPlan;
import com.cubrid.cubridmanager.ui.query.action.ShowSchemaAction;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;

/**
 * 
 * The Query Execution Plan Tab in the Query Editor
 * 
 * CombinedQueryPlanComposite Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class CombinedQueryPlanComposite extends
		Composite {

	private static final Logger logger = LogUtil.getLogger(CombinedQueryPlanComposite.class);
	private QueryEditorPart editor = null;

	private CTabFolder tabPlans = null;
	private SashForm sashPlan = null;
	private Table planHistory = null;

	private List<StructQueryPlan> planHistoryList = new ArrayList<StructQueryPlan>();

	private String planFilename = null;
	private String planShortFilename = null;
	private boolean isDirty = false;

	private ToolItem openItem = null;
	private ToolItem saveItem = null;
	private ToolItem saveAsItem = null;
	private ToolItem newItem = null;
	private ToolItem historySwitchItem = null;
	private ToolItem historyShowHideItem = null;
	private ToolItem displayModeItem = null;

	private int[] sashPlanWeight = null;
	private boolean displayModeTree = true;

	private TabItem explainTab = null;

	private boolean collectToHistoryFlag = true;

	private TableColumn[] planHistoryCols = new TableColumn[3];

	private Color[] termCellBgColor = new Color[2];

	private Label filenameLabel = null;

	private static final int[] TABLE_COLS_WIDTH_DEF = new int[] { 160, 100,
			100, 150, 70, 70, 70 };

	public CombinedQueryPlanComposite(TabFolder parent, int style,
			QueryEditorPart queryEditorPart) {
		super(parent, style);
		this.editor = queryEditorPart;

		GridLayout tLayout = new GridLayout(1, false);
		tLayout.verticalSpacing = 0;
		tLayout.horizontalSpacing = 0;
		tLayout.marginWidth = 0;
		tLayout.marginHeight = 0;

		setLayout(tLayout);
		explainTab = new TabItem(parent, SWT.NONE);
		explainTab.setText(Messages.QEDIT_PLAN_FOLDER);
		explainTab.setControl(this);
	}

	/**
	 * Initializing a Plan Tab
	 */
	public void initialize() {
		createPlanToolbar();

		sashPlan = new SashForm(this, SWT.HORIZONTAL);
		sashPlan.setLayout(new GridLayout(2, true));
		sashPlan.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		tabPlans = new CTabFolder(sashPlan, SWT.BOTTOM | SWT.BORDER | SWT.CLOSE);
		tabPlans.setSimple(true);
		tabPlans.setUnselectedImageVisible(true);
		tabPlans.setUnselectedCloseVisible(true);
		tabPlans.setLayout(new GridLayout(1, true));
		tabPlans.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		planHistory = new Table(sashPlan, SWT.SINGLE | SWT.FULL_SELECTION);
		planHistory.setLayout(new GridLayout(1, true));
		planHistory.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));
		planHistory.setHeaderVisible(true);
		planHistory.setLinesVisible(true);
		planHistory.addMouseListener(new MouseListener() {
			public void mouseDoubleClick(MouseEvent event) {
				StructQueryPlan sq = planHistoryList.get(planHistory.getSelectionIndex());
				int uid = planHistory.getSelectionIndex() + 1;
				PlanTabItem tabItem = findPlanTab(uid);
				if (tabItem == null) {
					tabItem = newPlanTab(uid);
				}
				tabPlans.setSelection(tabItem);
				printPlan(tabItem, sq);
			}

			public void mouseDown(MouseEvent event) {
			}

			public void mouseUp(MouseEvent event) {
			}
		});

		int i = 0;
		planHistoryCols[i] = new TableColumn(planHistory, SWT.RIGHT);
		planHistoryCols[i].setText(Messages.QEDIT_PLAN_HISTORY_COL1); // No
		planHistoryCols[i].setMoveable(true);
		planHistoryCols[i].setWidth(20);

		planHistoryCols[i] = new TableColumn(planHistory, SWT.LEFT);
		planHistoryCols[i].setText(Messages.QEDIT_PLAN_HISTORY_COL2); // Date
		planHistoryCols[i].setMoveable(true);
		planHistoryCols[i].setWidth(100);

		planHistoryCols[++i] = new TableColumn(planHistory, SWT.RIGHT);
		planHistoryCols[i].setText(Messages.QEDIT_PLAN_HISTORY_COL4); // Cost
		planHistoryCols[i].setMoveable(true);
		planHistoryCols[i].setWidth(50);

		sashPlanWeight = new int[] { 80, 20 };
		sashPlan.setWeights(sashPlanWeight);

		newPlanTab(1);

		termCellBgColor[0] = new Color(getDisplay(), 220, 220, 220);
		termCellBgColor[1] = new Color(getDisplay(), 240, 240, 240);
	}

	private void openPlan() {
		FileDialog dialog = QueryEditorPart.openFileOpenPlanDialog();

		String filename = dialog.open();
		if (filename == null) {
			return;
		}

		File file = new File(filename);

		List<StructQueryPlan> sqList = null;

		try {
			sqList = PlanHistoryManager.openFile(file);
		} catch (IOException ex) {
			logger.error(ex);
			CommonTool.openErrorBox(Messages.QEDIT_PLAN_OPEN_FILE_ERROR);
			return;
		}

		if (sqList == null) {
			CommonTool.openErrorBox(Messages.QEDIT_PLAN_INVALID_PLAN_FILE);
			return;
		}

		planFilename = filename;
		planShortFilename = file.getName();

		boolean collectToHistoryFlagPrev = collectToHistoryFlag;
		collectToHistoryFlag = true;

		clearAll();
		printHistories(sqList);

		collectToHistoryFlag = collectToHistoryFlagPrev;
	}

	private void savePlan() {
		String filename = null;
		if (planFilename == null) {
			FileDialog dialog = QueryEditorPart.openFileSavePlanDialog();
			filename = dialog.open();
		} else {
			filename = planFilename;
		}

		if (filename == null) {
			return;
		}

		if (filename.lastIndexOf(".xml") == -1) {
			filename += ".xml";
		}

		File file = new File(filename);

		try {
			PlanHistoryManager.saveFile(file, planHistoryList);
		} catch (IOException ex) {
			logger.error(ex);
			CommonTool.openErrorBox(Messages.QEDIT_PLAN_SAVE_FILE_ERROR);
			return;
		}

		planFilename = filename;
		planShortFilename = file.getName();
		setDirty(false);
	}

	private void saveAsPlan() {
		FileDialog dialog = QueryEditorPart.openFileSavePlanDialog();
		String filename = dialog.open();
		if (filename == null) {
			return;
		}

		if (filename.lastIndexOf(".xml") == -1) {
			filename += ".xml";
		}

		File file = new File(filename);

		try {
			PlanHistoryManager.saveFile(file, planHistoryList);
		} catch (IOException ex) {
			logger.error(ex);
			CommonTool.openErrorBox(Messages.QEDIT_PLAN_SAVE_FILE_ERROR);
			return;
		}

		planFilename = filename;
		planShortFilename = file.getName();
		setDirty(false);
	}

	private void createPlanToolbar() {

		Composite toolBarLine = new Composite(this, SWT.NONE);
		GridLayout tLayout = new GridLayout(2, false);
		tLayout.verticalSpacing = 0;
		tLayout.horizontalSpacing = 10;
		tLayout.marginWidth = 0;
		tLayout.marginHeight = 0;
		toolBarLine.setLayout(tLayout);
		toolBarLine.setLayoutData(new GridData(SWT.FILL, SWT.NONE, false, false));

		// Explain toolbar
		ToolBar toolBar = new ToolBar(toolBarLine, SWT.FLAT);

		// Explain history filename label
		filenameLabel = new Label(toolBarLine, SWT.NONE);
		filenameLabel.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		filenameLabel.setText("   ");

		// New
		newItem = new ToolItem(toolBar, SWT.PUSH);
		newItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_new.png"));
		newItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_NEW);
		newItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (isDirty) {
					MessageDialog dialog = new MessageDialog(
							editor.getSite().getShell(),
							Messages.QEDIT_PLAN_CLEAR_QUESTION_TITLE, null,
							Messages.QEDIT_PLAN_CLEAR_QUESTION,
							MessageDialog.QUESTION, new String[] {
									IDialogConstants.YES_LABEL,
									IDialogConstants.CANCEL_LABEL }, 0);

					switch (dialog.open()) {
					case 0: // yes
						break;
					default: // cancel
						return;
					}
				}

				planFilename = null;
				planShortFilename = null;

				clearAll();

				saveAsItem.setEnabled(false);

			}
		});

		// Open plan
		openItem = new ToolItem(toolBar, SWT.PUSH);
		openItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/file_open.png"));
		openItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_OPEN);
		openItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (isDirty) {
					MessageDialog dialog = new MessageDialog(
							editor.getSite().getShell(),
							Messages.QEDIT_PLAN_SAVE_CHANGE_QUESTION_TITLE,
							null, Messages.QEDIT_PLAN_SAVE_CHANGE_QUESTION,
							MessageDialog.QUESTION, new String[] {
									IDialogConstants.YES_LABEL,
									IDialogConstants.NO_LABEL,
									IDialogConstants.CANCEL_LABEL }, 0);

					switch (dialog.open()) {
					case 0: // yes
						savePlan();
						break;
					case 1: // no
						break;
					default: // cancel
						return;
					}
				}

				openPlan();
			}
		});

		// Save plan
		saveItem = new ToolItem(toolBar, SWT.PUSH);
		saveItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/file_save.png"));
		saveItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_SAVE);
		saveItem.setEnabled(false);
		saveItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				savePlan();
			}
		});

		// SaveAs plan
		saveAsItem = new ToolItem(toolBar, SWT.PUSH);
		saveAsItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/file_saveas.png"));
		saveAsItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_SAVEAS);
		saveAsItem.setEnabled(false);
		saveAsItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				saveAsPlan();
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		// Collecting histories switch
		historySwitchItem = new ToolItem(toolBar, SWT.CHECK);
		historySwitchItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_history_switch.png"));
		historySwitchItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_HISTORY_SWITCH);
		historySwitchItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				collectToHistoryFlag = !historySwitchItem.getSelection();
			}
		});

		// Explain display mode (graphic / raw text)
		displayModeItem = new ToolItem(toolBar, SWT.PUSH);
		displayModeItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_mode_tree.png"));
		displayModeItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_DISPLAY_MODE);
		displayModeItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				displayModeTree = !displayModeTree;
				String imageName = null;
				if (displayModeTree) {
					imageName = "/icons/queryeditor/qe_explain_mode_tree.png";
				} else {
					imageName = "/icons/queryeditor/qe_explain_mode_raw.png";
				}
				displayModeItem.setImage(CubridManagerUIPlugin.getImage(imageName));

				CTabItem[] items = tabPlans.getItems();
				for (int i = 0, len = items.length; i < len; i++) {
					PlanTabItem item = (PlanTabItem) items[i];
					item.useTreeMode(displayModeTree);
				}
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		// Show/hide of the history pane
		historyShowHideItem = new ToolItem(toolBar, SWT.PUSH);
		historyShowHideItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_history_hide.png"));
		historyShowHideItem.setToolTipText(Messages.TOOLTIP_QEDIT_EXPLAIN_HISTORY_SHOW_HIDE);
		historyShowHideItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean isShow = !planHistory.getVisible();
				planHistory.setVisible(isShow);
				if (isShow) {
					historyShowHideItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_history_hide.png"));
					sashPlan.setWeights(sashPlanWeight);
				} else {
					historyShowHideItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/qe_explain_history_show.png"));
					sashPlanWeight = sashPlan.getWeights();
					sashPlan.setWeights(new int[] { 100, 0 });
				}
			}
		});

	}

	public void clearAll() {
		planHistoryList.clear();
		setDirty(false);

		for (int i = planHistory.getItemCount() - 1; i >= 0; i--) {
			planHistory.getItem(0).dispose();
		}

		for (int i = tabPlans.getItemCount() - 1; i >= 0; i--) {
			PlanTabItem tabItem = (PlanTabItem) tabPlans.getItem(0);
			tabItem.getPlanTree().dispose();
			tabItem.dispose();
		}

		newPlanTab(1);
	}

	/**
	 * dispose all plan tabs in plan panel
	 * 
	 * @return
	 */
	public void clearPlanTabs() {
		if (tabPlans != null && !tabPlans.isDisposed()) {
			while (tabPlans.getItemCount() > 0) {
				if (!tabPlans.getItem(0).getControl().isDisposed())
					tabPlans.getItem(0).getControl().dispose();
				tabPlans.getItem(0).dispose();
			}
		}
	}

	/**
	 * make & init plan tab for execution plans
	 * 
	 * @param sq
	 * @return
	 */
	public void addPlanTab(StructQueryPlan sq) {
		int uid = 0;

		if (collectToHistoryFlag) {
			synchronized (planHistoryList) {
				planHistoryList.add(sq);
				uid = planHistoryList.size();
			}
		}

		if (logger.isDebugEnabled()) {
			logger.debug("<addPlanTab-uid>" + uid + "</addPlanTab-uid>");
		}

		PlanTabItem tabItem = findPlanTab(uid);
		if (tabItem == null) {
			tabItem = newPlanTab(uid);
		}

		tabPlans.setSelection(tabItem);

		printPlan(tabItem, sq);

		if (collectToHistoryFlag) {
			printHistory(uid, sq);
		}
	}

	public void addPlanTab(StructQueryPlan sq, boolean dirtyFlag) {
		if (collectToHistoryFlag)
			setDirty(dirtyFlag);
		addPlanTab(sq);
	}

	/**
	 * find a existing tab item object with a uid
	 * 
	 * @param uid
	 * @return
	 */
	private PlanTabItem findPlanTab(int uid) {
		if (tabPlans == null || tabPlans.getItemCount() == 0) {
			if (logger.isDebugEnabled()) {
				logger.debug("<findPlanTab-return>null</findPlanTab-return>");
			}
			return null;
		}

		String findTabName = uid > 0 ? Messages.QEDIT_PLAN + uid
				: Messages.QEDIT_PLAN;
		for (int i = 0, len = tabPlans.getItemCount(); i < len; i++) {
			String tabName = tabPlans.getItem(i).getText();
			if (findTabName.equals(tabName)) {
				return (PlanTabItem) tabPlans.getItem(i);
			}
		}

		if (logger.isDebugEnabled()) {
			logger.debug("<findPlanTab-return>null</findPlanTab-return>");
		}

		return null;
	}

	/**
	 * create a new plan tab
	 * 
	 * @return
	 */
	private PlanTabItem newPlanTab(int uid) {

		final PlanTabItem planTabItem = new PlanTabItem(tabPlans, SWT.NONE);
		planTabItem.setText(uid > 0 ? Messages.QEDIT_PLAN + uid
				: Messages.QEDIT_PLAN);

		SashForm planTabMid = new SashForm(tabPlans, SWT.VERTICAL);
		planTabMid.setLayout(new FillLayout());

		GridData gridData = new GridData();
		gridData.horizontalAlignment = SWT.FILL;
		gridData.verticalAlignment = SWT.FILL;

		// 1. Plan Tree
		final Tree tree = new Tree(planTabMid, SWT.BORDER | SWT.H_SCROLL
				| SWT.V_SCROLL | SWT.FULL_SELECTION);

		planTabItem.setPlanTree(tree);
		tree.setHeaderVisible(true);
		tree.setLinesVisible(true);
		tree.addSelectionListener(new SelectionListener() {
			public void widgetDefaultSelected(SelectionEvent e) {
			}

			public void widgetSelected(SelectionEvent e) {
				TreeItem item = ((Tree) e.getSource()).getSelection()[0];
				if (item.getData() != null) {
					planTabItem.getSqlText().setText((String) item.getData());
					planTabItem.decorateSqlText();
				}
			}
		});

		MenuManager menuManager = new MenuManager();
		menuManager.setRemoveAllWhenShown(true);
		menuManager.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				IAction showSchemaAction = ActionManager.getInstance().getAction(
						ShowSchemaAction.ID);
				if (showSchemaAction != null) {
					manager.add(showSchemaAction);
				}
			}
		});

		Menu contextMenu = menuManager.createContextMenu(tree);
		tree.setMenu(contextMenu);
		tree.addSelectionListener(new SelectionAdapter() {
			@Override
			public void widgetSelected(SelectionEvent e) {
				IAction showSchemaAction = ActionManager.getInstance().getAction(
						ShowSchemaAction.ID);

				showSchemaAction.setEnabled(false);

				if (editor.getSelectedDatabase().getDatabaseInfo() == null) {

					return;
				}

				String tname = tree.getSelection()[0].getText(1);
				if (tname == null || tname.length() == 0) {
					return;
				}
				int ep = tname.indexOf(' ');
				if (ep != -1) {
					tname = tname.substring(0, ep);
				}

				editor.setCurrentSchemaName(tname);

				if (tname != null) {
					showSchemaAction.setEnabled(true);
				}
			}
		});

		TreeColumn[] cols = new TreeColumn[7];
		int i = 0;

		// Type
		cols[i] = new TreeColumn(tree, SWT.LEFT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL1);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL1);

		// Table
		cols[++i] = new TreeColumn(tree, SWT.CENTER);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL2);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL2);

		// Index
		cols[++i] = new TreeColumn(tree, SWT.LEFT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL6);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL6);

		// Terms
		cols[++i] = new TreeColumn(tree, SWT.LEFT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL7);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL7);

		// CPU Cost (fixed/var)
		cols[++i] = new TreeColumn(tree, SWT.RIGHT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL3);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL3_DTL);

		// I/O Cost (fixed/var)
		cols[++i] = new TreeColumn(tree, SWT.RIGHT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL4);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL4_DTL);

		// Row/Page
		cols[++i] = new TreeColumn(tree, SWT.RIGHT);
		cols[i].setText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL5);
		cols[i].setToolTipText(Messages.QEDIT_PLAN_TREE_SIMPLE_COL5_DTL);

		for (int j = 0, len = cols.length; j < len; j++) {
			cols[j].setMoveable(j > 0);
		}

		planTabItem.setControl(planTabMid);

		// 2. Parsed SQL
		final StyledText planSql = new StyledText(planTabMid, SWT.MULTI
				| SWT.READ_ONLY | SWT.BORDER | SWT.V_SCROLL | SWT.WRAP);
		planSql.setLayout(new FillLayout());
		planSql.setKeyBinding('C' | SWT.MOD1, ST.COPY);
		Menu menu = new Menu(getShell(), SWT.POP_UP);
		final MenuItem copyItem = new MenuItem(menu, SWT.PUSH);
		copyItem.setText(Messages.QEDIT_PLAN_SQL_COPY);
		copyItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				planSql.selectAll();
				planSql.copy();
				planSql.setSelection(0);
			}
		});
		planSql.setMenu(menu);
		planTabItem.setSqlText(planSql);

		// 3. Raw Plan
		final StyledText planRaw = new StyledText(planTabMid, SWT.MULTI
				| SWT.READ_ONLY | SWT.BORDER | SWT.V_SCROLL | SWT.WRAP);
		planRaw.setLayout(new FillLayout());
		planRaw.setVisible(false);
		menu = new Menu(getShell(), SWT.POP_UP);
		final MenuItem copyRawPlanItem = new MenuItem(menu, SWT.PUSH);
		copyRawPlanItem.setText(Messages.QEDIT_PLAN_RAW_PLAN_COPY);
		copyRawPlanItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				planRaw.selectAll();
				planRaw.copy();
				planRaw.setSelection(0);
			}
		});
		planRaw.setMenu(menu);
		planTabItem.setRawPlan(planRaw);

		planTabMid.setWeights(new int[] { 70, 30, 0 });
		planTabItem.setSashForm(planTabMid);

		return planTabItem;

	}

	private void printPlan(PlanTabItem tabItem, StructQueryPlan sq) {

		if (tabItem == null || sq == null) {
			return;
		}

		// clear tab item contents
		while (tabItem.getPlanTree().getItemCount() > 0) {
			tabItem.getPlanTree().getItem(0).dispose();
		}

		tabItem.getRawPlan().setText(sq.getPlanRaw());

		for (int i = 0, len = sq.countSubPlan(); i < len; i++) {

			PlanRoot planRoot = sq.getSubPlan(i);
			if (planRoot == null) {
				return;
			}

			// print a raw plan
			if (i == 0) {
				tabItem.getSqlText().setText(planRoot.getRaw());
				tabItem.decorateSqlText();
			}

			PlanNode node = planRoot.getPlanNode();
			if (node != null) {
				printSubPlan(tabItem, null, node, planRoot.getRaw());
			}

		}

		for (int i = 0, len = TABLE_COLS_WIDTH_DEF.length; i < len; i++) {
			int tColMaxWidth = TABLE_COLS_WIDTH_DEF[i];
			tabItem.getPlanTree().getColumn(i).pack();
			if (tabItem.getPlanTree().getColumn(i).getWidth() < tColMaxWidth) {
				tabItem.getPlanTree().getColumn(i).setWidth(tColMaxWidth);
			}
		}

		tabItem.useTreeMode(displayModeTree);

	}

	private void printSubPlan(PlanTabItem tabItem, TreeItem treeItem,
			PlanNode node, String sql) {
		boolean isRoot = treeItem == null;
		boolean existChildren = node.getChildren() != null
				&& node.getChildren().size() > 0;

		TreeItem item = null;
		if (isRoot) {
			item = new TreeItem(tabItem.getPlanTree(), SWT.NONE);
			item.setData(sql);
		} else {
			item = new TreeItem(treeItem, SWT.NONE);
		}

		int i = 0;
		String icon = null;
		if ("idx-join".equals(node.getMethod())) {
			icon = "/icons/queryeditor/qe_explain_index_join.png";
		} else if (existChildren) {
			icon = "/icons/queryeditor/qe_explain_folder.png";
		} else if (node.getIndex() != null) {
			icon = "/icons/queryeditor/qe_explain_index.png";
		} else if (node.getTable() != null && node.getTable().getPartitions() != null) {
			icon = "/icons/queryeditor/qe_explain_partition.png";
		} else {
			icon = "/icons/queryeditor/qe_explain_table.png";
		}

		// Type
		item.setImage(i, CubridManagerUIPlugin.getImage(icon));
		item.setText(i++, node.getMethod() + (node.getOrder() != null ? " ("+node.getOrder().toLowerCase()+")" : ""));

		// Table
		if (node.getTable() != null) {
			item.setText(i++, node.getTable().getName());
		} else {
			item.setText(i++, "");
		}

		// Index
		if (node.getIndex() != null) {
			PlanTerm index = node.getIndex();
			item.setText(i++, index.getName());
		} else {
			item.setText(i++, "");
		}

		// Terms
		if (node.getTable() == null ||node.getTable().getPartitions() == null  ) {
			item.setText(i++, "");
		} else {
			item.setText(i++, node.getTable().getTextPartitions());
		}

		boolean isOddRow = false;

		if (node.getIndex() != null) {
			printSubPlanTerm(tabItem, item, node.getIndex(),
					Messages.QEDIT_PLAN_TREE_TERM_NAME_INDEX,
					(isOddRow = !isOddRow));
		}

		if (node.getEdge() != null) {
			printSubPlanTerm(tabItem, item, node.getEdge(),
					Messages.QEDIT_PLAN_TREE_TERM_NAME_JOIN,
					(isOddRow = !isOddRow));
		}

		if (node.getSargs() != null) {
			printSubPlanTerm(tabItem, item, node.getSargs(),
					Messages.QEDIT_PLAN_TREE_TERM_NAME_SELECT,
					(isOddRow = !isOddRow));
		}

		if (node.getFilter() != null) {
			printSubPlanTerm(tabItem, item, node.getFilter(),
					Messages.QEDIT_PLAN_TREE_TERM_NAME_FILTER,
					(isOddRow = !isOddRow));
		}

		// CPU Cost
		// I/O Cost
		if (node.getCost() != null) {
			PlanCost cost = node.getCost();
			item.setText(i++, cost.getFixedCpu() + "/" + cost.getVarCpu());
			item.setText(i++, cost.getFixedDisk() + "/" + cost.getVarDisk());
		} else {
			item.setText(i++, "");
			item.setText(i++, "");
		}

		// Row/Page
		if (node.getTable() != null) {
			item.setText(i++, node.getTable().getCard() + "/"
					+ node.getTable().getPage());
		} else {
			item.setText(i++, "");
		}

		if (!isRoot) {
			treeItem.setExpanded(true);
		}

		if (existChildren) {
			for (PlanNode childNode : node.getChildren()) {
				printSubPlan(tabItem, item, childNode, null);
			}
		}

	}

	private void printSubPlanTerm(PlanTabItem tabItem, TreeItem treeItem,
			PlanTerm term, String typeName, boolean isOddRow) {

		PlanTermItem[] termItems = term.getTermItems();
		if (termItems == null) {
			return;
		}

		TreeItem termTreeItem = new TreeItem(treeItem, SWT.NONE);
		termTreeItem.setText(0, typeName);
		termTreeItem.setBackground(termCellBgColor[isOddRow ? 0 : 1]);

		int len = termItems.length;
		if (len == 1) {
			termTreeItem.setText(3, term.getTermString());
		} else {
			PlanTermItem planTermItem = termItems[0];
			if (planTermItem == null || planTermItem.getCondition() == null) {
				return;
			}
			termTreeItem.setText(3, planTermItem.getCondition());

			for (int j = 1; j < len; j++) {

				planTermItem = termItems[j];
				if (planTermItem == null || planTermItem.getCondition() == null) {
					continue;
				}

				TreeItem item = new TreeItem(termTreeItem, SWT.NONE);

				item.setBackground(termCellBgColor[isOddRow ? 0 : 1]);

				int i = 0;
				// Type
				item.setText(i++, "");

				// Table
				item.setText(i++, "");

				// Index
				item.setText(i++, "");

				// Terms
				item.setText(i++, planTermItem.getCondition());

				// CPU Cost
				// I/O Cost
				item.setText(i++, "");
				item.setText(i++, "");

				// Row/Page
				item.setText(i++, "");

			}
		}

	}

	/**
	 * clear all plan tabs and print plan histories
	 * 
	 * @param sqList
	 */
	public void printHistories(List<StructQueryPlan> sqList) {
		clearPlanTabs();
		planHistoryList.clear();

		for (int i = 0, len = sqList.size(); i < len; i++) {
			StructQueryPlan sq = sqList.get(i);
			if (i == 0) {
				addPlanTab(sq);
			} else {
				printHistory(i + 1, sq);
				planHistoryList.add(sq);
			}
		}
	}

	/**
	 * print a plan history
	 * 
	 * @param sq
	 * @param tabIdx
	 */
	private void printHistory(int uid, StructQueryPlan sq) {
		String created = sq.getCreatedDateString();

		float costValue = 0.0f;

		PlanRoot planRoot = sq.getSubPlan(0);
		if (planRoot == null) {
			return;
		}
		
		for (int i = 0, len = sq.countSubPlan(); i < len; i++) {
			
			planRoot = sq.getSubPlan(i);
			PlanNode node = planRoot.getPlanNode();

			if (node != null && node.getCost() != null) {
				PlanCost cost = node.getCost();
				costValue += cost.getFixedTotal() + cost.getVarTotal();
			}
			
		}

		TableItem item = new TableItem(planHistory, SWT.LEFT);

		item.setText(0, String.valueOf(uid));
		item.setText(1, created);
		item.setText(2, String.valueOf(costValue));
		//		item.setText(3, planRoot.getPlainSql());

		for (int i = 0, len = planHistoryCols.length; i < len; i++) {
			if (planHistoryCols[i] != null && !planHistoryCols[i].isDisposed())
				planHistoryCols[i].pack();
		}
	}

	private void setDirty(boolean isDirty) {
		saveItem.setEnabled(isDirty);
		saveAsItem.setEnabled(true);
		explainTab.setText((isDirty ? "*" : "") + Messages.QEDIT_PLAN_FOLDER);
		printHistoryFilename(planShortFilename, isDirty);
		this.isDirty = isDirty;
	}

	@Override
	public void dispose() {
		clearPlanTabs();

		if (!planHistory.isDisposed())
			planHistory.dispose();

		for (int i = 0, len = planHistoryCols.length; i < len; i++) {
			if (planHistoryCols[i] != null && !planHistoryCols[i].isDisposed()) {
				planHistoryCols[i].dispose();
			}
		}

		if (!openItem.isDisposed())
			openItem.dispose();

		if (!saveItem.isDisposed())
			saveItem.dispose();

		if (!saveAsItem.isDisposed())
			saveAsItem.dispose();

		if (!newItem.isDisposed())
			newItem.dispose();

		super.dispose();
	}

	private void printHistoryFilename(String filename, boolean isDirty) {

		if (filename == null)
			filenameLabel.setText("");
		else
			filenameLabel.setText(Messages.QEDIT_PLAN_CURFILE_TITLE + ": "
					+ filename);

	}

	/**
	 * The Query Execution Plan Sub Tab Item in the Query Editor
	 * 
	 * @author pcraft 2009-4-28
	 */
	static class PlanTabItem extends
			CTabItem {
		/**
		 * a list index no of StructQueryPlan ArrayList in plan history pane
		 */
		private int uid = 0;
		private Tree planTree = null;
		private StyledText sqlText = null;
		private StyledText rawPlan = null;
		private SashForm sashForm = null;

		private int[] sashFormWeightBak = null;

		public PlanTabItem(CTabFolder parent, int style, int index) {
			super(parent, style, index);
		}

		public PlanTabItem(CTabFolder parent, int style) {
			super(parent, style);
		}

		public void setPaneWeight(int[] weight) {
			this.sashFormWeightBak = weight;
		}

		public int[] getPaneWeight() {
			return this.sashFormWeightBak;
		}

		public Tree getPlanTree() {

			return planTree;
		}

		public void setPlanTree(Tree plan) {

			this.planTree = plan;
		}

		public StyledText getSqlText() {

			return sqlText;
		}

		public void setSqlText(StyledText sql) {

			this.sqlText = sql;

		}

		public void decorateSqlText() {

			final String[] titleString = {
					"Join graph segments (f indicates final):",
					"Join graph nodes:", "Join graph equivalence classes:",
					"Join graph edges:", "Join graph terms:", "Query plan:",
					"Query stmt:" };

			for (int i = 0, sp = -1, ep = 0, len = titleString.length; i < len; i++) {
				sp = this.sqlText.getText().indexOf(titleString[i], ep);
				if (sp != -1) {
					StyleRange eachStyle = new StyleRange();
					eachStyle.start = sp;
					eachStyle.length = titleString[i].length();
					eachStyle.fontStyle = SWT.BOLD;
					eachStyle.foreground = getDisplay().getSystemColor(
							SWT.COLOR_RED);
					this.sqlText.setStyleRange(eachStyle);
				}
				ep = sp + 1;
			}

		}

		public int getUid() {

			return uid;
		}

		public void setUid(int uid) {

			this.uid = uid;
		}

		public StyledText getRawPlan() {
			return rawPlan;
		}

		public void setRawPlan(StyledText rawPlan) {
			this.rawPlan = rawPlan;
		}

		public void useTreeMode(boolean isTreeMode) {
			if (isTreeMode) {
				if (planTree.getVisible()) {
					return;
				}
				planTree.setVisible(true);
				sqlText.setVisible(true);
				rawPlan.setVisible(false);
				sashForm.setWeights(sashFormWeightBak);
			} else {
				if (!planTree.getVisible()) {
					return;
				}
				planTree.setVisible(false);
				sqlText.setVisible(false);
				rawPlan.setVisible(true);
				sashFormWeightBak = sashForm.getWeights();
				sashForm.setWeights(new int[] { 0, 0, 100 });
			}
		}

		public SashForm getSashForm() {
			return sashForm;
		}

		public void setSashForm(SashForm sashForm) {
			sashFormWeightBak = sashForm.getWeights();
			this.sashForm = sashForm;
		}

		@Override
		public void dispose() {
			if (!planTree.isDisposed())
				planTree.dispose();

			if (!sqlText.isDisposed())
				sqlText.dispose();

			if (!rawPlan.isDisposed())
				rawPlan.dispose();

			super.dispose();
		}
	}
}
