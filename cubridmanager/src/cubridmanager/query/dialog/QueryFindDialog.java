package cubridmanager.query.dialog;

import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Group;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.query.view.QueryEditor;

public class QueryFindDialog extends Dialog {
	private Shell sShell = null;
	private Composite cmpLeft = null;
	private Composite cmpRight = null;
	private Composite cmpTexts = null;
	private Composite cmpOptions = null;
	private Composite cmpOptLeft = null;
	private Composite cmpOptRight = null;
	private Label lblFind = null;
	private Text txtFind = null;
	private Label lblReplace = null;
	private Text txtReplace = null;
	private Button btnFind = null;
	private Button btnReplace = null;
	private Button btnReplaceAll = null;
	private Button btnClose = null;
	private Button chkCaseSensitive = null;
	private Button chkWrapSearch = null;
	private Button chkWholeWord = null;
	private Group grpDirection = null;
	private Button radioUp = null;
	private Button radioDown = null;
	private boolean isFind = false;

	public QueryFindDialog(boolean prmIsFind) {
		super(Application.mainwindow.getShell(), SWT.DIALOG_TRIM | SWT.MODELESS);

		isFind = prmIsFind;
	}

	public QueryFindDialog(String txt, boolean prmIsFind) {
		this(prmIsFind);

	}

