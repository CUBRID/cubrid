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

package com.cubrid.cubridmanager.ui.logs.dialog;

import java.util.Calendar;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;

import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;

/**
 * 
 * The dialog is used to set time.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-18 created by wuyingshi
 */
public class TimeSetDialog extends CMTitleAreaDialog implements SelectionListener,ModifyListener {

	private Composite parentComp = null;
	private Composite composite = null;
	private Spinner yearSpnFrom;
	private Spinner monthSpnFrom;
	private Spinner daySpnFrom;
	private Spinner hourSpnFrom;
	private Spinner minuteSpnFrom;
	private Spinner secondSpnFrom;

	private Spinner yearSpnTo;
	private Spinner monthSpnTo;
	private Spinner daySpnTo;
	private Spinner hourSpnTo;
	private Spinner minuteSpnTo;
	private Spinner secondSpnTo;
	private static String fromDate = "";
	private static String toDate = "";

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public TimeSetDialog(Shell parentShell) {
		super(parentShell);
	}

	/**
	 * Sent when the text is modified.
	 * 
	 * @param e
	 *            an event containing information about the modify
	 */
	public void modifyText(ModifyEvent e) {
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);

		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);

		GridData gridData1 = new GridData(GridData.CENTER);
		gridData1.heightHint = 30;
		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createDataTimeGroup(composite);
		initial();

		setTitle(Messages.title_TimeSetDialog);
		setMessage(Messages.title_TimeSetDialog);
		return parentComp;
	}

	/**
	 * 
	 * Create restored date and time information group
	 * 
	 * @param parent
	 */
	private void createDataTimeGroup(Composite parent) {
		Group dataTimeGroupFrom = new Group(parent, SWT.NONE);
		dataTimeGroupFrom.setText("FROM:");
		GridLayout layoutFrom = new GridLayout();
		layoutFrom.numColumns = 9;
		layoutFrom.horizontalSpacing = 2;
		dataTimeGroupFrom.setLayout(layoutFrom);
		dataTimeGroupFrom.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		Label dateLabelFrom = new Label(dataTimeGroupFrom, SWT.LEFT);
		dateLabelFrom.setText("Date:");
		dateLabelFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));

		yearSpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		yearSpnFrom.setMinimum(1);
		Calendar cal = Calendar.getInstance();
		yearSpnFrom.setMaximum(cal.get(Calendar.YEAR));
		yearSpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 60, -1));
		yearSpnFrom.addSelectionListener(this);
		yearSpnFrom.addModifyListener(this);

		monthSpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		monthSpnFrom.setMinimum(1);
		monthSpnFrom.setMaximum(12);
		monthSpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		monthSpnFrom.addSelectionListener(this);
		monthSpnFrom.addModifyListener(this);

		daySpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		daySpnFrom.setMaximum(31);
		daySpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		daySpnFrom.addSelectionListener(this);
		daySpnFrom.addModifyListener(this);

		Label timeLabel = new Label(dataTimeGroupFrom, SWT.LEFT);
		timeLabel.setText("Time:");
		timeLabel.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));

		hourSpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		hourSpnFrom.setMinimum(0);
		hourSpnFrom.setMaximum(23);
		hourSpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		hourSpnFrom.addSelectionListener(this);
		hourSpnFrom.addModifyListener(this);

		minuteSpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		minuteSpnFrom.setMinimum(0);
		minuteSpnFrom.setMaximum(59);
		minuteSpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		minuteSpnFrom.addSelectionListener(this);
		minuteSpnFrom.addModifyListener(this);

		secondSpnFrom = new Spinner(dataTimeGroupFrom, SWT.BORDER);
		secondSpnFrom.setMinimum(0);
		secondSpnFrom.setMaximum(59);
		secondSpnFrom.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		secondSpnFrom.addSelectionListener(this);
		secondSpnFrom.addModifyListener(this);
		Label label = new Label(dataTimeGroupFrom, SWT.NONE);
		label.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		Group dataTimeGroupTo = new Group(parent, SWT.NONE);
		dataTimeGroupTo.setText("TO:");
		GridLayout layoutTo = new GridLayout();
		layoutTo.numColumns = 9;
		layoutTo.horizontalSpacing = 2;
		dataTimeGroupTo.setLayout(layoutTo);
		dataTimeGroupTo.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		Label dateLabelTo = new Label(dataTimeGroupTo, SWT.LEFT);
		dateLabelTo.setText("Date:");
		dateLabelTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));

		yearSpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		yearSpnTo.setMinimum(1);
		yearSpnTo.setMaximum(cal.get(Calendar.YEAR));
		yearSpnTo.setLayoutData(CommonTool.createGridData(1, 1, 60, -1));
		yearSpnTo.addSelectionListener(this);
		yearSpnTo.addModifyListener(this);

		monthSpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		monthSpnTo.setMinimum(1);
		monthSpnTo.setMaximum(12);
		monthSpnTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		monthSpnTo.addSelectionListener(this);
		monthSpnTo.addModifyListener(this);

		daySpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		daySpnTo.setMaximum(31);
		daySpnTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		daySpnTo.addSelectionListener(this);
		daySpnTo.addModifyListener(this);

		Label timeLabelTo = new Label(dataTimeGroupTo, SWT.LEFT);
		timeLabelTo.setText("Time:");
		timeLabelTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));

		hourSpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		hourSpnTo.setMinimum(0);
		hourSpnTo.setMaximum(23);
		hourSpnTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		hourSpnTo.addSelectionListener(this);
		hourSpnTo.addModifyListener(this);

		minuteSpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		minuteSpnTo.setMinimum(0);
		minuteSpnTo.setMaximum(59);
		minuteSpnTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		minuteSpnTo.addSelectionListener(this);
		minuteSpnTo.addModifyListener(this);

		secondSpnTo = new Spinner(dataTimeGroupTo, SWT.BORDER);
		secondSpnTo.setMinimum(0);
		secondSpnTo.setMaximum(59);
		secondSpnTo.setLayoutData(CommonTool.createGridData(1, 1, 40, -1));
		secondSpnTo.addSelectionListener(this);
		secondSpnTo.addModifyListener(this);
		Label labelTo = new Label(dataTimeGroupTo, SWT.NONE);
		labelTo.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.title_TimeSetDialog);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.button_ok, true);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.button_close, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			setReturnCode(buttonId);
			close();
		}
		if (buttonId == IDialogConstants.CANCEL_ID) {
		}
		super.buttonPressed(buttonId);
	}

	/**
	 * Sent when node is changed.
	 * 
	 * @param e
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {

	}

	/**
	 * Sent when default selection occurs in the control.
	 * <p>
	 * For example, on some platforms default selection occurs in a List when
	 * the user double-clicks an item or types return in a Text. On some
	 * platforms, the event occurs when a mouse button or key is pressed. On
	 * others, it happens when the mouse or key is released. The exact key or
	 * mouse gesture that causes this event is platform specific.
	 * </p>
	 * 
	 * @param e
	 *            an event containing information about the default selection
	 */
	public void widgetDefaultSelected(SelectionEvent e) {

	}

	/**
	 * Sent when selection occurs in the control.
	 * <p>
	 * For example, selection occurs in a List when the user selects an item or
	 * items with the keyboard or mouse. On some platforms, the event occurs
	 * when a mouse button or key is pressed. On others, it happens when the
	 * mouse or key is released. The exact key or mouse gesture that causes this
	 * event is platform specific.
	 * </p>
	 * 
	 * @param e
	 *            an event containing information about the selection
	 */
	public void widgetSelected(SelectionEvent e) {
		validFrom();
		validTo();
	}

	/**
	 * verified time controls of From group.
	 * 
	 * @return
	 */
	private boolean validFrom() {
		boolean isValidYear = true;
		boolean isValidMonth = true;
		boolean isValidDay = true;
		boolean isValidHour = true;
		boolean inValidMinute = true;
		boolean isValidSecond = true;

		Calendar cal = Calendar.getInstance();
		int year = yearSpnFrom.getSelection();
		isValidYear = year > 0 && year <= cal.get(Calendar.YEAR);
		int month = monthSpnFrom.getSelection();
		isValidMonth = month > 0 && month <= 12;
		int day = daySpnFrom.getSelection();
		isValidDay = day > 0 && day <= 31;
		int hour = hourSpnFrom.getSelection();
		isValidHour = hour >= 0 && hour <= 23;
		int minute = minuteSpnFrom.getSelection();
		inValidMinute = minute >= 0 && minute < 60;
		int second = secondSpnFrom.getSelection();
		isValidSecond = second >= 0 && second < 60;

		if (!isValidYear) {
			setErrorMessage(Messages.errYear);
		} else if (!isValidMonth) {
			setErrorMessage(Messages.errMonth);
		} else if (!isValidDay) {
			setErrorMessage(Messages.errDay);
		} else if (!isValidHour) {
			setErrorMessage(Messages.errHour);
		} else if (!inValidMinute) {
			setErrorMessage(Messages.errMinute);
		} else if (!isValidSecond) {
			setErrorMessage(Messages.errSecond);
		}
		boolean isValid = isValidYear && isValidMonth && isValidDay && isValidHour && inValidMinute && isValidSecond;
		if (isValid) {
			setErrorMessage(null);
		}
		return isValid;
	}

	/**
	 * verified time controls of To group.
	 * 
	 * @return
	 */
	private boolean validTo() {
		boolean isValidYear = true;
		boolean isValidMonth = true;
		boolean isValidDay = true;
		boolean isValidHour = true;
		boolean inValidMinute = true;
		boolean isValidSecond = true;

		Calendar cal = Calendar.getInstance();
		int year = yearSpnTo.getSelection();
		isValidYear = year > 0 && year <= cal.get(Calendar.YEAR);
		int month = monthSpnTo.getSelection();
		isValidMonth = month > 0 && month <= 12;
		int day = daySpnTo.getSelection();
		isValidDay = day > 0 && day <= 31;
		int hour = hourSpnTo.getSelection();
		isValidHour = hour >= 0 && hour <= 23;
		int minute = minuteSpnTo.getSelection();
		inValidMinute = minute >= 0 && minute < 60;
		int second = secondSpnTo.getSelection();
		isValidSecond = second >= 0 && second < 60;

		if (!isValidYear) {
			setErrorMessage(Messages.errYear);
		} else if (!isValidMonth) {
			setErrorMessage(Messages.errMonth);
		} else if (!isValidDay) {
			setErrorMessage(Messages.errDay);
		} else if (!isValidHour) {
			setErrorMessage(Messages.errHour);
		} else if (!inValidMinute) {
			setErrorMessage(Messages.errMinute);
		} else if (!isValidSecond) {
			setErrorMessage(Messages.errSecond);
		}
		boolean isValid = isValidYear && isValidMonth && isValidDay && isValidHour && inValidMinute && isValidSecond;
		if (isValid) {
			setErrorMessage(null);
		}
		return isValid;
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {

		Calendar cal = Calendar.getInstance();
		yearSpnFrom.setSelection(cal.get(Calendar.YEAR));
		monthSpnFrom.setSelection(cal.get(Calendar.MONTH) + 1);
		daySpnFrom.setSelection(cal.get(Calendar.DATE));
		hourSpnFrom.setSelection(cal.get(Calendar.HOUR_OF_DAY));
		minuteSpnFrom.setSelection(cal.get(Calendar.MINUTE));
		secondSpnFrom.setSelection(cal.get(Calendar.SECOND));

		yearSpnTo.setSelection(cal.get(Calendar.YEAR));
		monthSpnTo.setSelection(cal.get(Calendar.MONTH) + 1);
		daySpnTo.setSelection(cal.get(Calendar.DATE));
		hourSpnTo.setSelection(cal.get(Calendar.HOUR_OF_DAY));
		minuteSpnTo.setSelection(cal.get(Calendar.MINUTE));
		secondSpnTo.setSelection(cal.get(Calendar.SECOND));

	}

	/**
	 * get the fromdate.
	 * 
	 * @return
	 */
	public static String getFromDate() {
		return fromDate;
	}

	/**
	 * set the fromdate.
	 * 
	 * @param fromDate
	 */
	public static void setFromDate(String fromDate) {
		TimeSetDialog.fromDate = fromDate;
	}

	/**
	 * get the todate.
	 * 
	 * @return
	 */
	public static String getToDate() {
		return toDate;
	}

	/**
	 * set the todate.
	 * 
	 * @param toDate
	 */
	public static void setToDate(String toDate) {
		TimeSetDialog.toDate = toDate;
	}

}
