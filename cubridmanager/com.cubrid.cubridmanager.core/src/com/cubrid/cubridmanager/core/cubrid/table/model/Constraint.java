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

package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

public class Constraint implements
		Cloneable {
	String name;
	String type;
	int keyCount;
	List<String> classAttributes = null; // String
	List<String> attributes = null; // String
	List<String> rules = null; // String

	/**
	 * if this constraint is FK type, return its foreign table; otherwise return
	 * null
	 * 
	 * @return
	 */
	public String getForeignTable() {
		if (getType().equals(ConstraintType.FOREIGNKEY.getText())) {
			List<String> rules = getRules();
			for (String rule : rules) {
				if (rule.startsWith("REFERENCES ")) {
					return rule.replace("REFERENCES ", "");
				}
			}
		}
		return null;
	}

	/**
	 * return the system default name for constraint when creating this
	 * constraint without constraint name
	 * 
	 * @param tableName
	 * @return
	 */
	public String getDefaultName(String tableName) {
		if (type.equals("PRIMARY KEY")) {
			return SystemNamingUtil.getPKName(tableName, getAttributes());
		} else if (type.equals("FOREIGN KEY")) {
			return SystemNamingUtil.getFKName(tableName, getAttributes());
		} else if (type.equals("INDEX")) {
			return SystemNamingUtil.getIndexName(tableName, getRules());
		} else if (type.equals("REVERSE INDEX")) {
			return SystemNamingUtil.getReverseIndexName(tableName, getAttributes());
		} else if (type.equals("UNIQUE")) {
			return SystemNamingUtil.getUniqueName(tableName, getRules());
		} else if (type.equals("REVERSE UNIQUE")) {
			return SystemNamingUtil.getReverseUniqueName(tableName,
					getAttributes());
		}
		return null;
	}

	/**
	 * clone current constraint instance, and return
	 */
	public Constraint clone() {
		Constraint newConstraint = null;
		try {
			newConstraint = (Constraint) super.clone();
		} catch (CloneNotSupportedException e) {
		}
		if (newConstraint == null) {
			return null;
		}
		if (classAttributes == null) {
			newConstraint.classAttributes = null;
		} else {
			newConstraint.classAttributes = new ArrayList<String>();
			for (String a : classAttributes) {
				newConstraint.classAttributes.add(a);
			}
		}
		if (attributes == null) {
			newConstraint.attributes = null;
		} else {
			newConstraint.attributes = new ArrayList<String>();
			for (String a : attributes) {
				newConstraint.attributes.add(a);
			}
		}
		if (rules == null) {
			newConstraint.rules = null;
		} else {
			newConstraint.rules = new ArrayList<String>();
			for (String a : rules) {
				newConstraint.rules.add(a);
			}
		}
		return newConstraint;
	}

	/**
	 * return description string of this for debug
	 */
	public String toString() {
		StringBuffer bf = new StringBuffer();
		bf.append("constraint name:" + this.name + "\n");
		bf.append("\ttype:" + this.type + "\n");
		List<String> list = this.getClassAttributes();
		bf.append("\tClassAttributes:\n");
		for (String str : list)
			bf.append(str + ",");

		list = this.getAttributes();
		bf.append("\tAttributes:\n");
		for (String str : list)
			bf.append(str + ",");

		list = this.getRules();
		bf.append("\tRules:\n");
		for (String str : list)
			bf.append(str + ",");
		return bf.toString();
	}

	/**
	 * Constructor
	 * 
	 * @param name
	 * @param type
	 */
	public Constraint(String name, String type) {
		this.name = name;
		this.type = type;
	}

	/**
	 * Constructor
	 * 
	 */
	public Constraint() {
	}

	/**
	 * add a rule to list
	 * 
	 * @param ruleName
	 */
	public void addRule(String ruleName) {
		if (null == rules) {
			rules = new ArrayList<String>();
		}
		rules.add(ruleName);
	}

	/**
	 * add a class attribute to list
	 * 
	 * @param classAttributeName
	 */
	public void addClassAttribute(String classAttributeName) {
		if (null == classAttributes) {
			classAttributes = new ArrayList<String>();
		}
		classAttributes.add(classAttributeName);
	}

	/**
	 * add an attribute name to list
	 * 
	 * @param attributename
	 */
	public void addAttribute(String attributename) {
		if (null == attributes) {
			attributes = new ArrayList<String>();
		}
		attributes.add(attributename);
	}

	public String getName() {
		return name;
	}

	public void setName(String name) {
		this.name = name;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}

	public List<String> getClassAttributes() {
		if (null != classAttributes) {
			return classAttributes;
		} else {
			return new ArrayList<String>(0);
		}
	}

	public List<String> getAttributes() {
		if (null != attributes) {
			return attributes;
		} else {
			return new ArrayList<String>(0);
		}
	}

	public List<String> getRules() {
		if (null != rules) {
			return rules;
		} else {
			return new ArrayList<String>(0);
		}
	}

	@Override
	public int hashCode() {
		final int prime = 31;
		int result = 1;
		result = prime * result
				+ ((attributes == null) ? 0 : attributes.hashCode());
		result = prime * result
				+ ((classAttributes == null) ? 0 : classAttributes.hashCode());
		result = prime * result + ((name == null) ? 0 : name.hashCode());
		result = prime * result + ((rules == null) ? 0 : rules.hashCode());
		result = prime * result + ((type == null) ? 0 : type.hashCode());
		return result;
	}

	@Override
	public boolean equals(Object obj) {
		if (this == obj)
			return true;
		if (obj == null)
			return false;
		if (getClass() != obj.getClass())
			return false;
		final Constraint other = (Constraint) obj;
		if (name == null) {
			if (other.name != null)
				return false;
		} else if (!name.equals(other.name))
			return false;
		if (type == null) {
			if (other.type != null)
				return false;
		} else if (!type.equals(other.type))
			return false;
		if (attributes == null) {
			if (other.attributes != null)
				return false;
		} else if (!attributes.equals(other.attributes))
			return false;
		if (classAttributes == null) {
			if (other.classAttributes != null)
				return false;
		} else if (!classAttributes.equals(other.classAttributes))
			return false;

		if (rules == null) {
			if (other.rules != null)
				return false;
		} else if (!rules.equals(other.rules))
			return false;

		return true;
	}

	public int getKeyCount() {
		return keyCount;
	}

	public void setKeyCount(int keyCount) {
		this.keyCount = keyCount;
	}

	public void setAttributes(List<String> attributes) {
		this.attributes = attributes;
	}

	public void setRules(List<String> rules) {
		this.rules = rules;
	}
}