	public void open(String searchText) {
		QueryEditor qe = MainRegistry.getCurrentQueryEditor();
		if (qe == null)
			return;

		createSShell();
		txtFind.setText(searchText);
		radioDown.setSelection(!qe.isCurUp);
		radioUp.setSelection(qe.isCurUp);
		chkCaseSensitive.setSelection(qe.isCurCaseSensitive);
		chkWrapSearch.setSelection(qe.isCurWrap);

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
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;

		sShell = new Shell(getParent(), getStyle());
		if (isFind)
			sShell.setText(Messages.getString("TOOLTIP.QEDIT.FIND"));
		else
			sShell.setText(Messages.getString("TOOLTIP.QEDIT.REPLACE"));

		sShell.setLayout(gridLayout);
		// if (!isFind)
		// sShell.setSize(new Point(400, 200));
		// else
		// sShell.setSize(new Point(400, 180));
		createCmpLeft();
		createCmpRight();

		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				MainRegistry.isFindDlgOpen = false;
			}
		});
	}

	/**
	 * This method initializes cmpLeft
	 * 
	 */
	private void createCmpLeft() {
		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;

		GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = 0;
		cmpLeft = new Composite(sShell, SWT.NONE);
		cmpLeft.setLayout(gridLayout);
		cmpLeft.setLayoutData(gridData);
		createCmpTexts();
		createCmpOptions();
	}

	/**
	 * This method initializes cmpRight
	 * 
	 */
	private void createCmpRight() {
		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpRight = new Composite(sShell, SWT.BORDER);
		cmpRight.setLayout(new GridLayout());
		cmpRight.setLayoutData(gridData);

		gridData = new GridData();
		gridData.widthHint = 80;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		btnFind = new Button(cmpRight, SWT.NONE);
		btnFind.setText(Messages.getString("QEDIT.FIND"));
		btnFind.setLayoutData(gridData);
		btnFind
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						QueryEditor qe = MainRegistry.getCurrentQueryEditor();
						if (qe == null)
							return;

						if (txtFind.getText().length() > 0) {
							qe.setFindOption(txtFind.getText(), chkWrapSearch
									.getSelection(), radioUp.getSelection(),
									chkCaseSensitive.getSelection(),
									chkWholeWord.getSelection());
							if (!qe.txtFind(txtFind.getText(), -1,
									chkWrapSearch.getSelection(), radioUp
											.getSelection(), chkCaseSensitive
											.getSelection(), chkWholeWord
											.getSelection()))
								CommonTool.InformationBox(sShell, Messages
										.getString("QEDIT.FIND"), Messages
										.getString("QEDIT.NOTFOUND"));
							sShell.setFocus();
						}
					}
				});

		if (!isFind) {
			btnReplace = new Button(cmpRight, SWT.NONE);
			btnReplace.setText(Messages.getString("QEDIT.REPLACEFIND"));
			btnReplace.setLayoutData(gridData);
			btnReplace
					.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(
								org.eclipse.swt.events.SelectionEvent e) {
							QueryEditor qe = MainRegistry
									.getCurrentQueryEditor();
							if (qe == null)
								return;

							qe.setFindOption(txtFind.getText(), chkWrapSearch
									.getSelection(), radioUp.getSelection(),
									chkCaseSensitive.getSelection(),
									chkWholeWord.getSelection());
							if (!qe.txtReplace(txtFind.getText(), txtReplace
									.getText(), chkWrapSearch.getSelection(),
									radioUp.getSelection(), chkCaseSensitive
											.getSelection(), chkWholeWord
											.getSelection()))
								CommonTool.InformationBox(sShell, Messages
										.getString("QEDIT.REPLACE"), Messages
										.getString("QEDIT.NOTFOUND"));
							sShell.setFocus();
						}
					});
			btnReplaceAll = new Button(cmpRight, SWT.NONE);
			btnReplaceAll.setText(Messages.getString("QEDIT.REPLACEALL"));
			btnReplaceAll.setLayoutData(gridData);
			btnReplaceAll
					.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(
								org.eclipse.swt.events.SelectionEvent e) {
							QueryEditor qe = MainRegistry
									.getCurrentQueryEditor();
							if (qe == null)
								return;

							CommonTool.InformationBox(
								sShell,
								Messages.getString("QEDIT.REPLACEALL"),
								qe.txtReplaceAll(txtFind.getText(),
									txtReplace.getText(),
									chkCaseSensitive.getSelection()
								) + Messages.getString("QEDIT.REPLACECOMPLETE")
							);
							sShell.setFocus();
						}
					});
		}

		btnClose = new Button(cmpRight, SWT.NONE);
		btnClose.setText(Messages.getString("QEDIT.CLOSE"));
		btnClose.setLayoutData(gridData);
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes cmpTexts
	 * 
	 */
	private void createCmpTexts() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;

		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpTexts = new Composite(cmpLeft, SWT.BORDER);
		cmpTexts.setLayoutData(gridData);
		cmpTexts.setLayout(gridLayout);

		GridData labelLayout = new GridData();
		labelLayout.widthHint = 90;
		labelLayout.grabExcessVerticalSpace = true;
		lblFind = new Label(cmpTexts, SWT.NONE);
		lblFind.setText(Messages.getString("QEDIT.FINDWORD"));
		lblFind.setLayoutData(labelLayout);

		GridData textLayout = new GridData();
		textLayout.grabExcessHorizontalSpace = true;
		textLayout.grabExcessVerticalSpace = true;
		textLayout.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		textLayout.widthHint = 200;
		txtFind = new Text(cmpTexts, SWT.BORDER);
		txtFind.setLayoutData(textLayout);

		if (!isFind) {
			lblReplace = new Label(cmpTexts, SWT.NONE);
			lblReplace.setText(Messages.getString("QEDIT.REPLACEWORD"));
			lblReplace.setLayoutData(labelLayout);

			txtReplace = new Text(cmpTexts, SWT.BORDER);
			txtReplace.setLayoutData(textLayout);
		}
	}

	/**
	 * This method initializes cmpOptions
	 * 
	 */
	private void createCmpOptions() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;

		cmpOptions = new Composite(cmpLeft, SWT.NONE);
		cmpOptions.setLayout(gridLayout);

		createCmpOptLeft();
		createCmpOptRight();

		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpOptions.setLayoutData(gridData);
	}

	/**
	 * This method initializes cmpOptLeft
	 * 
	 */
	private void createCmpOptLeft() {
		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpOptLeft = new Composite(cmpOptions, SWT.BORDER);
		cmpOptLeft.setLayout(new GridLayout());
		cmpOptLeft.setLayoutData(gridData);

		chkCaseSensitive = new Button(cmpOptLeft, SWT.CHECK);
		chkCaseSensitive.setText(Messages.getString("QEDIT.CASESENSITIVE"));
		chkCaseSensitive.setLayoutData(gridData);

		chkWrapSearch = new Button(cmpOptLeft, SWT.CHECK);
		chkWrapSearch.setText(Messages.getString("QEDIT.WRAPSEARCH"));
		chkWrapSearch.setLayoutData(gridData);

		chkWholeWord = new Button(cmpOptLeft, SWT.CHECK);
		chkWholeWord.setText(Messages.getString("QEDIT.WHOLEWORD"));
		chkWholeWord.setLayoutData(gridData);
	}

	/**
	 * This method initializes cmpOptRight
	 * 
	 */
	private void createCmpOptRight() {
		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpOptRight = new Composite(cmpOptions, SWT.NONE);
		cmpOptRight.setLayout(new GridLayout());
		cmpOptRight.setLayoutData(gridData);
		createGrpDirection();
	}

	/**
	 * This method initializes grpDirection
	 * 
	 */
	private void createGrpDirection() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		grpDirection = new Group(cmpOptRight, SWT.NONE);
		grpDirection.setLayout(gridLayout);
		grpDirection.setLayoutData(gridData);
		grpDirection.setText(Messages.getString("QEDIT.DIRECTION"));

		gridData.widthHint = 130;
		radioUp = new Button(grpDirection, SWT.RADIO);
		radioUp.setLayoutData(gridData);
		radioUp.setText(Messages.getString("QEDIT.UP"));

		radioDown = new Button(grpDirection, SWT.RADIO);
		radioDown.setText(Messages.getString("QEDIT.DOWN"));
		radioDown.setLayoutData(gridData);
	}

}
