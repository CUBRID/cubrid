package cubridmanager.diag.view;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.diag.DiagStatusMonitorTemplate;

public class StatusTemplate extends ViewPart {

	public static final String ID = "workview.StatusTemplate";
	private Composite top = null;
	private Table table = null;
	private Label label = null;
	public static String CurrentSelect = null;
	public static String CurrentText = null;
	public DiagStatusMonitorTemplate currentTemplate = null;

	public StatusTemplate() {
		super();
		// TODO site name
		currentTemplate = MainRegistry.getStatusTemplateByName(MainRegistry
				.GetCurrentSiteName(), CurrentText);
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		top.setLayout(null);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createTable();

		label = new Label(top, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 1, 221, 37));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 18,
				SWT.NORMAL));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setText(Messages.getString("TITLE.STATUSTEMPLATE"));

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
		item1.setText(2, currentTemplate.targetdb);
		item1.setText(3, currentTemplate.sampling_term);
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_REQ()) {
			monitorTarget += currentTemplate.monitor_config
					.GET_CLIENT_MONITOR_INFO_CAS_REQ_STRING();
			needComma = true;
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_TRAN()) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config
					.GET_CLIENT_ACTINFO_CAS_TRAN_STRING();
			needComma = true;
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_ACT_SESSION()) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config
					.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_STRING();
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.query_open_page) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_query_open_pageString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.query_opened_page) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_query_opened_pageString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.query_slow_query) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_query_slow_queryString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.query_full_scan) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_query_full_scanString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.conn_cli_request) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_need_mon_cub_connString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.conn_aborted_clients) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_conn_aborted_clientsString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.conn_conn_req) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_conn_conn_reqString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.conn_conn_reject) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_conn_conn_rejectString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.buffer_page_write) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_buffer_page_writeString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.buffer_page_read) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_buffer_page_readString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.lock_deadlock) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_lock_deadlockString;
			needComma = true;
		}
		if (currentTemplate.monitor_config.dbData.lock_request) {
			if (needComma)
				monitorTarget += ", ";
			monitorTarget += currentTemplate.monitor_config.dbData.status_lock_requestString;
			needComma = true;
		}

		item1.setText(4, monitorTarget);
	}

	public void setFocus() {
		// TODO Auto-generated method stub
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 44, 282,
						131));

		TableLayout tlayout = new TableLayout();

		tlayout.addColumnData(new ColumnWeightData(15, 50, false));
		tlayout.addColumnData(new ColumnWeightData(20, 100, false));
		tlayout.addColumnData(new ColumnWeightData(10, 50, false));
		;
		tlayout.addColumnData(new ColumnWeightData(10, 50, false));
		tlayout.addColumnData(new ColumnWeightData(55, 300, false));

		table.setLayout(tlayout);

		TableColumn nameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn descColumn = new TableColumn(table, SWT.LEFT);
		TableColumn targetDBColumn = new TableColumn(table, SWT.LEFT);
		TableColumn samplingtermColumn = new TableColumn(table, SWT.LEFT);
		TableColumn contentsColumn = new TableColumn(table, SWT.LEFT);

		nameColumn.setText(Messages.getString("LABEL.DIAGTEMPLATENAME"));
		descColumn.setText(Messages.getString("LABEL.DIAGTEMPLATEDESC"));
		targetDBColumn.setText(Messages.getString("LABEL.TARGETDATABASE"));
		samplingtermColumn.setText(Messages
				.getString("LABEL.DIAGSAMPLINGTERM_SEC"));
		contentsColumn
				.setText(Messages.getString("LABEL.DIAGTEMPLATECONTENTS"));
	}
}
