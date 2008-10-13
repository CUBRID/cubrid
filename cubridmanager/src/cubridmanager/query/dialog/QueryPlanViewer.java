package cubridmanager.query.dialog;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.query.dialog.QueryPlan;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridData;

public class QueryPlanViewer extends Dialog {
	private QueryPlan qe;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,43"
	private Composite cmpBtnArea = null;
	private Text txtPlan = null;
	private Button btnClose = null;

	public QueryPlanViewer(QueryPlan qe, Shell parent) {
		super(parent, SWT.DIALOG_TRIM | SWT.MODELESS);
		this.qe = qe;
	}

	public void open(String str) {
		createSShell();

		sShell.setText(Messages.getString("TITLE.QUERYPLAN1"));
		txtPlan.setText(str);

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = getParent().getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch()) {
				display.sleep();
			}
		}
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.SHELL_TRIM);
		createSashForm();
		createCmpBtnArea();
		sShell.setSize(new org.eclipse.swt.graphics.Point(400, 300));
		GridLayout gridLayout = new GridLayout();
		sShell.setLayout(gridLayout);

		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				qe.isQueryPlanViewerDlgOpen = false;
			}
		});
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm() {

		GridData gridData = new GridData(GridData.FILL_BOTH);
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.heightHint = 500;
		gridData.widthHint = 500;
		txtPlan = new Text(sShell, SWT.BORDER | SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL);
		txtPlan.setLayoutData(gridData);
		txtPlan.setEditable(false);
		txtPlan.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void createCmpBtnArea() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 1;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(gridLayout1);
		cmpBtnArea.setLayoutData(gridData1);

		GridData gridData4 = new GridData();
		gridData4.widthHint = 75;
		btnClose = new Button(cmpBtnArea, SWT.NONE);
		btnClose.setLayoutData(gridData4);
		btnClose.setText(Messages.getString("BUTTON.CLOSE"));
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

} // @jve:decl-index=0:visual-constraint="12,9"
