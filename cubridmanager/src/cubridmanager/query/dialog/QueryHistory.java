package cubridmanager.query.dialog;

import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Messages;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Group;

public class QueryHistory {
	private Shell sShell = null;
	private Button btnAppend = null;
	private Button btnReplace = null;
	private Button btnClipboard = null;
	private Button btnRemove = null;
	private Button btnClose = null;
	private Label lblFilter = null;
	private Text txtFilter = null;
	private Button btnGo = null;
	private Table tblHistory = null;
	private Rectangle rect = null;
	private Composite top = null;
	private Group grpButtons = null;

	public QueryHistory() {
	}

	public void widgetSelected(SelectionEvent event) {
		createSShell();
		sShell.open();
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		sShell = new Shell(SWT.SHELL_TRIM);
		sShell.setText(Messages.getString("QEDIT.QUERYHISTORY"));
		sShell.setSize(new Point(800, 600));

		createTop();
		createGrpButtons();
		adjustWindows();

		tblHistory.addPaintListener(new org.eclipse.swt.events.PaintListener() {
			public void paintControl(org.eclipse.swt.events.PaintEvent e) {
				// TODO Auto-generated Event stub paintControl()
				// adjustWindows();
			}
		});

		sShell.addShellListener(new org.eclipse.swt.events.ShellAdapter() {
			public void shellClosed(org.eclipse.swt.events.ShellEvent e) {
				// TODO Auto-generated Event stub shellClosed()
				sShell = null;
			}
		});
		sShell.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
			public void controlResized(org.eclipse.swt.events.ControlEvent e) {
				// TODO Auto-generated Event stub controlResized()
				adjustWindows();
			}
		});
	}

	private void adjustWindows() {
		rect = sShell.getBounds();
		Rectangle wrkrect = new Rectangle(0, 80, rect.width, rect.height - 110);
		top.setBounds(wrkrect);
		tblHistory.setBounds(0, 5, wrkrect.width - 5, wrkrect.height - 5);
		grpButtons.setSize(sShell.getSize().x - 10, 75);
	}

	/**
	 * This method initializes top
	 * 
	 */
	private void createTop() {
		top = new Composite(sShell, SWT.NONE);
		createTblHistory();
		top.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 80, 790, sShell
				.getBounds().height - 110));
	}

	/**
	 * This method initializes tblHistory
	 * 
	 */
	private void createTblHistory() {
		Rectangle topRect = top.getBounds();

		tblHistory = new Table(top, SWT.FULL_SELECTION);
		tblHistory.setHeaderVisible(true);
		tblHistory.setLinesVisible(true);
		tblHistory.setBounds(topRect.x + 5, topRect.y + 5, topRect.width - 5,
				topRect.height - 5);

		TableColumn colNum = new TableColumn(tblHistory, SWT.NONE);
		colNum.setWidth(20);
		colNum.setResizable(false);
		TableColumn colSQL = new TableColumn(tblHistory, SWT.NONE);
		colSQL.setWidth(500);
		colSQL.setText(Messages.getString("QEDIT.SQL"));
		TableColumn colExecDate = new TableColumn(tblHistory, SWT.NONE);
		colExecDate.setWidth(200);
		colExecDate.setText(Messages.getString("QEDIT.EXECDATE"));
		TableColumn colUser = new TableColumn(tblHistory, SWT.NONE);
		colUser.setWidth(65);
		colUser.setText(Messages.getString("QEDIT.USER"));
	}

	/**
	 * This method initializes grpButtons
	 * 
	 */
	private void createGrpButtons() {
		grpButtons = new Group(sShell, SWT.NONE);
		grpButtons.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 0, 790,
				75));

		btnAppend = new Button(grpButtons, SWT.NONE);
		btnReplace = new Button(grpButtons, SWT.NONE);
		btnClipboard = new Button(grpButtons, SWT.NONE);
		btnRemove = new Button(grpButtons, SWT.NONE);
		btnClose = new Button(grpButtons, SWT.NONE);
		lblFilter = new Label(grpButtons, SWT.NONE);
		lblFilter.setText(Messages.getString("QEDIT.FILTER"));
		txtFilter = new Text(grpButtons, SWT.BORDER);
		btnGo = new Button(grpButtons, SWT.NONE);

		btnAppend.setText(Messages.getString("QEDIT.APPEND"));
		btnReplace.setText(Messages.getString("QEDIT.REPLACEEDIT"));
		btnClipboard.setText(Messages.getString("QEDIT.CLIPBOARD"));
		btnRemove.setText(Messages.getString("QEDIT.REMOVE"));
		btnClose.setText(Messages.getString("QEDIT.CLOSE"));
		btnGo.setText(Messages.getString("QEDIT.FIND"));

		lblFilter.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 52, 31, 18));
		txtFilter.setBounds(new org.eclipse.swt.graphics.Rectangle(45, 49, 180, 18));
		btnAppend.setBounds(new org.eclipse.swt.graphics.Rectangle(5, 10, 100, 22));
		btnReplace.setBounds(new org.eclipse.swt.graphics.Rectangle(105, 10, 100, 22));
		btnClipboard.setBounds(new org.eclipse.swt.graphics.Rectangle(205, 10, 100, 22));
		btnRemove.setBounds(new org.eclipse.swt.graphics.Rectangle(305, 10, 100, 22));
		btnClose.setBounds(new org.eclipse.swt.graphics.Rectangle(420, 10, 100, 22));
		btnClose.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// TODO Auto-generated Event stub
						// widgetDefaultSelected()
						sShell.close();
						sShell = null;
					}
				});
		btnGo.setBounds(new org.eclipse.swt.graphics.Rectangle(225, 47, 80,
						22));
	}
}
