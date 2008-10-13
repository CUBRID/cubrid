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
