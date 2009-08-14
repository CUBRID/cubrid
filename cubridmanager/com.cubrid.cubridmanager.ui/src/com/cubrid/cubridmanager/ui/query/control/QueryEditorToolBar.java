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
package com.cubrid.cubridmanager.ui.query.control;

import java.util.List;

import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * A Toolbar Control to show the query editor toolItem and database selection
 * menu
 * 
 * @author pangqiren 2009-3-2
 */
public class QueryEditorToolBar extends
		ToolBar {

	private static final String NULL_DATABASE_ID = "__null__";

	public static final String NO_DATABASE_SELECTED = Messages.noDbSelected;

	// default database selection
	public static CubridDatabase NULL_DATABASE = new CubridDatabase(
			NULL_DATABASE_ID, NO_DATABASE_SELECTED);

	CubridDatabase dbSelectd = NULL_DATABASE;

	private Menu dbSelectionMenu;

	private DatabaseMenuItem selectedMenuItem;

	private Listener listener;

	private Button selectDbButton;

	private QueryEditorPart editor;

	private DatabaseMenuItem nullDbMenuItem;

	/**
	 * Create the composite
	 * 
	 * @param parent
	 * @param editor
	 */
	public QueryEditorToolBar(Composite parent, QueryEditorPart editor) {
		super(parent, SWT.WRAP);
		this.editor = editor;
		final GridLayout g = new GridLayout();
		g.marginHeight = 0;
		setLayout(g);
		ToolItem label = new ToolItem(this, SWT.SEPARATOR);
		selectDbButton = new Button(this, SWT.NONE);
		selectDbButton.setAlignment(SWT.LEFT);
		selectDbButton.setText(NO_DATABASE_SELECTED);
		label.setControl(selectDbButton);
		label.setWidth(200);
		GridData gd = new GridData(SWT.FILL, SWT.FILL, true, true);
		gd.horizontalSpan = 0;
		gd.verticalSpan = 0;
		gd.minimumWidth = 230;
		gd.grabExcessVerticalSpace = true;
		setLayoutData(gd);

		loadDatabaseMenu();
		selectMenuItem(nullDbMenuItem);

		selectDbButton.addListener(SWT.Selection, new Listener() {

			public void handleEvent(Event event) {
				Rectangle rect = selectDbButton.getBounds();
				Point pt = new Point(rect.x, rect.y + rect.height);
				pt = selectDbButton.toDisplay(pt);
				dbSelectionMenu.setLocation(pt);
				dbSelectionMenu.setVisible(true);
			}

		});
		addSelectionListener();
	}

	/**
	 * when tree node in navigation view change, refresh the database list
	 */
	public void refresh() {
		Display.getDefault().asyncExec(new Runnable() {

			public void run() {
				String id = selectedMenuItem.getId();
				loadDatabaseMenu();
				DatabaseMenuItem item = findById(id);
				if (item == null || !item.isEnabled()) {
					editor.shutDownConnection();
					selectMenuItem(nullDbMenuItem);
				} else {
					item.setSelection(true);
				}
				addSelectionListener();
			}

		});
	}

	@Override
	protected void checkSubclass() {
		// do not check subclass
	}

	public void setListener(Listener listener) {
		this.listener = listener;

	}

	public void setDatabase(CubridDatabase database) {
		if (database.isLogined()
				&& database.getRunningType() == DbRunningType.CS) {
			initMenuItem(database.getId());
		}

	}

	/**
	 * get selected database
	 * 
	 * @return
	 */
	public CubridDatabase getSelectedDb() {
		return dbSelectd;
	}

	/**
	 * inject custom operation when database changed
	 * 
	 * @param listener
	 */
	public void addDatabaseChangedListener(Listener listener) {
		setListener(listener);

	}

	public void setText(String text) {
		if (text.length() > 20) {
			String showText = text.substring(0, 16) + "...";
			selectDbButton.setText(showText);
		} else {
			selectDbButton.setText(text);
		}
		selectDbButton.setToolTipText(text);
	}

	private void addSelectionListener() {
		MenuItem[] items = dbSelectionMenu.getItems();
		for (final MenuItem item : items) {
			item.addSelectionListener(new SelectionAdapter() {

				public void widgetSelected(SelectionEvent e) {
					if (item.getStyle() != SWT.RADIO) {
						CommonTool.openErrorBox(getParent().getShell(),
								Messages.plsSelectDb);
						return;
					}
					CubridDatabase oldSelectedDb = getSelectedDb();
					DatabaseMenuItem dbItem = (DatabaseMenuItem) item;
					CubridDatabase selectedDb = dbItem.getDatabase();
					if (oldSelectedDb != null && selectedDb != null
							&& oldSelectedDb.getId().equals(selectedDb.getId())) {
						return;
					} else if (oldSelectedDb != null
							&& !oldSelectedDb.getId().equals(NULL_DATABASE_ID)) {
						boolean confirm = CommonTool.openConfirmBox(
								editor.getSite().getShell(),
								Messages.changeDbConfirm);
						if (!confirm) {
							dbItem.setSelection(false);
							return;
						}
					}
					boolean valid = editor.resetJDBCConnection();
					if (valid) {
						selectMenuItem(dbItem);
					} else {
						dbItem.setSelection(false);
					}
				}
			});
		}
	}

	/**
	 * load all database on all server. if not login or database not started,
	 * the item disabled.
	 */
	public void loadDatabaseMenu() {
		if (dbSelectionMenu != null) {
			dbSelectionMenu.dispose();
			this.getParent().update();
		}
		dbSelectionMenu = new Menu(getParent().getShell(), SWT.POP_UP);
		nullDbMenuItem = new DatabaseMenuItem(NULL_DATABASE.getId(),
				dbSelectionMenu, SWT.RADIO);
		nullDbMenuItem.setText(NULL_DATABASE.getLabel());
		nullDbMenuItem.setDatabase(NULL_DATABASE);
		List<CubridServer> servers = CubridNodeManager.getInstance().getAllServer();
		for (CubridServer server : servers) {
			new MenuItem(dbSelectionMenu, SWT.SEPARATOR);
			DatabaseMenuItem serverItem = new DatabaseMenuItem(server.getId(),
					dbSelectionMenu, SWT.NONE);
			serverItem.setImage(CubridManagerUIPlugin.getImage("/icons/navigator/host.png"));
			serverItem.setText(server.getLabel());
			List<ICubridNode> children = server.getChildren();
			for (ICubridNode child : children) {
				if (child.getType() == CubridNodeType.DATABASE_FOLDER) {
					ICubridNode[] dbs = child.getChildren(new NullProgressMonitor());
					if (dbs.length > 0) {
						for (ICubridNode database : dbs) {
							if (database.getType() == CubridNodeType.DATABASE) {
								CubridDatabase db = (CubridDatabase) database;
								DatabaseMenuItem dbItem = new DatabaseMenuItem(
										database.getId(), dbSelectionMenu,
										SWT.RADIO);
								dbItem.setText(database.getLabel());
								dbItem.setDatabase((CubridDatabase) database);
								if (!db.isLogined()
										|| !(db.getRunningType() == DbRunningType.CS)) {
									dbItem.setEnabled(false);
								}
							}
						}
					}
				}
			}
		}
		// if selectedItem is not null, it must be disposed, so find created one
		// with id.
		if (selectedMenuItem != null) {
			selectedMenuItem = findById(selectedMenuItem.getId());
		}
	}

	private void initMenuItem(String id) {
		DatabaseMenuItem item = findById(id);
		selectMenuItem(item);

	}

	/**
	 * target a database selection change
	 * 
	 * @param item
	 */
	private void selectMenuItem(DatabaseMenuItem item) {
		if (item != null) {
			if (selectedMenuItem != null && !selectedMenuItem.isDisposed()) {
				selectedMenuItem.setSelection(false);
			}
			item.setSelection(true);
			selectedMenuItem = item;
			setText(item.getText());
			dbSelectd = item.getDatabase();
			Shell shell = editor.getSite().getShell();
			if (dbSelectd == NULL_DATABASE) {
				if (shell != null && !shell.isDisposed()) {
					shell.setText(LayoutManager.CUBRID_MANAGER_TITLE + " - "
							+ NO_DATABASE_SELECTED);
				}
			} else {
				String title = LayoutManager.getTitle(dbSelectd);
				if (shell != null) {
					shell.setText(LayoutManager.CUBRID_MANAGER_TITLE);
					if (title != null && title.trim().length() > 0) {
						shell.setText(LayoutManager.CUBRID_MANAGER_TITLE
								+ " - " + title);
					}
				}
			}
			if (listener != null) {
				Event e = new Event();
				e.data = dbSelectd;
				listener.handleEvent(e);
			}
		}
	}

	/**
	 * find menu item by menu item id
	 * 
	 * @param id
	 * @return
	 */
	public DatabaseMenuItem findById(String id) {
		MenuItem[] items = dbSelectionMenu.getItems();
		if (items.length > 0) {
			for (MenuItem item : items) {
				if (item instanceof DatabaseMenuItem
						&& ((DatabaseMenuItem) item).getId().equals(id)) {
					return (DatabaseMenuItem) item;
				}
			}
		}
		return null;
	}

	/**
	 * if no database selected
	 * 
	 * @return
	 */
	public boolean isNull() {
		return getSelectedDb() == NULL_DATABASE;
	}

	/**
	 * extend MenuItem, save id and database.
	 * 
	 * @author wangsl 2009-6-4
	 */
	static class DatabaseMenuItem extends
			MenuItem {

		String id;

		CubridDatabase database;

		public DatabaseMenuItem(String id, Menu parent, int style) {
			super(parent, style);
			setId(id);
		}

		public CubridDatabase getDatabase() {
			return database;
		}

		public void setDatabase(CubridDatabase database) {
			this.database = database;
		}

		@Override
		protected void checkSubclass() {
			// do nothing
		}

		public String getId() {
			return id;
		}

		public void setId(String id) {
			this.id = id;
		}

		public String toString() {
			return id;
		}

	}
}