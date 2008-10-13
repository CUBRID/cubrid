package cubridmanager.diag.view;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;

import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.diag.DiagActivityMonitorTemplate;

public class ActivityTemplate extends ViewPart {

	public static final String ID = "workview.ActivityTemplate"; 
	public static String CurrentSelect = null;
	public static String CurrentText = null;
	private Composite top = null;
	private Table table = null;
	private Label label = null;
	public DiagActivityMonitorTemplate currentTemplate = null;

	public ActivityTemplate() {
		super();
		currentTemplate = MainRegistry.getActivityTemplateByName(MainRegistry
				.GetCurrentSiteName(), CurrentText);
	}

	public void createPartControl(Composite parent) {
		top = new Composite(parent, SWT.NONE);
		top.setLayout(null);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createTable();
		label = new Label(top, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(3, 5, 281, 28));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label.setText(Messages.getString("TITLE.ACTIVITYTEMPLATE"));
		setinformation();
	}

	private void setinformation() {
		top.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				adjustWindows();
			}
		});

		insertListData();
	}

	public void adjustWindows() {
		Rectangle toprect = top.getBounds();
		Rectangle wrkrect = new Rectangle(toprect.x + 5, toprect.y + 5,
				toprect.width - 10, toprect.height - 10);
		wrkrect.height = 30;
		label.setBounds(wrkrect);

		wrkrect.x = 0;
		wrkrect.y = 35; // first window's height+5
		wrkrect.width = toprect.width;
		wrkrect.height = toprect.height - 40; // first window's height+10
		if (wrkrect.height <= 0)
			wrkrect.height = 0;
		table.setBounds(wrkrect);
	}

	public void insertListData() {
		String monitorTarget = new String();
		boolean needComma = false;
		TableItem item1 = new TableItem(table, SWT.NONE);
		item1.setText(0, currentTemplate.templateName);
		item1.setText(1, currentTemplate.desc);
		if (currentTemplate.targetdb.length() > 0)
			item1.setText(2, currentTemplate.targetdb);
		if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_REQ()) {
			monitorTarget += currentTemplate.activity_config
					.GET_CLIENT_ACTINFO_CAS_REQ_STRING();
			needComma = true;
		}
		if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_TRAN()) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.activity_config
					.GET_CLIENT_ACTINFO_CAS_TRAN_STRING();
			needComma = true;
		}
		if (currentTemplate.activity_config.dbData.act_query_fullscan) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.activity_config.dbData.act_query_fullscanString;
			needComma = true;
		}
		if (currentTemplate.activity_config.dbData.act_lock_deadlock) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.activity_config.dbData.act_lock_deadlockString;
			needComma = true;
		}
		if (currentTemplate.activity_config.dbData.act_buffer_page_read) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.activity_config.dbData.act_buffer_page_readString;
			needComma = true;
		}
		if (currentTemplate.activity_config.dbData.act_buffer_page_write) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.activity_config.dbData.act_buffer_page_writeString;
		}
		item1.setText(3, monitorTarget);
	}

	public void setFocus() {
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(top, SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);

		table
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 40, 300,
						135));
		TableLayout tlayout = new TableLayout();

		tlayout.addColumnData(new ColumnWeightData(20, 80, false));
		tlayout.addColumnData(new ColumnWeightData(20, 80, false));
		tlayout.addColumnData(new ColumnWeightData(20, 80, false));
		tlayout.addColumnData(new ColumnWeightData(40, 200, false));
		tlayout.addColumnData(new ColumnWeightData(20, 80, false));

		table.setLayout(tlayout);

		TableColumn nameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn descColumn = new TableColumn(table, SWT.LEFT);
		TableColumn targetDBColumn = new TableColumn(table, SWT.LEFT);
		TableColumn contentsColumn = new TableColumn(table, SWT.LEFT);
		TableColumn filterColumn = new TableColumn(table, SWT.LEFT);

		nameColumn.setText(Messages.getString("LABEL.DIAGTEMPLATENAME"));
		descColumn.setText(Messages.getString("LABEL.DIAGTEMPLATEDESC"));
		targetDBColumn.setText(Messages.getString("LABEL.TARGETDATABASE"));
		contentsColumn
				.setText(Messages.getString("LABEL.DIAGTEMPLATECONTENTS"));
		filterColumn.setText(Messages.getString("LABEL.DIAGFILTER"));
	}
}
