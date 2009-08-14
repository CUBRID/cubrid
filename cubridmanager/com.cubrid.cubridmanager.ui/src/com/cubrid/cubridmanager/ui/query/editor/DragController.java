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
package com.cubrid.cubridmanager.ui.query.editor;

import java.util.List;

import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.TreeSelection;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.dnd.DND;
import org.eclipse.swt.dnd.DragSource;
import org.eclipse.swt.dnd.DragSourceAdapter;
import org.eclipse.swt.dnd.DragSourceEvent;
import org.eclipse.swt.dnd.DropTarget;
import org.eclipse.swt.dnd.DropTargetAdapter;
import org.eclipse.swt.dnd.DropTargetEvent;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Tree;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllAttrTask;
import com.cubrid.cubridmanager.ui.common.navigator.CubridNavigatorView;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * a class to control navigator view drag and drop
 * 
 * @author wangsl 2009-5-18
 */
public class DragController {

	private QueryEditorPart editor;
	private TreeViewer treeViewer;

	private static DragSource source;

	public DragController(QueryEditorPart editor) {
		CubridNavigatorView navigatorView = LayoutManager.getNavigatorView();
		if (navigatorView == null || editor == null) {
			throw new IllegalArgumentException();
		}
		this.treeViewer = navigatorView.getViewer();
		if (treeViewer == null) {
			throw new IllegalArgumentException();
		}
		this.editor = editor;
	}

	public DragController(TreeViewer treeViewer, QueryEditorPart editor) {
		this.treeViewer = treeViewer;
		this.editor = editor;
	}

	/**
	 * register drag source and text editor target
	 */
	public void register() {
		if (source == null) {
			source = new DragSource(treeViewer.getControl(), DND.DROP_MOVE);
			source.setTransfer(new Transfer[] { TextTransfer.getInstance() });
			source.addDragListener(new DragSourceAdapter() {

				@Override
				public void dragFinished(DragSourceEvent event) {
					super.dragFinished(event);
				}

				@Override
				public void dragSetData(DragSourceEvent event) {
					ISelection selection = treeViewer.getSelection();
					TreeSelection ts = (TreeSelection) selection;
					Object s = ts.getFirstElement();
					event.data = s.toString();
				}

				@Override
				public void dragStart(DragSourceEvent event) {
					Control c = source.getControl();
					if (c instanceof Tree) {
						Tree t = (Tree) c;
						if (t.getSelection().length == 1) {
							return;
						}
					}
					event.doit = false;
				}
			});
		}
		DropTarget sqlTarget = new DropTarget(editor.getSqlTextEditor(),
				DND.DROP_MOVE);
		sqlTarget.setTransfer(new Transfer[] { TextTransfer.getInstance() });
		sqlTarget.addDropListener(new DropTargetAdapter() {

			@Override
			public void drop(DropTargetEvent event) {
				replaceSql();
			}

		});

	}

	/**
	 * register result table target, connect drag target with drag source
	 * 
	 * @param table
	 */
	public void addTableDropTarget(Table table) {
		DropTarget resultTarget = new DropTarget(table, DND.DROP_MOVE);
		resultTarget.setTransfer(new Transfer[] { TextTransfer.getInstance() });
		resultTarget.addDropListener(new DropTargetAdapter() {

			@Override
			public void drop(DropTargetEvent event) {
				boolean isSuccess = replaceSql();
				if (isSuccess) {
					editor.getRunItem().notifyListeners(SWT.Selection,
							new Event());
				}
			}

		});
	}

	private String getScript() {
		String sql = null;
		ISchemaNode selectedNode = getSelectionNode();
		if (selectedNode != null) {
			CubridDatabase db = selectedNode.getDatabase();
			DatabaseInfo dbInfo = db.getDatabaseInfo();
			GetAllAttrTask task = new GetAllAttrTask(dbInfo);
			task.setClassName(selectedNode.getName());
			task.getDbAllAttrListTaskExcute();
			if (task.getErrorMsg() != null)
				return "";
			List<DBAttribute> allAttrList = task.getAllAttrList();
			sql = "SELECT ";
			for (DBAttribute attr : allAttrList) {
				if (attr.isClassAttribute())
					sql += " class \"" + attr.getName() + "\" ,";
				else
					sql += " \"" + attr.getName() + "\" ,";
			}
			sql = sql.substring(0, sql.length() - 1);
			sql += " FROM \"" + selectedNode.getName() + "\";\n";
		}
		return sql;
	}

	private ISchemaNode getSelectionNode() {
		ISelection selection = treeViewer.getSelection();
		if (selection instanceof TreeSelection) {
			TreeSelection ts = (TreeSelection) selection;
			if (ts.size() != 1) {
				return null;
			}
			Object s = ts.getFirstElement();
			if (s instanceof ICubridNode) {
				ICubridNode node = (ICubridNode) s;
				CubridNodeType type = node.getType();
				if (type == CubridNodeType.SYSTEM_TABLE
						|| type == CubridNodeType.SYSTEM_VIEW
						|| type == CubridNodeType.USER_TABLE
						|| type == CubridNodeType.USER_PARTITIONED_TABLE
						|| type == CubridNodeType.USER_VIEW
						|| type == CubridNodeType.USER_PARTITIONED_TABLE_FOLDER) {
					return (ISchemaNode) node;
				}
			}
		}
		return null;
	}

	private boolean replaceSql() {
		ISchemaNode selectedNode = getSelectionNode();
		boolean changed = false;
		if (selectedNode != null) {
			CubridDatabase db = selectedNode.getDatabase();
			CubridDatabase selectedDb = editor.getSelectedDatabase();
			if (!selectedDb.getId().equals(db.getId())) {
				changed = true;
				if (editor.isTransaction()) {
					MessageDialog dialog = new MessageDialog(
							editor.getSite().getShell(),
							com.cubrid.cubridmanager.ui.common.Messages.titleConfirm,
							null, Messages.bind(Messages.connCloseConfirm,
									new String[] { selectedDb.getLabel() }),
							MessageDialog.QUESTION, new String[] {
									Messages.btnYes, Messages.btnNo,
									Messages.cancel }, 2) {

						@Override
						protected void buttonPressed(int buttonId) {
							switch (buttonId) {
							case 0:
								editor.commit();
								setReturnCode(0);
								close();
								break;
							case 1:
								editor.rollback();
								setReturnCode(1);
								close();
								break;
							case 2:
								setReturnCode(2);
								close();
							default:
								break;
							}
						}

					};
					int returnVal = dialog.open();
					if (returnVal == 2 || returnVal == -1) {
						return false;
					}
				} else {
					boolean confirm = CommonTool.openConfirmBox(
							editor.getSite().getShell(),
							Messages.changeDbConfirm);
					if (!confirm) {
						return false;
					}
				}
				editor.shutDownConnection();
				editor.connect(db);
			}
			String sql = getScript();
			if (sql != null) {
				int start = 0;
				if (changed) {
					editor.getSqlTextEditor().setText(sql);
				} else {
					start = editor.getSqlTextEditor().getText().length();
					editor.getSqlTextEditor().append(sql);
				}
				int end = start + sql.length();
				editor.getSqlTextEditor().setSelection(start, end);
				//editor.format();
				return true;
			}

		}
		return false;
	}

}
