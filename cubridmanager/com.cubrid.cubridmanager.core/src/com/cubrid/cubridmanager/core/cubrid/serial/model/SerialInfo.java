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
package com.cubrid.cubridmanager.core.cubrid.serial.model;

/**
 * 
 * This class is responsible to cache serial information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class SerialInfo implements
		Cloneable {

	private String name;
	private String owner;
	private String currentValue;
	private String incrementValue;
	private String maxValue;
	private String minValue;
	private boolean isCyclic = false;
	private String startedValue;
	private String className;
	private String attName;

	/**
	 * The constructor
	 * 
	 * @param name
	 * @param owner
	 * @param currentValue
	 * @param incrementValue
	 * @param maxValue
	 * @param minValue
	 * @param isCyclic
	 * @param startedValue
	 * @param className
	 * @param attName
	 */
	public SerialInfo(String name, String owner, String currentValue,
			String incrementValue, String maxValue, String minValue,
			boolean isCyclic, String startedValue, String className,
			String attName) {
		super();
		this.name = name;
		this.owner = owner;
		this.currentValue = currentValue;
		this.incrementValue = incrementValue;
		this.maxValue = maxValue;
		this.minValue = minValue;
		this.isCyclic = isCyclic;
		this.startedValue = startedValue;
		this.className = className;
		this.attName = attName;
	}

	/**
	 * The constructor
	 */
	public SerialInfo() {
	}

	/**
	 * 
	 * Get serial name
	 * 
	 * @return
	 */
	public String getName() {
		return name;
	}

	/**
	 * 
	 * Set serial name
	 * 
	 * @param name
	 */
	public void setName(String name) {
		this.name = name;
	}

	/**
	 * 
	 * Get serial owner
	 * 
	 * @return
	 */
	public String getOwner() {
		return owner;
	}

	/**
	 * 
	 * Set serial owner
	 * 
	 * @param owner
	 */
	public void setOwner(String owner) {
		this.owner = owner;
	}

	/**
	 * 
	 * Get current value
	 * 
	 * @return
	 */
	public String getCurrentValue() {
		return currentValue;
	}

	/**
	 * 
	 * Set current value
	 * 
	 * @param currentValue
	 */
	public void setCurrentValue(String currentValue) {
		this.currentValue = currentValue;
	}

	/**
	 * 
	 * Get increment value
	 * 
	 * @return
	 */
	public String getIncrementValue() {
		return incrementValue;
	}

	/**
	 * 
	 * Set increment value
	 * 
	 * @param incrementValue
	 */
	public void setIncrementValue(String incrementValue) {
		this.incrementValue = incrementValue;
	}

	/**
	 * 
	 * Get max value
	 * 
	 * @return
	 */
	public String getMaxValue() {
		return maxValue;
	}

	/**
	 * 
	 * Set max value
	 * 
	 * @param maxValue
	 */
	public void setMaxValue(String maxValue) {
		this.maxValue = maxValue;
	}

	/**
	 * 
	 * Get min value
	 * 
	 * @return
	 */
	public String getMinValue() {
		return minValue;
	}

	/**
	 * 
	 * Set min value
	 * 
	 * @param minValue
	 */
	public void setMinValue(String minValue) {
		this.minValue = minValue;
	}

	/**
	 * 
	 * Return whether it is cyclic
	 * 
	 * @return
	 */
	public boolean isCycle() {
		return isCyclic;
	}

	/**
	 * 
	 * Set whether it is cyclic
	 * 
	 * @param isCycle
	 */
	public void setCyclic(boolean isCycle) {
		this.isCyclic = isCycle;
	}

	/**
	 * 
	 * Get started value
	 * 
	 * @return
	 */
	public String getStartedValue() {
		return startedValue;
	}

	/**
	 * 
	 * Set started value
	 * 
	 * @param startedValue
	 */
	public void setStartedValue(String startedValue) {
		this.startedValue = startedValue;
	}

	/**
	 * 
	 * Get class name that the serial belong to
	 * 
	 * @return
	 */
	public String getClassName() {
		return className;
	}

	/**
	 * 
	 * Set class name that the serial belong to
	 * 
	 * @param className
	 */
	public void setClassName(String className) {
		this.className = className;
	}

	/**
	 * 
	 * Get attr name of class that the serial belong to
	 * 
	 * @return
	 */
	public String getAttName() {
		return attName;
	}

	/**
	 * 
	 * Set attr name of class that the serial belong to
	 * 
	 * @param attName
	 */
	public void setAttName(String attName) {
		this.attName = attName;
	}

	@Override
	public boolean equals(Object obj) {
		if (this == obj) {
			return true;
		}
		if (obj == null) {
			return false;
		}
		if (!(obj instanceof SerialInfo)) {
			return false;
		}
		SerialInfo a = (SerialInfo) obj;
		boolean equal = a.name == null ? this.name == null
				: a.name.equals(this.name);
		equal = equal
				&& (a.owner == null ? this.owner == null
						: a.owner.equals(this.owner));

		equal = equal
				&& (a.currentValue == null ? this.currentValue == null
						: a.currentValue.equals(this.currentValue));
		equal = equal
				&& (a.incrementValue == null ? this.incrementValue == null
						: a.incrementValue.equals(this.incrementValue));

		equal = equal
				&& (a.maxValue == null ? this.maxValue == null
						: a.maxValue.equals(this.maxValue));
		equal = equal
				&& (a.minValue == null ? this.minValue == null
						: a.minValue.equals(this.minValue));

		equal = equal
				&& (a.startedValue == null ? this.startedValue == null
						: a.startedValue.equals(this.startedValue));
		equal = equal
				&& (a.className == null ? this.className == null
						: a.className.equals(this.className));

		equal = equal
				&& (a.attName == null ? this.attName == null
						: a.attName.equals(this.attName));
		equal = equal && (a.isCyclic == this.isCyclic);
		return equal;
	}

	@Override
	public int hashCode() {
		return name.hashCode();
	}

	public SerialInfo clone() {
		try {
			return (SerialInfo) super.clone();
		} catch (CloneNotSupportedException e) {
		}
		return null;
	}
}
