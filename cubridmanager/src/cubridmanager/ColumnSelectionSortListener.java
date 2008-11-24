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

package cubridmanager;

import java.text.Collator;
import java.util.Locale;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;

public class ColumnSelectionSortListener implements SelectionListener {
	boolean desc = false;

	int columnIndex;

	Table table;

	private ColumnSelectionSortListener() {
		try {
			throw new Exception();
		} catch (Exception e) {
			CommonTool.ErrorBox("Do not support!");
		}
	}

	public ColumnSelectionSortListener(Table table, int columnIndex) {
		this.table = table;
		this.columnIndex = columnIndex;
	}

	public void widgetSelected(SelectionEvent e) {
		TableItem[] items = table.getItems();
		int numOfColumns = table.getColumnCount();
		Collator collator = Collator.getInstance(Locale.getDefault());
		for (int i = 1; i < items.length; i++) {
			String value1 = items[i].getText(columnIndex);
			for (int j = 0; j < i; j++) {
				String value2 = items[j].getText(columnIndex);
				if (desc) { // reverse sort
					if (collator.compare(value1, value2) > 0) {
						String[] values = new String[numOfColumns];
						for (int x = 0; x < table.getColumnCount(); x++)
							values[x] = items[i].getText(x);
						Object object = items[i].getData();
						items[i].dispose();
						TableItem item = new TableItem(table, SWT.NONE, j);
						item.setText(values);
						item.setData(object);
						items = table.getItems();
						break;
					}
				} else {
					if (collator.compare(value1, value2) < 0) {
						String[] values = new String[numOfColumns];
						for (int x = 0; x < table.getColumnCount(); x++)
							values[x] = items[i].getText(x);
						Object object = items[i].getData();
						items[i].dispose();
						TableItem item = new TableItem(table, SWT.NONE, j);
						item.setText(values);
						item.setData(object);
						items = table.getItems();
						break;
					}
				}
			}
		}
		desc = !desc;
	}

	public void widgetDefaultSelected(SelectionEvent e) {
		// TODO Auto-generated method stub

	}

}
