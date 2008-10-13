package cubridmanager.cubrid.view;

import java.text.NumberFormat;
import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Display;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.VolumeInfo;

public class DBSpace extends ViewPart {

	public static final String ID = "workview.DBSpace";
	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String VOL_GENERAL = "VOL_GENERAL";
	public static final String VOL_ACTIVE = "VOL_ACTIVE";
	public static final String VOL_ARCHIVE = "VOL_ARCHIVE";
	public static final String VOL_OBJECT = "VOL_OBJECT";
	public static String CurrentSelect = new String("");
	public static String CurrentObj = new String("");
	public static String CurrentVolumeType = new String("");
	private VolumeInfo objrec = null;
	private AuthItem DB_Auth = null;
	private ArrayList Volinfo = null;
	private Composite top = null;
	private Table table = null;

	public DBSpace() {
		super();
		if (CubridView.Current_db.length() < 1 || CurrentSelect.length() < 1)
			this.dispose();
		else {
			DB_Auth = MainRegistry.Authinfo_find(CubridView.Current_db);
			Volinfo = DB_Auth.Volinfo;
			for (int i = 0, n = Volinfo.size(); i < n; i++) {
				if (((VolumeInfo) Volinfo.get(i)).spacename.equals(CurrentObj)) {
					objrec = (VolumeInfo) Volinfo.get(i);
					break;
				}
			}
			if (objrec == null)
				this.dispose();
		}
	}

	public void createPartControl(Composite parent) {
		FillLayout flayout = new FillLayout();
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(flayout);
		createTable();
	}

	public void setFocus() {
		// TODO Auto-generated method stub
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		if (CurrentVolumeType.equals(VOL_ACTIVE))
			setPartName(Messages.getString("TREE.ACTIVE")
					+ Messages.getString("STRING.INFORMATION"));
		else if (CurrentVolumeType.equals(VOL_ARCHIVE))
			setPartName(Messages.getString("TREE.ARCHIVE")
					+ Messages.getString("STRING.INFORMATION"));
		else if (CurrentVolumeType.equals(VOL_GENERAL))
			setPartName(Messages.getString("TREE.GENERIC")
					+ Messages.getString("STRING.INFORMATION"));

		table = new Table(top, SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table
				.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 49, 272,
						95));

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VOLUMENAME"));
		item.setText(1, objrec.spacename);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VOLUMEPATH"));
		item.setText(1, objrec.location);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CHANGEDATE"));
		item.setText(1, CommonTool.convertYYYYMMDD(objrec.date));
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VOLUMETYPE"));
		item.setText(1, objrec.type.toUpperCase().replaceAll("_", " "));
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.TOTALSIZEPAGES"));
		item.setText(1, objrec.tot);
		if (!objrec.type.equals("Active_log")
				&& !objrec.type.equals("Archive_log")) {
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.REMAINSIZEPAGES"));
			item.setText(1, objrec.free);
		}
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.SIZEMB"));
		double mb = (DB_Auth.pagesize * CommonTool.atol(objrec.tot))
				/ (double) MainConstants.MEGABYTES;
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(2);
		nf.setGroupingUsed(false);
		item.setText(1, nf.format(mb));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		table.setLayout(tlayout);
	}
}
