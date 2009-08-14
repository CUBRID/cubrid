/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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
package com.cubrid.cubridmanager.ui.cubrid.jobauto.control;

import java.util.Arrays;
import java.util.Observable;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.events.VerifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;

import com.cubrid.cubridmanager.ui.cubrid.jobauto.Messages;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

/**
 * A group that depict some widget in some class with composite such as
 * EditBackupPlanDialog,EditQueryPlanDialog and so on
 * 
 * @author lizhiqiang 2009-3-24
 */
public class PeriodGroup extends
		Observable {

	private String msgPeriodGroup;
	private String msgPeriodTypeLbl;
	private String msgPeriodDetailLbl;
	private String msgPeriodHourLbl;
	private String msgPeriodMinuteLbl;
	private String tipPeriodDetailCombo;

	private CMTitleAreaDialog dialog;
	private Label typelabel;
	private Combo typeCombo;
	private Label detailLabel;
	private Combo detailCombo;
	private Combo hourCombo;
	private Combo minuteCombo;
	private String[] itemsOfTypeCombo;
	private String[] itemsOfDetailsCombo;
	private String[] itemsOfDetailsComboForMon;
	private String[] itemsOfDetailsComboForWeek;
	private String[] itemsOfHourCombo;
	private String[] itemsOfMinuteCombo;
	private String typeValue;
	private String detailValue;
	private String hourValue;
	private String minuteValue;
	private String hourToolTip;
	private String minuteToolTip;
	private boolean isAllow[];

	/**
	 * Constructor
	 * 
	 * @param dialog
	 */
	public PeriodGroup(CMTitleAreaDialog dialog) {

		this.dialog = dialog;
		msgPeriodGroup = Messages.msgPeriodGroup;
		msgPeriodTypeLbl = Messages.msgPeriodTypeLbl;
		msgPeriodDetailLbl = Messages.msgPeriodDetailLbl;
		msgPeriodHourLbl = Messages.msgPeriodHourLbl;
		msgPeriodMinuteLbl = Messages.msgPeriodMinuteLbl;
		tipPeriodDetailCombo = Messages.tipPeriodDetailCombo;
		hourToolTip = Messages.hourToolTip;
		minuteToolTip = Messages.minuteToolTip;
		itemsOfTypeCombo = new String[] { Messages.monthlyPeriodType,
				Messages.weeklyPeriodType, Messages.dailyPeriodType,
				Messages.specialdayPeriodType };
		itemsOfDetailsComboForMon = new String[31];
		for (int i = 0; i < 31; i++) {
			itemsOfDetailsComboForMon[i] = Integer.toString(i + 1);
		}
		// Initials the value of typeValue and detailValue
		itemsOfDetailsComboForWeek = new String[] { Messages.sundayOfWeek,
				Messages.mondayOfWeek, Messages.tuesdayOfWeek,
				Messages.wednesdayOfWeek, Messages.thursdayOfWeek,
				Messages.fridayOfWeek, Messages.saturdayOfWeek };
		typeValue = itemsOfTypeCombo[0];
		// Sets itemsOfDetailsComboForMon as itemsOfDetailsCombo
		itemsOfDetailsCombo = itemsOfDetailsComboForMon;
		detailValue = itemsOfDetailsCombo[0];

		itemsOfHourCombo = new String[24];
		for (int i = 0; i < 24; i++) {
			itemsOfHourCombo[i] = Integer.toString(i);
		}
		itemsOfMinuteCombo = new String[60];
		for (int i = 0; i < 60; i++) {
			itemsOfMinuteCombo[i] = Integer.toString(i);
		}
		hourValue = itemsOfHourCombo[12];
		minuteValue = itemsOfMinuteCombo[30];

		isAllow = new boolean[4];
		for (int i = 0; i < isAllow.length; i++) {
			isAllow[i] = true;
		}
	}

	/**
	 * Create a period group
	 * 
	 * @param composite
	 */
	public void createPeriodGroup(Composite composite) {

		final Group periodGroup = new Group(composite, SWT.RESIZE);
		periodGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		GridLayout groupLayout = new GridLayout(4, false);
		periodGroup.setLayout(groupLayout);
		periodGroup.setText(msgPeriodGroup);

		typelabel = new Label(periodGroup, SWT.RESIZE);
		final GridData gdTypeLabel = new GridData(SWT.LEFT, SWT.CENTER, false,
				false);
		gdTypeLabel.widthHint = 80;
		typelabel.setLayoutData(gdTypeLabel);
		typelabel.setText(msgPeriodTypeLbl);

		typeCombo = new Combo(periodGroup, SWT.READ_ONLY);
		final GridData gdTypeCombo = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gdTypeCombo.widthHint = 135;
		typeCombo.setLayoutData(gdTypeCombo);
		typeCombo.setItems(itemsOfTypeCombo);
		typeCombo.setText(typeValue);
		typeCombo.addModifyListener(new TypeComboModifyListener());

		detailLabel = new Label(periodGroup, SWT.RESIZE);
		final GridData gdDetailLabel = new GridData(SWT.LEFT, SWT.CENTER,
				false, false);
		gdDetailLabel.widthHint = 80;
		detailLabel.setLayoutData(gdDetailLabel);
		detailLabel.setText(msgPeriodDetailLbl);

		detailCombo = new Combo(periodGroup, SWT.RESIZE);
		final GridData gdDetailCombo = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gdDetailCombo.widthHint = 135;
		detailCombo.setLayoutData(gdDetailCombo);

		// initials detailCobo
		if (typeValue.equalsIgnoreCase(itemsOfTypeCombo[0])) {
			itemsOfDetailsCombo = itemsOfDetailsComboForMon;
		} else if (typeValue.equalsIgnoreCase(itemsOfTypeCombo[1])) {
			itemsOfDetailsCombo = itemsOfDetailsComboForWeek;
		} else if (typeValue.equalsIgnoreCase(itemsOfTypeCombo[2])) {
			detailLabel.setVisible(false);
			detailCombo.setVisible(false);
		} else if (typeValue.equalsIgnoreCase(itemsOfTypeCombo[3])) {
			itemsOfDetailsCombo = new String[] { tipPeriodDetailCombo };
			detailCombo.setText(tipPeriodDetailCombo);
			detailCombo.setToolTipText(tipPeriodDetailCombo);
		}

		detailCombo.setItems(itemsOfDetailsCombo);
		detailCombo.setText(detailValue);
		detailCombo.addModifyListener(new DetailComboModifyListener());
		final Label backupHourLabel = new Label(periodGroup, SWT.RESIZE);
		final GridData gdHoureLabel = new GridData(SWT.LEFT, SWT.CENTER, false,
				false);
		gdHoureLabel.widthHint = 80;
		backupHourLabel.setLayoutData(gdHoureLabel);
		backupHourLabel.setText(msgPeriodHourLbl);

		hourCombo = new Combo(periodGroup, SWT.BORDER);
		hourCombo.setItems(itemsOfHourCombo);
		final GridData gdHourCombo = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gdHourCombo.widthHint = 135;
		hourCombo.setLayoutData(gdHourCombo);
		hourCombo.setText(hourValue);
		hourCombo.setToolTipText(hourToolTip);
		hourCombo.addVerifyListener(new ComboVerifyListener());
		hourCombo.addModifyListener(new HourModifyListener());

		final Label backupminuteLabel = new Label(periodGroup, SWT.RESIZE);
		backupminuteLabel.setText(msgPeriodMinuteLbl);

		minuteCombo = new Combo(periodGroup, SWT.BORDER);
		minuteCombo.setItems(itemsOfMinuteCombo);
		final GridData gdMinunteCombo = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		gdMinunteCombo.widthHint = 135;
		minuteCombo.setLayoutData(gdMinunteCombo);
		minuteCombo.setText(minuteValue);
		minuteCombo.setToolTipText(minuteToolTip);
		minuteCombo.addVerifyListener(new ComboVerifyListener());
		minuteCombo.addModifyListener(new MinuteModifyListener());

	}

	/*
	 * A class that response the change of typeCombo
	 */
	private class TypeComboModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			detailsForType(typeCombo);
		}

	}

	/*
	 * A class that response the change of detailCombo
	 */
	public class DetailComboModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String text = detailCombo.getText().trim();
			if (!validateDetailCombo(detailCombo)) {
				isAllow[0] = false;
			} else if (text.equals(tipPeriodDetailCombo)) {
				isAllow[3] = false;
			} else {
				isAllow[0] = true;
				isAllow[3] = true;
				
			}
			notifyDialog();
		}

	}

	/*
	 * A class that response the change of hourSpinner
	 */
	private class HourModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String sHour = hourCombo.getText();
			if (sHour.trim().length() == 0) {	
				isAllow[1] = false;
			} else {
				int hour = Integer.valueOf(sHour);
				if (hour > 23 || hour < 0) {		
					isAllow[1] = false;
				} else {
					isAllow[1] = true;
				}
			}
			notifyDialog();
		}

	}

	/*
	 * A class that response the change of minuteSpinner
	 */
	public class MinuteModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String sMinute = minuteCombo.getText();
			if (sMinute.trim().length() == 0) {
				isAllow[2] = false;
			} else {
				int minute = Integer.valueOf(sMinute);
				if (minute > 59 || minute < 0) {
					isAllow[2] = false;
				} else {
					isAllow[2] = true;
				}
			}
			notifyDialog();
		}

	}

	/*
	 * 
	 * Sets the value of details when changing the item of typeCombo
	 * 
	 * @param combo
	 */
	private void detailsForType(Combo combo) {
		int selectionIndex = combo.getSelectionIndex();
		detailLabel.setVisible(true);
		detailCombo.setVisible(true);
		detailCombo.setToolTipText("");
		switch (selectionIndex) {
		case 0:
			itemsOfDetailsCombo = itemsOfDetailsComboForMon;
			detailCombo.setItems(itemsOfDetailsCombo);
			detailCombo.setText(itemsOfDetailsCombo[0]);
			break;
		case 1:
			itemsOfDetailsCombo = itemsOfDetailsComboForWeek;
			detailCombo.setItems(itemsOfDetailsCombo);
			detailCombo.setText(itemsOfDetailsCombo[0]);
			break;
		case 2:
			detailLabel.setVisible(false);
			detailCombo.setVisible(false);
			isAllow[0] = true;
			isAllow[3] = true;
			notifyDialog();
			detailCombo.setText("nothing");
			break;
		case 3:
			itemsOfDetailsCombo = new String[] { tipPeriodDetailCombo };
			detailCombo.setToolTipText(tipPeriodDetailCombo);
			detailCombo.setItems(itemsOfDetailsCombo);
			detailCombo.setText(itemsOfDetailsCombo[0]);
			break;
		default:
		}
	
	}

	/*
	 * 
	 * Validates the value of detail combo
	 * 
	 * @param Combo
	 */
	private boolean validateDetailCombo(Combo combo) {
		boolean returnvalue = true;
		String text = combo.getText().trim();
		switch (typeCombo.getSelectionIndex()) {
		case 2:
			returnvalue = true;
			break;
		case 3:
			if (!text.matches(verifyTime())) {
				returnvalue = false;
			}
			break;
		default:
			boolean exist = Arrays.asList(itemsOfDetailsCombo).contains(text);
			if (!exist) {
				returnvalue = false;
			}

		}
		return returnvalue;
	}

	/**
	 * A class that response the change of Combo
	 * 
	 * @author lizhiqiang 2009-4-22
	 */
	private static class ComboVerifyListener implements
			VerifyListener {

		public void verifyText(VerifyEvent e) {
			String text = e.text;
			if (text.equals("")) {
				return;
			}
			if (!text.matches("^\\d+$")) {
				e.doit = false;
			} else {
				e.doit = true;
			}
		}

	}

	public void setMsgPeriodGroup(String msgPeriodGroup) {
		this.msgPeriodGroup = msgPeriodGroup;
	}

	public void setMsgPeriodTypeLbl(String msgPeriodTypeLbl) {
		this.msgPeriodTypeLbl = msgPeriodTypeLbl;
	}

	public void setMsgPeriodDetailLbl(String msgPeriodDetailLbl) {
		this.msgPeriodDetailLbl = msgPeriodDetailLbl;
	}

	public void setMsgPeriodHourLbl(String msgPeriodHourLbl) {
		this.msgPeriodHourLbl = msgPeriodHourLbl;
	}

	public void setMsgPeriodMinuteLbl(String msgPeriodMinuteLbl) {
		this.msgPeriodMinuteLbl = msgPeriodMinuteLbl;
	}


	public void setTipPeriodDetailCombo(String tipPeriodDetailCombo) {
		this.tipPeriodDetailCombo = tipPeriodDetailCombo;
	}

	/**
	 * Gets time value by hourSpinner and minuteSpinner
	 * 
	 */
	public String getTime() {
		StringBuffer time = new StringBuffer();
		time.append(getHour());
		time.append(getMinute());
		return time.toString();
	}

	/**
	 * Gets hour value by hourSpinner
	 * 
	 */
	public String getHour() {
		String hour = hourCombo.getText().trim();
		if (hour.length() < 2) {
			hour = "0" + hour;
		}
		return hour;
	}

	/**
	 * Gets minute value by MinuteSpinner
	 * 
	 */
	public String getMinute() {
		String minute = minuteCombo.getText().trim();
		if (minute.length() < 2) {
			minute = "0" + minute;
		}
		return minute;
	}

	/**
	 * @param type the typeValue to set
	 */
	public void setTypeValue(String type) {

		if (type.equalsIgnoreCase("Monthly")) {
			this.typeValue = Messages.monthlyPeriodType;
		} else if (type.equalsIgnoreCase("Weekly")) {
			this.typeValue = Messages.weeklyPeriodType;
		} else if (type.equalsIgnoreCase("Daily")) {
			this.typeValue = Messages.dailyPeriodType;
		} else if (type.equalsIgnoreCase("Special")) {
			this.typeValue = Messages.specialdayPeriodType;
		} else {
			this.typeValue = type;
		}

	}

	/**
	 * @param detailValue the detailValue to set
	 */
	public void setDetailValue(String detail) {
		if (detail.equalsIgnoreCase("sunday")) {
			this.detailValue = Messages.sundayOfWeek;
		} else if (detail.equalsIgnoreCase("Monday")) {
			this.detailValue = Messages.mondayOfWeek;
		} else if (detail.equalsIgnoreCase("Tuesday")) {
			this.detailValue = Messages.tuesdayOfWeek;
		} else if (detail.equalsIgnoreCase("Wednesday")) {
			this.detailValue = Messages.wednesdayOfWeek;
		} else if (detail.equalsIgnoreCase("Thursday")) {
			this.detailValue = Messages.thursdayOfWeek;
		} else if (detail.equalsIgnoreCase("Friday")) {
			this.detailValue = Messages.fridayOfWeek;
		} else if (detail.equalsIgnoreCase("Saturday")) {
			this.detailValue = Messages.saturdayOfWeek;
		} else {
			this.detailValue = detail;
		}
	}

	/**
	 * @param hourValue the hourValue to set
	 */
	public void setHourValue(int hourValue) {
		this.hourValue = Integer.toString(hourValue);
	}

	/**
	 * Gest the tip of tipPeriodDetailCombo
	 * 
	 * @return the tipPeriodDetailCombo
	 */
	public String getTipPeriodDetailCombo() {
		return tipPeriodDetailCombo;
	}

	/**
	 * @param minuteValue the minuteValue to set
	 */
	public void setMinuteValue(int minuteValue) {
		this.minuteValue = Integer.toString(minuteValue);
	}

	/**
	 * Gets the text of typeCombo
	 * 
	 * @return the typeCombo
	 */
	public String getTextOfTypeCombo() {
		String returnType = "";
		String type = typeCombo.getText().trim();
		if (type.equalsIgnoreCase(Messages.monthlyPeriodType)) {
			returnType = "Monthly";
		} else if (type.equalsIgnoreCase(Messages.weeklyPeriodType)) {
			returnType = "Weekly";
		} else if (type.equalsIgnoreCase(Messages.dailyPeriodType)) {
			returnType = "Daily";
		} else if (type.equalsIgnoreCase(Messages.specialdayPeriodType)) {
			returnType = "Special";
		} else {
			returnType = type;
		}
		return returnType;
	}

	/**
	 * Gets the Text of DetailCombo
	 * 
	 * @return the detailCombo
	 */
	public String getTextOfDetailCombo() {
		String returnDetail = "";
		String detail = detailCombo.getText().trim();
		if (detail.equalsIgnoreCase(Messages.sundayOfWeek)) {
			returnDetail = "Sunday";
		} else if (detail.equalsIgnoreCase(Messages.mondayOfWeek)) {
			returnDetail = "Monday";
		} else if (detail.equalsIgnoreCase(Messages.tuesdayOfWeek)) {
			returnDetail = "Tuesday";
		} else if (detail.equalsIgnoreCase(Messages.wednesdayOfWeek)) {
			returnDetail = "Wednesday";
		} else if (detail.equalsIgnoreCase(Messages.thursdayOfWeek)) {
			returnDetail = "Thursday";
		} else if (detail.equalsIgnoreCase(Messages.fridayOfWeek)) {
			returnDetail = "Friday";
		} else if (detail.equalsIgnoreCase(Messages.saturdayOfWeek)) {
			returnDetail = "Saturday";

		} else {
			returnDetail = detail;
		}
		return returnDetail;
	}

	/**
	 * If some control has changed, notify the relevant Observer(Dialog)
	 * 
	 * @param string
	 */
	public void notifyDialog() {
		setChanged();
		boolean allow = true;
		for (int k = 0; k < isAllow.length; k++) {
			allow = allow && isAllow[k];
		}
		notifyObservers(allow);
	}

	/*
	 * Gets the regular expressions of "yyyy-mm-dd"
	 * 
	 * @return
	 */
	private String verifyTime() {
		String string = "^((([2-9]\\d{3})-(0[13578]|1[02])-(0[1-9]|[12]\\d|3[01]))|(([2-9]\\d{3})-(0[469]|11])-(0[1-9]|[12]\\d|30))|(([2-9]\\d{3})-0?2-(0[1-9]|1\\d|2[0-8]))|((([2-9]\\d)(0[48]|[2468][048]|[13579][26])|((16|[2468][048]|[3579][26])00))-02-29))$";
		return string;
	}

	/**
	 * 
	 * Get is allow
	 * 
	 * @return
	 */
	public boolean[] getAllow() {
		return this.isAllow;
	}
	
	/**
	 * Enable the "OK" button,called by caller
	 * 
	 */
	public void enableOk() {
		if (!isAllow[0]) {
			dialog.setErrorMessage(Messages.errDetailTextMsg);
		} else if (!isAllow[1]) {
			dialog.setErrorMessage(Messages.hourToolTip);
		} else if (!isAllow[2]) {
			dialog.setErrorMessage(Messages.minuteToolTip);
		}else{
			dialog.setErrorMessage(null);
		}
	}
}
