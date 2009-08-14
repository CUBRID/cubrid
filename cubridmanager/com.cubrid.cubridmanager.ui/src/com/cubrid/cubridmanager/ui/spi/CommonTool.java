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

package com.cubrid.cubridmanager.ui.spi;

import java.lang.Character.UnicodeBlock;
import java.text.DecimalFormat;
import java.text.SimpleDateFormat;
import java.util.Date;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.CheckboxTableViewer;
import org.eclipse.jface.viewers.IContentProvider;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.jface.viewers.ViewerSorter;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;
import com.cubrid.cubridmanager.ui.common.navigator.CubridDeferredTreeContentManager;
import com.cubrid.cubridmanager.ui.common.navigator.DeferredContentProvider;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This tool class provide a lot of common convinence method
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CommonTool {
	private static final Logger logger = LogUtil.getLogger(CommonTool.class);
	private static Clipboard clipboard = null;

	/**
	 * 
	 * Convert string to int
	 * 
	 * @param str
	 * @return
	 */
	public static int str2Int(String str) {
		int ret = 0;
		try {
			ret = Integer.parseInt(str);
		} catch (Exception e) {
			ret = 0;
			logger.error(e);
		}
		return ret;
	}

	/**
	 * 
	 * Convert string to double
	 * 
	 * @param str
	 * @return
	 */
	public static double str2Double(String str) {
		double ret = 0;
		try {
			ret = Double.parseDouble(str);
		} catch (Exception e) {
			ret = 0;
			logger.error(e);
		}
		return ret;
	}

	/**
	 * Convert "ON","y" or other string to boolean
	 * 
	 * @param str
	 * @return
	 */
	public static boolean str2Boolean(String str) {
		if (str == null) {
			return false;
		}
		if (str.equals(OnOffType.ON.getText())
				|| str.equals(YesNoType.Y.getText())) {
			return true;
		}
		return false;
	}

	/**
	 * 
	 * Center this shell
	 * 
	 * @param shell
	 */
	public static void centerShell(Shell shell) {
		if (shell == null)
			return;

		Rectangle mainBounds;
		Rectangle displayBounds = shell.getDisplay().getClientArea();
		if (shell.getShell() == null)
			mainBounds = displayBounds;
		else
			mainBounds = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell().getBounds();

		Rectangle shellBounds = shell.getBounds();

		int x = mainBounds.x + (mainBounds.width - shellBounds.width) / 2;
		int y = mainBounds.y + (mainBounds.height - shellBounds.height) / 2;

		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;

		if ((x + shellBounds.width) > displayBounds.width)
			x = displayBounds.width - shellBounds.width;
		if ((y + shellBounds.height) > displayBounds.height)
			y = displayBounds.height - shellBounds.height;

		shell.setLocation(x, y);
	}

	/**
	 * Return clipboard object
	 * 
	 * @return
	 */
	public static synchronized Clipboard getClipboard() {
		if (clipboard == null) {
			clipboard = new Clipboard(Display.getDefault());
			PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell().addDisposeListener(
					new DisposeListener() {
						public void widgetDisposed(DisposeEvent e) {
							clipboard.dispose();
							clipboard = null;
						}
					});
		}
		return clipboard;
	}

	/**
	 * 
	 * create grid data
	 * 
	 * @param style
	 * @param horSpan
	 * @param verSpan
	 * @param widthHint
	 * @return
	 */
	public static GridData createGridData(int style, int horSpan, int verSpan,
			int widthHint, int heightHint) {
		GridData gridData = new GridData(style);
		gridData.horizontalSpan = horSpan;
		gridData.verticalSpan = verSpan;
		if (widthHint >= 0)
			gridData.widthHint = widthHint;
		if (heightHint >= 0)
			gridData.heightHint = heightHint;
		return gridData;
	}

	/**
	 * Create Grid data
	 * 
	 * @param horSpan
	 * @param verSpan
	 * @param widthHint
	 * @param heightHint
	 * @return
	 */
	public static GridData createGridData(int horSpan, int verSpan,
			int widthHint, int heightHint) {
		GridData gridData = new GridData();
		gridData.horizontalSpan = horSpan;
		gridData.verticalSpan = verSpan;
		if (heightHint >= 0)
			gridData.heightHint = heightHint;
		if (widthHint >= 0)
			gridData.widthHint = widthHint;
		return gridData;
	}

	/**
	 * 
	 * Open Message dialog
	 * 
	 * @param sh
	 * @param dialogImageType
	 * @param title
	 * @param msg
	 * @param dialogButton
	 * @return
	 */
	public static int openMsgBox(Shell sh, int dialogImageType, String title,
			String msg, String[] dialogButton) {
		if (sh == null) {
			sh = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
		}
		MessageDialog dialog = new MessageDialog(sh, title, null, msg,
				dialogImageType, dialogButton, 0);
		return dialog.open();
	}

	/**
	 * 
	 * Open confirm box
	 * 
	 * @param sh
	 * @param msg
	 * @return
	 */
	public static boolean openConfirmBox(Shell sh, String msg) {
		return openMsgBox(sh, MessageDialog.WARNING, Messages.titleConfirm,
				msg, new String[] { Messages.btnYes, Messages.btnNo }) == 0;
	}

	/**
	 * 
	 * Open confirm box
	 * 
	 * @param msg
	 * @return
	 */
	public static boolean openConfirmBox(String msg) {
		return openConfirmBox(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				msg);
	}

	/**
	 * 
	 * Open error box
	 * 
	 * @param msg
	 */
	public static void openErrorBox(String msg) {
		openErrorBox(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				msg);
	}

	/**
	 * 
	 * Open error box
	 * 
	 * @param sh
	 * @param msg
	 */
	public static void openErrorBox(Shell sh, String msg) {
		openMsgBox(sh, MessageDialog.ERROR, Messages.titleError, msg,
				new String[] { Messages.btnOk });
	}

	/**
	 * 
	 * Open information box
	 * 
	 * @param sh
	 * @param title
	 * @param msg
	 */
	public static void openInformationBox(Shell sh, String title, String msg) {
		openMsgBox(sh, MessageDialog.INFORMATION, title, msg,
				new String[] { Messages.btnOk });
	}

	/**
	 * 
	 * Open information box
	 * 
	 * @param title
	 * @param msg
	 */
	public static void openInformationBox(String title, String msg) {
		openInformationBox(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				title, msg);
	}

	/**
	 * 
	 * Open Warning box
	 * 
	 * @param title
	 * @param msg
	 */
	public static void openWarningBox(String msg) {
		openInformationBox(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				Messages.titleWarning, msg);
	}

	/**
	 * 
	 * Create the common table viewer that can be sorted by TableViewerSorter
	 * object,this viewer's input object must be List<Map<String,Object>> and
	 * Map's key must be column index,Map's value of the column must be String.
	 * 
	 * @param parent
	 * @param sorter
	 * @param columnNameArr
	 * @param gridData
	 * @return
	 */
	public static TableViewer createCommonTableViewer(Composite parent,
			ViewerSorter sorter, final String[] columnNameArr, GridData gridData) {
		final TableViewer tableViewer = new TableViewer(parent, SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER | SWT.H_SCROLL | SWT.FULL_SELECTION);
		tableViewer.setContentProvider(new TableContentProvider());
		tableViewer.setLabelProvider(new TableLabelProvider());
		if (sorter != null)
			tableViewer.setSorter(sorter);

		tableViewer.getTable().setLinesVisible(true);
		tableViewer.getTable().setHeaderVisible(true);
		tableViewer.getTable().setLayoutData(gridData);

		for (int i = 0; i < columnNameArr.length; i++) {
			final TableColumn tblColumn = new TableColumn(
					tableViewer.getTable(), SWT.LEFT);
			tblColumn.setText(columnNameArr[i]);
			if (sorter != null) {
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
					}
				});
			}
			tblColumn.pack();
		}
		return tableViewer;
	}

	/**
	 * 
	 * Create the common checkbox table viewer that can be sorted by
	 * TableViewerSorter object,this viewer's input object must be List<Map<String,Object>>
	 * and Map's key must be column index,Map's value of the column must be
	 * String.
	 * 
	 * @param parent
	 * @param sorter
	 * @param columnNameArr
	 * @param gridData
	 * @return
	 */
	public static TableViewer createCheckBoxTableViewer(Composite parent,
			ViewerSorter sorter, final String[] columnNameArr, GridData gridData) {
		final CheckboxTableViewer tableViewer = CheckboxTableViewer.newCheckList(
				parent, SWT.V_SCROLL | SWT.MULTI | SWT.BORDER | SWT.H_SCROLL
						| SWT.FULL_SELECTION);
		tableViewer.setContentProvider(new TableContentProvider());
		tableViewer.setLabelProvider(new TableLabelProvider());
		if (sorter != null)
			tableViewer.setSorter(sorter);

		tableViewer.getTable().setLinesVisible(true);
		tableViewer.getTable().setHeaderVisible(true);
		tableViewer.getTable().setLayoutData(gridData);

		for (int i = 0; i < columnNameArr.length; i++) {
			final TableColumn tblColumn = new TableColumn(
					tableViewer.getTable(), SWT.LEFT);
			tblColumn.setText(columnNameArr[i]);
			if (sorter != null) {
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
					}
				});
			}
			tblColumn.pack();
		}
		return tableViewer;
	}

	/**
	 * 
	 * Reload the children of this node and restore the expanded status
	 * 
	 * @param viewer
	 * @param node
	 */
	public static void refreshNavigatorTree(TreeViewer viewer, ICubridNode node) {
		if (node.getLoader() != null) {
			node.getLoader().setLoaded(false);
		}
		if (!viewer.getExpandedState(node)) {
			node.removeAllChild();
		}
		Object[] expandedElements = viewer.getExpandedElements();
		IContentProvider contentProvider = viewer.getContentProvider();
		if (contentProvider instanceof DeferredContentProvider) {
			CubridDeferredTreeContentManager manager = ((DeferredContentProvider) contentProvider).getDeferredTreeContentManager();
			if (manager != null) {
				manager.setExpandedElements(expandedElements);
			}
		}
		viewer.refresh(node, true);
	}

	/**
	 * 
	 * Clear the expanded elements of treeviewer
	 * 
	 * @param tv
	 */
	public static void clearExpandedElements(TreeViewer tv) {
		IContentProvider contentProvider = tv.getContentProvider();
		if (contentProvider instanceof DeferredContentProvider) {
			CubridDeferredTreeContentManager manager = ((DeferredContentProvider) contentProvider).getDeferredTreeContentManager();
			if (manager != null) {
				manager.setExpandedElements(null);
			}
		}
	}

	/**
	 * 
	 * Get line separator
	 * 
	 * @return
	 */
	public static String getLineSeparator() {
		return System.getProperty("line.separator");
	}

	/**
	 * 
	 * Validate and check idenfifier
	 * 
	 * @param identifier
	 * @return
	 */
	public static String validateCheckInIdentifier(String identifier) {
		String ret_string; // add string if Identifier has invalid string. 

		ret_string = ""; // Last status is "", it is valid identifier.
		if (identifier == null || identifier.length() <= 0) {
			ret_string = "empty";
			return ret_string;
		}

		if (identifier.indexOf(" ") >= 0)
			ret_string += " ";
		if (identifier.indexOf("\t") >= 0)
			ret_string += "\t";
		if (identifier.indexOf("/") >= 0)
			ret_string += "/";
		if (identifier.indexOf(".") >= 0)
			ret_string += ".";
		if (identifier.indexOf("~") >= 0)
			ret_string += "~";
		if (identifier.indexOf(",") >= 0)
			ret_string += ",";
		if (identifier.indexOf("\\") >= 0)
			ret_string += "\\";
		if (identifier.indexOf("\"") >= 0)
			ret_string += "\"";
		if (identifier.indexOf("|") >= 0)
			ret_string += "|";
		if (identifier.indexOf("]") >= 0)
			ret_string += "]";
		if (identifier.indexOf("[") >= 0)
			ret_string += "[";
		if (identifier.indexOf("}") >= 0)
			ret_string += "}";
		if (identifier.indexOf("{") >= 0)
			ret_string += "{";
		if (identifier.indexOf(")") >= 0)
			ret_string += ")";
		if (identifier.indexOf("(") >= 0)
			ret_string += "(";
		if (identifier.indexOf("=") >= 0)
			ret_string += "=";
		if (identifier.indexOf("-") >= 0)
			ret_string += "-";
		if (identifier.indexOf("+") >= 0)
			ret_string += "+";
		if (identifier.indexOf("?") >= 0)
			ret_string += "?";
		if (identifier.indexOf("<") >= 0)
			ret_string += "<";
		if (identifier.indexOf(">") >= 0)
			ret_string += ">";
		if (identifier.indexOf(":") >= 0)
			ret_string += ":";
		if (identifier.indexOf(";") >= 0)
			ret_string += ";";
		if (identifier.indexOf("!") >= 0)
			ret_string += "!";
		if (identifier.indexOf("'") >= 0)
			ret_string += "'";
		if (identifier.indexOf("@") >= 0)
			ret_string += "@";
		if (identifier.indexOf("$") >= 0)
			ret_string += "$";
		if (identifier.indexOf("^") >= 0)
			ret_string += "^";
		if (identifier.indexOf("&") >= 0)
			ret_string += "&";
		if (identifier.indexOf("*") >= 0)
			ret_string += "*";

		return ret_string;
	}

	/**
	 * format the number
	 * 
	 * 
	 * @param money
	 * @param format:"##,###.##","##.##"
	 * @return
	 */
	public static String formatNumber(double num, String format) {
		DecimalFormat df = new DecimalFormat(format);
		return df.format(num);
	}

	/**
	 * return true if a string s is a ascii string.
	 * 
	 * @param s
	 * @return
	 */
	public static boolean isASCII(String s) {
		for (int i = 0, len = s.length(); i < len; i++) {
			if (!UnicodeBlock.of(s.charAt(i)).equals(UnicodeBlock.BASIC_LATIN)) {
				return false;
			}
		}
		return true;
	}

	/**
	 * format the number
	 * 
	 * 
	 * @param money
	 * @param format:"##,###.##","##.##"
	 * @return
	 */
	public static String formatNumber(float num, String format) {
		DecimalFormat df = new DecimalFormat(format);
		return df.format(num);
	}

	/**
	 * format the string in fixed-length
	 * 
	 * 
	 * @param targetStr
	 * @param strLength
	 * @return
	 */
	public static String formatString(String targetStr, int strLength,
			boolean isRight) {
		if (targetStr == null) {
			return null;
		}
		int curLength = targetStr.getBytes().length;
		if (targetStr != null && curLength > strLength) {
			return targetStr;
			//			targetStr = targetStr.substring(0, strLength);
		}
		String newString = "";
		int cutLength = strLength - curLength;
		for (int i = 0; i < cutLength; i++)
			newString += " ";
		return isRight ? (targetStr + newString) : (newString + targetStr);
	}

	/**
	 * 
	 * This method encodes the url, removes the spaces from the url and replaces
	 * the same with <code>"%20"</code>.
	 * 
	 * @param input
	 * @return
	 */
	public static String urlEncodeForSpaces(char[] input) {
		StringBuffer retu = new StringBuffer(input.length);
		for (int i = 0; i < input.length; i++) {
			if (input[i] == ' ') {
				retu.append("%20");
			} else {
				retu.append(input[i]);
			}
		}
		return retu.toString();
	}

	/**
	 * 
	 * Register context menu copy action for styled text
	 * 
	 * @param text
	 * @param isEditable
	 */
	public static void registerContextMenu(final StyledText text,
			boolean isEditable) {
		if (text == null || text.isDisposed()) {
			return;
		}
		Menu menu = new Menu(text.getShell(), SWT.POP_UP);
		final MenuItem itemCopy = new MenuItem(menu, SWT.PUSH);
		itemCopy.setText(Messages.msgContextMenuCopy);
		itemCopy.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				copyContentToClipboard(text);
			}
		});
		if (isEditable) {
			final MenuItem itemPaste = new MenuItem(menu, SWT.PUSH);
			itemPaste.setText(Messages.msgContextMenuPaste);
			itemPaste.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					String content = getTextContentFromClipboard();
					if (content != null) {
						text.insert(content);
					}
				}
			});
		}
		text.setMenu(menu);
	}

	/**
	 * 
	 * Copy the styled text conent to clipboard
	 * 
	 * @param text
	 */
	public static void copyContentToClipboard(StyledText text) {
		TextTransfer textTransfer = TextTransfer.getInstance();
		Clipboard clipboard = CommonTool.getClipboard();
		String data = text.getSelectionText();
		if (data == null || data.trim().length() == 0) {
			data = text.getText();
		}
		if (data != null && !data.equals("")) {
			clipboard.setContents(new Object[] { data },
					new Transfer[] { textTransfer });
		}
	}

	/**
	 * 
	 * Get text content from clipboard
	 * 
	 * @return
	 */
	public static String getTextContentFromClipboard() {
		Clipboard clipboard = CommonTool.getClipboard();
		TextTransfer textTransfer = TextTransfer.getInstance();
		Object obj = clipboard.getContents(textTransfer);
		if (obj != null) {
			return (String) obj;
		} else {
			return "";
		}
	}

	/**
	 * 
	 * Format date for string according to data format ex:(1)yyyy-MM-dd
	 * hh:mm:ss.SSS (2)yyyy-MM-dd hh:mm:ss
	 * 
	 * @param date
	 * @param sf
	 * @return
	 */
	public static String formatDate(Date date, String sf) {
		SimpleDateFormat dateformat = new SimpleDateFormat(sf);
		return dateformat.format(date);
	}

}
