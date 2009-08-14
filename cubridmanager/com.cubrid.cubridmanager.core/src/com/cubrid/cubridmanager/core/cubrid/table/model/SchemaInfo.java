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

/**
 * to store a table or view schema information
 * <li> for table, including columns, class columns, super classes, resolutions,
 * class resolutions, constraint
 * <li> for view, including columns, class columns, query specifications
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-5 created by moulinwang
 */
public class SchemaInfo implements
		Comparable<SchemaInfo>,
		Cloneable {
	private String classname = null;
	private String type = null;
	private String owner = null;
	private String virtual = null;
	private String dbname = null;
	private String is_partitiongroup = null;
	private String partitiongroupname = null;
	private List<DBAttribute> classAttributes = null;; // DBAttribute
	private List<DBAttribute> attributes = null; // DBAttribute
	private List<DBMethod> classMethods = null; // DBMethod
	private List<DBMethod> methods = null;// DBMethod
	private List<DBResolution> classResolutions = null; // DBResolution
	private List<DBResolution> resolutions = null; // DBResolution
	private List<Constraint> constraints = null;// Constraint
	private List<String> superClasses = null; // add super classes
	private List<String> subClasses = null;
	private List<String> oidList = null;
	private List<String> methodFiles = null;
	private List<String> querySpecs = null;

	/**
	 * get constraint if exists via constraint name, given a type name for
	 * validation
	 * 
	 * @param name
	 * @param type
	 * @return
	 */
	public Constraint getConstraintByName(String name, String type) {
		if (constraints == null) {
			return null;
		} else {
			for (int i = 0; i < constraints.size(); i++) {
				Constraint constraint = constraints.get(i);
				if (constraint.getType().equals(type)
						&& constraint.getName().equals(name)) {
					return constraint;
				}
			}
		}
		return null;
	}

	/**
	 * remove constraint if exists via constraint name, given a type name for
	 * validation
	 * 
	 * @param name
	 * @param type
	 */
	public void removeConstraintByName(String name, String type) {
		if (constraints == null) {
			return;
		} else {
			for (int i = 0; i < constraints.size(); i++) {
				Constraint constraint = constraints.get(i);
				if (constraint.getType().equals(type)
						&& constraint.getName().equals(name)) {
					constraints.remove(i);
					return;
				}
			}
		}
	}

	/**
	 * remove unique constraint on an attribute
	 * 
	 * @param attrName
	 * @param type
	 */
	public void removeUniqueByAttrName(String attrName) {
		if (constraints == null) {
			return;
		} else {
			for (int i = constraints.size() - 1; i >= 0; i--) {
				Constraint constraint = constraints.get(i);
				if (constraint.getType().equals(ConstraintType.UNIQUE.getText())) {
					List<String> attributes = constraint.getAttributes();
					if (attributes.size() == 1
							&& attributes.get(0).equals(attrName)) {
						constraints.remove(i);
					}
				}
			}
		}
	}

	/**
	 * 
	 * get FK constraint by name if exists
	 * 
	 * @param fkName
	 * @return
	 */
	public Constraint getFKConstraint(String fkName) {
		if (constraints == null) {
			return null;
		} else {
			for (Constraint fk : constraints) {
				if (fk.getType().equals(ConstraintType.FOREIGNKEY.getText())) {
					if (fkName.equals(fk.getName())) {
						return fk;
					}
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * get FK constraint by name if exists
	 * 
	 * @param fkName
	 * @return
	 */
	public List<Constraint> getFKConstraints() {
		List<Constraint> fkList = new ArrayList<Constraint>();
		if (constraints == null) {
			return fkList;
		} else {
			for (Constraint fk : constraints) {
				if (fk.getType().equals(ConstraintType.FOREIGNKEY.getText())) {
					fkList.add(fk);
				}
			}
		}
		return fkList;
	}

	/**
	 * remove FK constraint if exists
	 * 
	 * @param fk
	 */
	public void removeFKConstraint(Constraint fk) {
		removeFKConstraint(fk.getName());
	}

	/**
	 * remove FK constraint by name if exists
	 * 
	 * @param fkName
	 */
	public void removeFKConstraint(String fkName) {
		if (constraints == null) {
			return;
		} else {
			for (int i = 0; i < constraints.size(); i++) {
				Constraint fk = constraints.get(i);
				if (fk.getType().equals(ConstraintType.FOREIGNKEY.getText())) {
					if (fkName.equals(fk.getName())) {
						constraints.remove(i);
					}
				}
			}
		}
	}

	/**
	 * get foreign table list
	 * 
	 * @return
	 */
	public List<String> getForeignTables() {
		List<String> list = new ArrayList<String>();
		if (constraints == null) {
			return list;
		} else {
			for (Constraint fk : constraints) {
				if (fk.getType().equals(ConstraintType.FOREIGNKEY.getText())) {
					List<String> rules = fk.getRules();
					for (String rule : rules) {
						if (rule.startsWith("REFERENCES ")) {
							list.add(rule.replace("REFERENCES ", ""));
						}
					}
				}
			}
		}
		return list;
	}

	/**
	 * return whether the given column is unique, by checking whether exists a
	 * unique constraint defined on just this column
	 * 
	 * @param attr
	 * @return
	 */
	public boolean isAttributeUnique(DBAttribute attr,
			List<SchemaInfo> superList) {
		if (constraints == null) {
			return false;
		} else {
			String attrName = attr.getName();
			boolean isInherited = attr.getInherit().equals(this.getClassname()) ? false
					: true;
			for (Constraint constraint : constraints) {
				String constraintName = constraint.getName();
				if (isInherited) {
					if (!isInSuperClasses(superList, constraintName)) {
						continue;
					}
				} else {
					if (isInSuperClasses(superList, constraintName)) {
						continue;
					}
				}
				String constraintType = constraint.getType();
				if (constraintType.equals(ConstraintType.UNIQUE.getText())) {
					if (constraint.getAttributes().size() == 1
							&& constraint.getAttributes().get(0).equals(
									attrName)
							&& constraint.getRules().get(0).equals(
									attrName + " ASC")) {
						return true;
					}
				}
				if (constraintType.equals(ConstraintType.PRIMARYKEY.getText())) {
					if (constraint.getAttributes().size() == 1
							&& constraint.getAttributes().get(0).equals(
									attrName)) {
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * trigger local defined columns(not inherit from super classes) to changed
	 * its table name when schema name changed
	 * 
	 * @param newClassName
	 */
	private void fireClassNameChanged(String newClassName) {
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		list.addAll(getAttributes());
		list.addAll(getClassAttributes());
		for (DBAttribute a : list) {
			if (a.getInherit().equals(classname)) {
				a.setInherit(newClassName);
			}
		}		
	}

	/**
	 * when column name changed, make constraints containing the column to
	 * change the column name
	 * 
	 * @param oldAttrName
	 * @param newAttrName
	 */
	private void fireAttributeNameChanged(String oldAttrName,
			String newAttrName, List<SchemaInfo> superList) {
		List<Constraint> clist = this.getConstraints();
		for (Constraint c : clist) {
			if (!isInSuperClasses(superList, c.getName())) {
				List<String> attributes = c.getAttributes();
				if (attributes.contains(oldAttrName)) {
					int index = attributes.indexOf(oldAttrName);
					attributes.remove(index);
					attributes.add(index, newAttrName);
				}
			}
		}
	}

	/**
	 * return a new copy of current schema instance
	 */
	public SchemaInfo clone() {
		SchemaInfo newSchemaInfo = null;
		try {
			newSchemaInfo = (SchemaInfo) super.clone();
		} catch (CloneNotSupportedException e) {
		}
		if (newSchemaInfo == null) {
			return null;
		}
		if (classAttributes == null) {
			newSchemaInfo.classAttributes = null;
		} else {
			newSchemaInfo.classAttributes = new ArrayList<DBAttribute>();
			for (DBAttribute a : classAttributes) {
				newSchemaInfo.classAttributes.add(a.clone());
			}
		}

		if (attributes == null) {
			newSchemaInfo.attributes = null;
		} else {
			newSchemaInfo.attributes = new ArrayList<DBAttribute>();
			for (DBAttribute a : attributes) {
				newSchemaInfo.attributes.add(a.clone());
			}
		}

		if (classResolutions == null) {
			newSchemaInfo.classResolutions = null;
		} else {
			newSchemaInfo.classResolutions = new ArrayList<DBResolution>();
			for (DBResolution a : classResolutions) {
				newSchemaInfo.classResolutions.add(a.clone());
			}
		}

		if (resolutions == null) {
			newSchemaInfo.resolutions = null;
		} else {
			newSchemaInfo.resolutions = new ArrayList<DBResolution>();
			for (DBResolution a : resolutions) {
				newSchemaInfo.resolutions.add(a.clone());
			}
		}

		if (constraints == null) {
			newSchemaInfo.constraints = null;
		} else {
			newSchemaInfo.constraints = new ArrayList<Constraint>();
			for (Constraint a : constraints) {
				newSchemaInfo.constraints.add(a.clone());
			}
		}
		if (superClasses == null) {
			newSchemaInfo.superClasses = null;
		} else {
			newSchemaInfo.superClasses = new ArrayList<String>();
			for (String a : superClasses) {
				newSchemaInfo.superClasses.add(a);
			}
		}
		if (subClasses == null) {
			newSchemaInfo.subClasses = null;
		} else {
			newSchemaInfo.subClasses = new ArrayList<String>();
			for (String a : subClasses) {
				newSchemaInfo.subClasses.add(a);
			}
		}
		if (oidList == null) {
			newSchemaInfo.oidList = null;
		} else {
			newSchemaInfo.oidList = new ArrayList<String>();
			for (String a : oidList) {
				newSchemaInfo.oidList.add(a);
			}
		}
		if (querySpecs == null) {
			newSchemaInfo.querySpecs = null;
		} else {
			newSchemaInfo.querySpecs = new ArrayList<String>();
			for (String a : querySpecs) {
				newSchemaInfo.querySpecs.add(a);
			}
		}
		return newSchemaInfo;
	}

	/**
	 * return description string for debug
	 */
	public String toString() {
		StringBuffer bf = new StringBuffer();
		bf.append("DB name:" + this.dbname + "\n");
		bf.append("table name:" + this.classname + "\n");
		bf.append("\tOwner:" + this.owner + "\n");
		bf.append("\ttype:" + this.type + "\n");
		bf.append("\tvirtual:" + this.virtual + "\n");
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		list.addAll(this.getClassAttributes());
		list.addAll(this.getAttributes());
		for (DBAttribute a : list) {
			bf.append("\n" + a.toString());
		}

		List<Constraint> clist = getConstraints();
		for (Constraint c : clist) {
			bf.append("\n" + c.toString());
		}

		List<String> slist = getSuperClasses();
		bf.append("Supper Class:");
		for (String str : slist) {
			bf.append(str + "\n");
		}
		List<DBResolution> rlist = new ArrayList<DBResolution>();
		rlist.addAll(this.getClassResolutions());
		rlist.addAll(this.getResolutions());
		for (DBResolution r : rlist) {
			bf.append(r.toString() + "\n");
		}

		return bf.toString();
	}

	public SchemaInfo() {

	}

	/**
	 * add a query specification to list
	 * 
	 * @param querySpec
	 */
	public void addQuerySpec(String querySpec) {
		if (null == querySpecs) {
			querySpecs = new ArrayList<String>();
		}
		querySpecs.add(querySpec);
	}

	/**
	 * add a super class to list
	 * 
	 * @param superClass
	 */
	public void addSuperClass(String superClass) {
		if (null == superClasses) {
			superClasses = new ArrayList<String>();
		}
		superClasses.add(superClass);
	}

	/**
	 * add a method file to list
	 * 
	 * @param methodfile
	 */
	public void addMethodFile(String methodfile) {
		if (null == methodFiles) {
			methodFiles = new ArrayList<String>();
		}
		methodFiles.add(methodfile);
	}

	/**
	 * add a constraint to list
	 * 
	 * @param constraint
	 */
	public void addConstraint(Constraint constraint) {
		if (null == constraints) {
			constraints = new ArrayList<Constraint>();
		}
		constraints.add(constraint);
	}

	/**
	 * add a resolution to list
	 * 
	 * @param resolution
	 */
	public void addResolution(DBResolution resolution) {
		if (null == resolutions) {
			resolutions = new ArrayList<DBResolution>();
		}
		resolutions.add(resolution);
		resolution.setClassResolution(false);
	}

	/**
	 * add a class resolution to list
	 * 
	 * @param classResolution
	 */
	public void addClassResolution(DBResolution classResolution) {
		if (null == classResolutions) {
			classResolutions = new ArrayList<DBResolution>();
		}
		classResolutions.add(classResolution);
		classResolution.setClassResolution(true);
	}

	/**
	 * add a method to list
	 * 
	 * @param method
	 */
	public void addMethod(DBMethod method) {
		if (null == methods) {
			methods = new ArrayList<DBMethod>();
		}
		methods.add(method);
	}

	/**
	 * add a class method to list
	 * 
	 * @param classMethod
	 */
	public void addClassMethod(DBMethod classMethod) {
		if (null == classMethods) {
			classMethods = new ArrayList<DBMethod>();
		}
		classMethods.add(classMethod);
	}

	/**
	 * add an instance attribute to list
	 * 
	 * @param attribute
	 */
	public void addAttribute(DBAttribute attribute) {
		if (null == attributes) {
			attributes = new ArrayList<DBAttribute>();
		}
		attributes.add(attribute);
		attribute.setClassAttribute(false);
	}

	/**
	 * add a class attribute to list
	 * 
	 * @param classAttribute
	 */
	public void addClassAttribute(DBAttribute classAttribute) {
		if (null == classAttributes) {
			classAttributes = new ArrayList<DBAttribute>();
		}
		classAttributes.add(classAttribute);
		classAttribute.setClassAttribute(true);
	}

	/**
	 * remove attribute by name
	 * 
	 * @param attributeName
	 * @param isClassAttribute
	 * @return
	 */
	public boolean removeDBAttributeByName(String attributeName,
			boolean isClassAttribute) {
		DBAttribute attr = getDBAttributeByName(attributeName, isClassAttribute);
		if (attr != null) {
			if (isClassAttribute) {
				return getClassAttributes().remove(attr);
			} else {
				return getAttributes().remove(attr);
			}
		}
		return false;
	}

	public void addDBAttribute(DBAttribute newDBAttribute,
			boolean isClassAttribute) {
		if (isClassAttribute) {
			addClassAttribute(newDBAttribute);
		} else {
			addAttribute(newDBAttribute);
		}
	}

	/**
	 * replace a column in its origin position
	 * 
	 * @param oldDBAttribute
	 * @param newDBAttribute
	 * @param isClassAttribute
	 * @return
	 */
	public boolean replaceDBAttributeByName(DBAttribute oldDBAttribute,
			DBAttribute newDBAttribute, boolean isClassAttribute,
			List<SchemaInfo> superList) {
		String attributeName = oldDBAttribute.getName();
		if (isClassAttribute) {
			if (null != classAttributes) {
				for (int i = 0; i < classAttributes.size(); i++) {
					if (classAttributes.get(i).getName().equalsIgnoreCase(
							attributeName)) {
						classAttributes.remove(i);
						classAttributes.add(i, newDBAttribute);
						return true;
					}
				}
			}
		} else {
			if (null != attributes) {
				for (int i = 0; i < attributes.size(); i++) {
					if (attributes.get(i).getName().equalsIgnoreCase(
							attributeName)) {
						attributes.remove(i);
						attributes.add(i, newDBAttribute);
						fireAttributeNameChanged(attributeName,
								newDBAttribute.getName(), superList);
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * get DBAttribute by name, <br>
	 * if is class attribute,search in class attributes; otherwise search in
	 * instance attributes
	 * 
	 * @param attributeName
	 * @return
	 */
	public DBAttribute getDBAttributeByName(String attributeName,
			boolean isClassAttribute) {
		if (isClassAttribute) {
			if (null != classAttributes) {
				for (DBAttribute a : classAttributes) {
					if (a.getName().equalsIgnoreCase(attributeName)) {
						return a;
					}
				}
			}
		} else {
			if (null != attributes) {
				for (DBAttribute a : attributes) {
					if (a.getName().equalsIgnoreCase(attributeName)) {
						return a;
					}
				}
			}
		}
		return null;
	}

	/**
	 * get DBAttribute by name, first search in instance attributes, if not
	 * found, search in class attributes
	 * 
	 * @param methodName
	 * @return
	 */
	public DBMethod getDBMethodByName(String methodName) {
		if (null != methods) {
			for (DBMethod a : methods) {
				if (a.name.equals(methodName)) {
					return a;
				}
			}
		}
		if (null != classMethods) {
			for (DBMethod a : classMethods) {
				if (a.name.equals(methodName)) {
					return a;
				}
			}
		}
		return null;
	}

	/**
	 * get Constraint by name
	 * 
	 * @param constraintName
	 * @return
	 */
	public Constraint getConstraintByName(String constraintName) {
		if (null != constraints) {
			for (Constraint c : constraints) {
				if (c.getName().equals(constraintName)) {
					return c;
				}
			}
		}
		return null;
	}

	/**
	 * return PK constraint <br>
	 * Note: when inheritance, current schema will inherit super classes' PK
	 * constraint
	 * 
	 * @param superList
	 * @return
	 */
	public Constraint getPK(List<SchemaInfo> superList) {
		if (null != constraints) {
			for (Constraint constraint : constraints) {
				if (constraint.getType().equals(
						ConstraintType.PRIMARYKEY.getText())) {
					String constraintName = constraint.getName();
					if (!isInSuperClasses(superList, constraintName)) {
						return constraint;
					}
				}
			}
		}
		return null;
	}

	/**
	 * return inherited PK constraints from super classes
	 * 
	 * @param superList
	 * @return
	 */
	public List<Constraint> getInheritPK(List<SchemaInfo> superList) {
		List<Constraint> inheritPKList = new ArrayList<Constraint>();
		if (null != constraints) {
			for (Constraint constraint : constraints) {
				if (constraint.getType().equals(
						ConstraintType.PRIMARYKEY.getText())) {
					String constraintName = constraint.getName();
					if (isInSuperClasses(superList, constraintName)) {
						inheritPKList.add(constraint);
					}
				}
			}
		}
		return inheritPKList;
	}

	/**
	 * check whether a constraint is inherited from super classes
	 * 
	 * @param superList
	 * @param constraintName
	 * @return
	 */
	public boolean isInSuperClasses(List<SchemaInfo> superList,
			String constraintName) {
		boolean found = false;
		for (SchemaInfo sup : superList) {
			Constraint c = sup.getConstraintByName(constraintName);
			if (c == null) {
				continue;
			} else {
				return true;
			}
		}
		return found;
	}

	public boolean isSystemClass() {
		if (type.equals("system"))
			return true;
		else
			return false;
	}

	public int compareTo(SchemaInfo obj) {
		return classname.compareTo(obj.classname);
	}

	@Override
	public int hashCode() {
		final int prime = 31;
		int result = 1;
		result = prime * result
				+ ((attributes == null) ? 0 : attributes.hashCode());
		result = prime * result
				+ ((classAttributes == null) ? 0 : classAttributes.hashCode());
		result = prime * result
				+ ((classMethods == null) ? 0 : classMethods.hashCode());
		result = prime
				* result
				+ ((classResolutions == null) ? 0 : classResolutions.hashCode());
		result = prime * result
				+ ((classname == null) ? 0 : classname.hashCode());
		result = prime * result
				+ ((constraints == null) ? 0 : constraints.hashCode());
		result = prime * result + ((dbname == null) ? 0 : dbname.hashCode());
		result = prime
				* result
				+ ((is_partitiongroup == null) ? 0
						: is_partitiongroup.hashCode());
		result = prime * result + ((owner == null) ? 0 : owner.hashCode());
		result = prime * result
				+ ((resolutions == null) ? 0 : resolutions.hashCode());
		result = prime * result
				+ ((superClasses == null) ? 0 : superClasses.hashCode());
		result = prime * result + ((type == null) ? 0 : type.hashCode());
		result = prime * result + ((virtual == null) ? 0 : virtual.hashCode());
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
		final SchemaInfo other = (SchemaInfo) obj;
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
		if (classMethods == null) {
			if (other.classMethods != null)
				return false;
		} else if (!classMethods.equals(other.classMethods))
			return false;
		if (classResolutions == null) {
			if (other.classResolutions != null)
				return false;
		} else if (!classResolutions.equals(other.classResolutions))
			return false;
		if (classname == null) {
			if (other.classname != null)
				return false;
		} else if (!classname.equals(other.classname))
			return false;
		if (constraints == null) {
			if (other.constraints != null)
				return false;
		} else if (!constraints.equals(other.constraints))
			return false;
		if (dbname == null) {
			if (other.dbname != null)
				return false;
		} else if (!dbname.equals(other.dbname))
			return false;
		if (is_partitiongroup == null) {
			if (other.is_partitiongroup != null)
				return false;
		} else if (!is_partitiongroup.equals(other.is_partitiongroup))
			return false;
		if (owner == null) {
			if (other.owner != null)
				return false;
		} else if (!owner.equals(other.owner))
			return false;
		if (resolutions == null) {
			if (other.resolutions != null)
				return false;
		} else if (!resolutions.equals(other.resolutions))
			return false;
		if (superClasses == null) {
			if (other.superClasses != null)
				return false;
		} else if (!superClasses.equals(other.superClasses))
			return false;
		if (type == null) {
			if (other.type != null)
				return false;
		} else if (!type.equals(other.type))
			return false;
		if (virtual == null) {
			if (other.virtual != null)
				return false;
		} else if (!virtual.equals(other.virtual))
			return false;
		return true;
	}

	public String getClassname() {
		return classname;
	}

	public void setClassname(String classname) {
		fireClassNameChanged(classname);
		this.classname = classname;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}

	public String getOwner() {
		return owner;
	}

	public void setOwner(String owner) {
		this.owner = owner;
	}

	public String getVirtual() {
		return virtual;
	}

	public void setVirtual(String virtual) {
		this.virtual = virtual;
	}

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public String getIs_partitiongroup() {
		return is_partitiongroup;
	}

	public void setIs_partitiongroup(String is_partitiongroup) {
		this.is_partitiongroup = is_partitiongroup;
	}

	public String getPartitiongroupname() {
		return partitiongroupname;
	}

	public void setPartitiongroupname(String partitiongroupname) {
		this.partitiongroupname = partitiongroupname;
	}

	public List<String> getMethodFiles() {
		return methodFiles;
	}

	public List<DBMethod> getClassMethods() {
		return classMethods;
	}

	public List<DBMethod> getMethods() {
		return methods;
	}

	public List<DBAttribute> getClassAttributes() {
		if (null == classAttributes) {
			return new ArrayList<DBAttribute>();
		}
		return classAttributes;
	}

	public List<DBAttribute> getAttributes() {
		if (null == attributes) {
			return new ArrayList<DBAttribute>();
		}
		return attributes;
	}

	/**
	 * return local defined class attributes, not inherit from super classes
	 * 
	 * @return
	 */
	public List<DBAttribute> getLocalClassAttributes() {
		if (null == classAttributes) {
			return new ArrayList<DBAttribute>();
		}
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		for (DBAttribute classAttribute : classAttributes) {
			if (classAttribute.getInherit().equals(this.getClassname())) {
				list.add(classAttribute);
			}
		}
		return list;
	}

	/**
	 * return local defined attributes, not inherit from super classes
	 * 
	 * @return
	 */
	public List<DBAttribute> getLocalAttributes() {
		if (null == attributes) {
			return new ArrayList<DBAttribute>();
		}
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		for (DBAttribute attribute : attributes) {
			if (attribute.getInherit().equals(this.getClassname())) {
				list.add(attribute);
			}
		}
		return list;
	}

	/**
	 * return class attributes inherited from super classes
	 * 
	 * @return
	 */
	public List<DBAttribute> getInheritClassAttributes() {
		if (null == classAttributes) {
			return new ArrayList<DBAttribute>();
		}
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		for (DBAttribute classAttribute : classAttributes) {
			if (!classAttribute.getInherit().equals(this.getClassname())) {
				list.add(classAttribute);
			}
		}
		return list;
	}

	/**
	 * return attributes inherited from super classes
	 * 
	 * @return
	 */
	public List<DBAttribute> getInheritAttributes() {
		if (null == attributes) {
			return new ArrayList<DBAttribute>();
		}
		List<DBAttribute> list = new ArrayList<DBAttribute>();
		for (DBAttribute attribute : attributes) {
			if (!attribute.getInherit().equals(this.getClassname())) {
				list.add(attribute);
			}
		}
		return list;
	}

	public List<DBResolution> getClassResolutions() {
		if (null == classResolutions) {
			return new ArrayList<DBResolution>();
		}
		return classResolutions;
	}

	public List<DBResolution> getResolutions() {
		if (null != resolutions)
			return resolutions;
		else
			return new ArrayList<DBResolution>();
	}

	public List<Constraint> getConstraints() {
		if (null != constraints)
			return constraints;
		else
			return new ArrayList<Constraint>();
	}

	public List<String> getSuperClasses() {
		if (null != superClasses)
			return superClasses;
		else
			return new ArrayList<String>();
	}

	public List<String> getSubClasses() {
		return subClasses;
	}

	public List<String> getOidList() {
		return oidList;
	}

	public List<String> getQuerySpecs() {
		return querySpecs;
	}

	public void setSuperClasses(List<String> superClasses) {
		this.superClasses = superClasses;
	}

	public void setClassAttributes(List<DBAttribute> classAttributes) {
		this.classAttributes = classAttributes;
	}

	public void setAttributes(List<DBAttribute> attributes) {
		this.attributes = attributes;
	}

	public void setClassResolutions(List<DBResolution> classResolutions) {
		this.classResolutions = classResolutions;
	}

	public void setResolutions(List<DBResolution> resolutions) {
		this.resolutions = resolutions;
	}

	public void addResolution(DBResolution resolution, boolean isClassType) {
		if (isClassType) {
			addClassResolution(resolution);
		} else {
			addResolution(resolution);
		}

	}
}
