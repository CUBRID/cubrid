/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.ui.monitoring.editor;

/**
 * 
 * A class that includes target configuration info
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-7 created by lizhiqiang
 */
public class TargetConfig {
	private String name;
	private String displayName;
	private String transName;
	private int color;
	private float magnification;
	private String topCategory;
	private String category;
	private String monitorName;
	private String chartTitle;

	/**
	 * Gets the value of name
	 * 
	 * @return the name
	 */
	public String getName() {
		return name;
	}

	/**
	 * @param name the name to set
	 */
	public void setName(String name) {
		this.name = name;
	}

	/**
	 * Gets the value of displayName
	 * 
	 * @return the displayName
	 */
	public String getDisplayName() {
		return displayName;
	}

	/**
	 * @param displayName the displayName to set
	 */
	public void setDisplayName(String displayName) {
		this.displayName = displayName;
	}

	/**
	 * Gets the value of transName
	 * 
	 * @return the transName
	 */
	public String getTransName() {
		return transName;
	}

	/**
	 * @param transName the transName to set
	 */
	public void setTransName(String transName) {
		this.transName = transName;
	}

	/**
	 * Gets the value of color
	 * 
	 * @return the color
	 */
	public int getColor() {
		return color;
	}

	/**
	 * @param color the color to set
	 */
	public void setColor(int color) {
		this.color = color;
	}

	/**
	 * Gets the value of magnification
	 * 
	 * @return the magnification
	 */
	public float getMagnification() {
		return magnification;
	}

	/**
	 * @param magnification the magnification to set
	 */
	public void setMagnification(float magnification) {
		this.magnification = magnification;
	}

	/**
	 * Gets the value of topCategory
	 * 
	 * @return the topCategory
	 */
	public String getTopCategory() {
		return topCategory;
	}

	/**
	 * @param topCategory the topCategory to set
	 */
	public void setTopCategory(String topCategory) {
		this.topCategory = topCategory;
	}

	/**
	 * Gets the value of category
	 * 
	 * @return the category
	 */
	public String getCategory() {
		return category;
	}

	/**
	 * @param category the category to set
	 */
	public void setCategory(String category) {
		this.category = category;
	}

	/**
	 * Gets the value of monitorName
	 * 
	 * @return the monitorName
	 */
	public String getMonitorName() {
		return monitorName;
	}

	/**
	 * @param monitorName the monitorName to set
	 */
	public void setMonitorName(String monitorName) {
		this.monitorName = monitorName;
	}

	/**
	 * Gets the value of chartTitle
	 * 
	 * @return the chartTitle
	 */
	public String getChartTitle() {
		return chartTitle;
	}

	/**
	 * @param chartTitle the chartTitle to set
	 */
	public void setChartTitle(String chartTitle) {
		this.chartTitle = chartTitle;
	}

}
