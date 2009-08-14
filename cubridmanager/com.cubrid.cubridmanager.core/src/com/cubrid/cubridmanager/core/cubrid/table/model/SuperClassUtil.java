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
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

/**
 * to provide methods when a schema adds or removes super classes
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class SuperClassUtil {

	private static final Logger logger = LogUtil.getLogger(SuperClassUtil.class);

	/**
	 * check whether a resolution exists in a list
	 * 
	 * @param resolutions
	 * @param column
	 * @param table
	 * @return
	 */
	public static boolean isInResolutions(List<DBResolution> resolutions,
			String column, String table) {
		for (DBResolution r : resolutions) {
			if (r.getName().equals(column) && r.getClassName().equals(table)) {
				return true;
			}
		}
		return false;
	}

	/**
	 * get a resolution from a list
	 * 
	 * @param resolutions
	 * @param column
	 * @param table
	 * @return
	 */
	public static DBResolution getResolution(List<DBResolution> resolutions,
			String column, String table) {
		for (DBResolution r : resolutions) {
			if (r.getName().equals(column) && r.getClassName().equals(table)) {
				return r;
			}
		}
		return null;
	}

	/**
	 * get next resolution from a list,given removed resolution is not an alias
	 * resolution
	 * 
	 * @param resolutions
	 * @param removedResolution
	 * @param conflicts
	 * @return
	 */
	public static DBResolution getNextResolution(
			List<DBResolution> resolutions, DBResolution removedResolution,
			List<String[]> conflicts) {
		String column = removedResolution.getName();
		String table = removedResolution.getClassName();
		boolean started = false;
		boolean found = false;
		DBResolution firstResolution = null;
		DBResolution nextResolution = null;
		for (String[] strs : conflicts) {
			if (strs[0].equals(column)) {
				if (!started) {
					DBResolution r = SuperClassUtil.getResolution(resolutions,
							strs[0], strs[2]);
					if (r == null) {
						started = true;
						firstResolution = new DBResolution(strs[0], strs[2],
								null);
					}
				}
				if (!found) {
					if (strs[2].equals(table)) {
						found = true;
						continue;
					}
				}
				if (started) {
					if (found) {
						DBResolution r = SuperClassUtil.getResolution(
								resolutions, strs[0], strs[2]);
						if (r == null) {
							nextResolution = new DBResolution(strs[0], strs[2],
									null);
							break;
						}
					}
				}
			}
			continue;
		}
		assert (found == true);
		if (nextResolution == null) {
			return firstResolution;
		} else {
			return nextResolution;
		}
	}

	/**
	 * get an attribute from a list by attribute name
	 * 
	 * @param list
	 * @param column
	 * @return
	 */
	public static DBAttribute getAttrInList(List<DBAttribute> list,
			String column) {
		for (DBAttribute attr : list) {
			if (attr.getName().equals(column)) {
				return attr;
			}
		}
		return null;
	}

	/**
	 * an inner class for storing an attribute and schema which the attribute is
	 * in
	 * 
	 * @author moulinwang
	 * @version 1.0 - 2009-6-4 created by moulinwang
	 */
	static class NewAttribute {
		DBAttribute attr;
		SchemaInfo schema;

		public NewAttribute(DBAttribute attr, SchemaInfo s) {
			this.attr = attr;
			this.schema = s;
		}
	}

	/**
	 * reset attributes and resolutions when resolution changes, eg:
	 * <li>when adding an alias resolution, an attribute inherited should be
	 * added to the schema
	 * <li>when adding another resolution, an attribute inherited should be
	 * changed to the schema
	 * 
	 * @param database
	 * @param oldSchemaInfo
	 * @param newSchemaInfo
	 * @param isClassType
	 */
	public static void fireResolutionChanged(DatabaseInfo database,
			SchemaInfo oldSchemaInfo, SchemaInfo newSchemaInfo,
			boolean isClassType) {
		//checking attribute
		List<DBResolution> newResolutions = null;
		List<DBAttribute> localAttributes = null;
		if (isClassType) {
			newResolutions = getCloneResolutions(newSchemaInfo.getClassResolutions());
			localAttributes = newSchemaInfo.getLocalClassAttributes();
		} else {
			newResolutions = getCloneResolutions(newSchemaInfo.getResolutions());
			localAttributes = newSchemaInfo.getLocalAttributes();
		}

		Map<String, List<SchemaInfo>> columnInheritSchemaMap = new HashMap<String, List<SchemaInfo>>();
		checkingOnSuperClassChanged(columnInheritSchemaMap, database,
				oldSchemaInfo, newSchemaInfo, newSchemaInfo.getSuperClasses(),
				localAttributes, newResolutions, isClassType);

		//reset resolution, super classes and resolution
		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		List<String> newSupers = newSchemaInfo.getSuperClasses();
		for (String sup : newSupers) {
			schemalist.add(database.getSchemaInfo(sup).clone());
		}
		if (isClassType) {
			resetAttribute(newSchemaInfo, schemalist, newResolutions,
					localAttributes, columnInheritSchemaMap, true);
			newSchemaInfo.setClassResolutions(newResolutions);
		} else {
			resetAttribute(newSchemaInfo, schemalist, newResolutions,
					localAttributes, columnInheritSchemaMap, false);
			newSchemaInfo.setResolutions(newResolutions);
		}
	}

	/**
	 * reset attributes and resolutions when super classes change, eg:
	 * <li>when adding a super class, some naming conflicts maybe occurs, so
	 * some resolution would be added, attributes to the schema should be reset
	 * and ordered
	 * 
	 * @param database
	 * @param oldSchemaInfo
	 * @param newSchemaInfo
	 * @param isClassType
	 */
	public static boolean fireSuperClassChanged(DatabaseInfo database,
			SchemaInfo oldSchemaInfo, SchemaInfo newSchemaInfo,
			List<String> newSupers) {

		//checking attribute
		List<DBResolution> newResolutions = getCloneResolutions(newSchemaInfo.getResolutions());
		List<DBAttribute> localAttributes = newSchemaInfo.getLocalAttributes();
		Map<String, List<SchemaInfo>> columnInheritSchemaMap = new HashMap<String, List<SchemaInfo>>();
		boolean success = checkingOnSuperClassChanged(columnInheritSchemaMap,
				database, oldSchemaInfo, newSchemaInfo, newSupers,
				localAttributes, newResolutions, false);
		if (!success) {
			return false;
		}
		//checking class attributes
		List<DBResolution> newClassResolutions = getCloneResolutions(newSchemaInfo.getClassResolutions());
		List<DBAttribute> localClassAttributes = newSchemaInfo.getLocalClassAttributes();
		Map<String, List<SchemaInfo>> classColumnInheritSchemaMap = new HashMap<String, List<SchemaInfo>>();
		boolean classSuccess = checkingOnSuperClassChanged(
				classColumnInheritSchemaMap, database, oldSchemaInfo,
				newSchemaInfo, newSupers, localClassAttributes,
				newClassResolutions, true);
		if (!classSuccess) {
			return false;
		}
		//reset resolution, super classes and resolution
		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		for (String sup : newSupers) {
			schemalist.add(database.getSchemaInfo(sup).clone());
		}

		if (success && classSuccess) {
			//remove inherit constraint
			List<SchemaInfo> superList = getSuperClasses(database,
					newSchemaInfo);
			List<Constraint> constraints = newSchemaInfo.getConstraints();
			for (int j = constraints.size() - 1; j >= 0; j--) {
				Constraint constraint = constraints.get(j);
				String constraintType = constraint.getType();
				String constraintName = constraint.getName();
				boolean isConstraintInheritSupported = isConstraintTypeInherit(constraintType);
				if (isConstraintInheritSupported) {
					if (newSchemaInfo.isInSuperClasses(superList,
							constraintName)) {
						constraints.remove(j);
					}
				}
			}
			resetAttribute(newSchemaInfo, schemalist, newResolutions,
					localAttributes, columnInheritSchemaMap, false);
			newSchemaInfo.setResolutions(newResolutions);

			resetAttribute(newSchemaInfo, schemalist, newClassResolutions,
					localClassAttributes, classColumnInheritSchemaMap, true);
			newSchemaInfo.setClassResolutions(newClassResolutions);

			newSchemaInfo.setSuperClasses(newSupers);
			//add inherit constraint
			superList = getSuperClasses(database, newSupers);
			for (SchemaInfo schema : superList) {
				constraints = schema.getConstraints();
				for (int i = 0; i < constraints.size(); i++) {
					Constraint constraint = constraints.get(i);
					String constraintType = constraint.getType();
					boolean isConstraintInheritSupported = isConstraintTypeInherit(constraintType);
					if (isConstraintInheritSupported) {
						newSchemaInfo.addConstraint(constraint);
					}
				}
			}
		}
		return true;

	}	

	/**
	 * check whether a type of constraint can be inherited
	 * 
	 * @param constraintType
	 */
	private static boolean isConstraintTypeInherit(String constraintType) {
		if (constraintType.equals(ConstraintType.PRIMARYKEY.getText())
				|| constraintType.equals(ConstraintType.FOREIGNKEY.getText())
				|| constraintType.equals(ConstraintType.REVERSEUNIQUE.getText())
				|| constraintType.equals(ConstraintType.UNIQUE.getText())) {
			return true;
		}
		return false;
	}

	/**
	 * clone a list of resolution and return
	 * 
	 * @param resolutions
	 * @return
	 */
	private static List<DBResolution> getCloneResolutions(
			List<DBResolution> resolutions) {
		List<DBResolution> newResolutions = new ArrayList<DBResolution>();
		for (DBResolution r : resolutions) {
			newResolutions.add(r.clone());
		}
		return newResolutions;
	}

	/**
	 * validate whether columns are data type compatible when super classes
	 * change
	 * 
	 * if not compatible, return false
	 * 
	 * @param database
	 * @param newSchemaInfo
	 * @param newSupers
	 * @param newResolutions
	 * @param classResolutions
	 */
	public static boolean checkingOnSuperClassChanged(
			Map<String, List<SchemaInfo>> columnInheritSchemaMap,
			DatabaseInfo database, SchemaInfo oldSchemaInfo,
			SchemaInfo newSchemaInfo, List<String> newSupers,
			List<DBAttribute> localAttributes,
			List<DBResolution> newResolutions, boolean isClassAttr) {

		List<String[]> conflicts = getColumnConflicts(database, newSchemaInfo,
				newSupers, isClassAttr);
		removeUnusedResolution(newResolutions, newSupers);
		removeUnusedResolution(newResolutions, conflicts, isClassAttr);
		List<DBResolution> oldResolutions = null;
		if (oldSchemaInfo == null) {
			oldResolutions = new ArrayList<DBResolution>();
		} else {
			if (isClassAttr) {
				oldResolutions = oldSchemaInfo.getClassResolutions();
			} else {
				oldResolutions = oldSchemaInfo.getResolutions();
			}
		}
		addDefaultResolution(oldResolutions, newResolutions, conflicts,
				newSchemaInfo);
		//reset attributes
		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		for (String sup : newSupers) {
			schemalist.add(database.getSchemaInfo(sup).clone());
		}
		//apply changes
		boolean success = checkingAttributeCompatible(columnInheritSchemaMap,
				database, newSchemaInfo, schemalist, localAttributes,
				newResolutions, isClassAttr);
		return success;

	}

	/**
	 * remove unneeded resolution for there is no conflict
	 * 
	 * @param resolutions
	 * @param conflicts
	 * @param isClassAttr
	 */
	private static void removeUnusedResolution(List<DBResolution> resolutions,
			List<String[]> conflicts, boolean isClassAttr) {
		//columnName, a.getType(), s.getClassname()
		for (int j = 0; j < resolutions.size();) {
			DBResolution resolution = resolutions.get(j);
			boolean found = false;
			for (String[] conflict : conflicts) {
				if (conflict[2].equals(resolution.getClassName())
						&& conflict[0].equals(resolution.getName())) {
					found = true;
				}
			}
			//if not found in conflicts, so should be removed
			if (!found) {
				resolutions.remove(j);
			} else {
				j++;
			}
		}

	}

	/**
	 * get a list of schema object by a schema's super classes
	 * 
	 * @param database
	 * @param newSchemaInfo
	 * @return
	 */
	public static List<SchemaInfo> getSuperClasses(DatabaseInfo database,
			SchemaInfo newSchemaInfo) {
		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		List<String> newSupers = newSchemaInfo.getSuperClasses();
		for (String sup : newSupers) {
			schemalist.add(database.getSchemaInfo(sup));
		}
		return schemalist;
	}

	/**
	 * get a list of schema object by a list of table names
	 * 
	 * @param database
	 * @param tableList
	 * @return
	 */
	public static List<SchemaInfo> getSuperClasses(DatabaseInfo database,
			List<String> tableList) {
		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		for (String sup : tableList) {
			schemalist.add(database.getSchemaInfo(sup));
		}
		return schemalist;
	}

	/**
	 * validate whether columns are data type compatible when inherited other
	 * super classes
	 * 
	 * @param database
	 * @param newSchemaInfo
	 * @param schemalist
	 * @param resolutions
	 * @param isClassAttr
	 * @return
	 */
	private static boolean checkingAttributeCompatible(
			Map<String, List<SchemaInfo>> columnInheritSchemaMap,
			DatabaseInfo database,
			SchemaInfo newSchemaInfo, //List<String> newSupers,
			List<SchemaInfo> schemalist, List<DBAttribute> localAttributes,
			List<DBResolution> resolutions, boolean isClassAttr) {

		//statistic 
		//add local attributes first
		addAttributes2StatisticMap(columnInheritSchemaMap, newSchemaInfo,
				localAttributes, isClassAttr);
		//add super schemas' attributes
		addAttributes2StatisticMap(columnInheritSchemaMap, schemalist,
				isClassAttr);

		boolean success = computingAttributeList(database, newSchemaInfo,
				resolutions, columnInheritSchemaMap, isClassAttr);

		return success;
	}

	/**
	 * when super classes change, attributes of a schema changes, not only the
	 * number of attributes, but also the order of attributes
	 * 
	 * @param newSchemaInfo
	 * @param schemalist
	 * @param resolutions
	 * @param localAttributes
	 * @param columnInheritSchemaMap
	 * @param isClassAttr
	 */
	private static void resetAttribute(SchemaInfo newSchemaInfo,
			List<SchemaInfo> schemalist, List<DBResolution> resolutions,
			List<DBAttribute> localAttributes,
			Map<String, List<SchemaInfo>> columnInheritSchemaMap,
			boolean isClassAttr) {
		//reorder attributes		
		List<DBAttribute> newAttrList = new ArrayList<DBAttribute>();
		//add inherit attributes at first
		for (int j = 0; j < schemalist.size(); j++) {
			SchemaInfo schema = schemalist.get(j);
			List<DBAttribute> attrList = null;
			if (isClassAttr) {
				attrList = schema.getClassAttributes();
			} else {
				attrList = schema.getAttributes();
			}

			for (int i = 0; i < attrList.size(); i++) {
				DBAttribute attr = attrList.get(i);
				String columnName = attr.getName();
				List<SchemaInfo> list = columnInheritSchemaMap.get(columnName);
				/**
				 * <li>if it is in statistic map, it should be remained;
				 * <li>if it is not in, but it has an alias name, it should be
				 * remained too;
				 */
				if (list.contains(schema)) {
					newAttrList.add(attr);
				} else {
					String tableName = schema.getClassname();
					DBResolution r = getResolution(resolutions, columnName,
							tableName);
					if (r != null && r.getAlias() != null
							&& !r.getAlias().equals("")) {
						attr.setName(r.getAlias()); //modify attr name
						newAttrList.add(attr);
					}

				}
			}

		}
		//at last, add local attributes 		
		newAttrList.addAll(localAttributes);
		if (!isClassAttr) {
			newSchemaInfo.setAttributes(newAttrList);
		} else {
			newSchemaInfo.setClassAttributes(newAttrList);
		}
	}

	/**
	 * computing which attributes will be remained
	 * 
	 * @param database
	 * @param newSchemaInfo
	 * @param resolutions
	 * @param columnInheritSchemaMap
	 */
	public static boolean computingAttributeList(DatabaseInfo database,
			SchemaInfo newSchemaInfo, List<DBResolution> resolutions,
			Map<String, List<SchemaInfo>> columnInheritSchemaMap,
			boolean isClassAttr) {
		for (Iterator<Entry<String, List<SchemaInfo>>> i = columnInheritSchemaMap.entrySet().iterator(); i.hasNext();) {
			Entry<String, List<SchemaInfo>> entry = i.next();
			String columnName = entry.getKey();
			List<SchemaInfo> schemaList = entry.getValue();

			if (schemaList.size() > 1) {
				//there should be a resolution
				List<NewAttribute> attrList = new ArrayList<NewAttribute>();
				//local attribute has the highest priority
				NewAttribute localAttr = null;
				for (SchemaInfo schema : schemaList) {
					DBAttribute attr = schema.getDBAttributeByName(columnName,
							isClassAttr);
					if (attr.getInherit().equals(newSchemaInfo.getClassname())) {
						localAttr = new NewAttribute(attr, schema);
					} else {
						attrList.add(new NewAttribute(attr, schema));
					}
				}
				/**
				 * compute:
				 * <li> if local attribute exists, check whether it is the
				 * lowest in the class hierarchy
				 * <li> otherwise, return the lowest attributes, one or more in
				 * a list
				 */
				List<NewAttribute> lowestAttrList;
				try {
					lowestAttrList = getLowestAttributes(attrList, localAttr,
							database);
				} catch (Exception e) {
					logger.error(e);
					return false;
				}
				int size = lowestAttrList.size();
				if (size == 1) {
					//there is only one
					schemaList.clear();
					schemaList.add(lowestAttrList.get(0).schema);
				} else {
					//select one
					schemaList.clear();
					boolean found = false;
					for (int j = size - 1; j >= 0; j--) {
						NewAttribute attr = lowestAttrList.get(j);
						String column = attr.attr.getName();
						String table = attr.schema.getClassname();

						DBResolution r = getResolution(resolutions, column,
								table);
						if (r != null) {
							if (r.getAlias() != null
									&& !r.getAlias().equals("")) {
								//there is an alias, but it is not the candidate resolution
							} else {
								found = true;
								schemaList.add(attr.schema);
							}
						}
					}
					if (!found) {
						schemaList.add(lowestAttrList.get(0).schema);
					}
				}
			}
		}
		return true;
	}

	/**
	 * add default resolution aligning with the list of conflicts
	 * 
	 * @param oldResolutions
	 * @param newResolutions
	 * @param conflicts
	 */
	public static void addDefaultResolution(List<DBResolution> oldResolutions,
			List<DBResolution> newResolutions, List<String[]> conflicts,
			SchemaInfo newSchemaInfo) {
		List<String> localAttrList = new ArrayList<String>();
		String table = newSchemaInfo.getClassname();
		if (table == null) {
			table = "";
		}
		for (String[] strs : conflicts) {
			String tbl = strs[2];
			String col = strs[0];
			//local attribute has the highest priority
			if(table.equals(tbl)){
				localAttrList.add(col);
				continue;
			}else{
				if(localAttrList.contains(col)){
					continue;
				}
			}
			boolean found = false;
			boolean hasAlias = false;
			for (DBResolution r : newResolutions) {
				String columnName = r.getName();
				if (col.equals(columnName)) {
					if (r.getAlias().equals("")) {
						found = true;
					} else {
						if (tbl.equals(r.getClassName())) {
							hasAlias = true;
						}
					}
				}
			}
			//if current conflict has an alias, it should not be considered.
			if (hasAlias) {
				continue;
			}
			if (!found) { //if not found in conflicts, check whether an resolution exist
				for (DBResolution r : oldResolutions) {
					String columnName = r.getName();
					if (col.equals(columnName)) {
						newResolutions.add(r.clone());
						found = true;
						break;
					}
				}
			}
			if (!found) { //give a default resolution
				newResolutions.add(new DBResolution(col, tbl, ""));
			}
		}

	}

	/**
	 * sometime local defined attribute and super classes' attribute would be
	 * conflicted, these data types must be compatible, and get the lowest
	 * attribute. the rule is :
	 * <li>0. check these data types are compatible
	 * <li>1. to choose local defined attribute if exist
	 * <li>2. to choose the most special data type, if exist more than one,
	 * choose them in a list
	 * 
	 * 
	 * @param attrList
	 * @param localAttr
	 */
	private static List<NewAttribute> getLowestAttributes(
			List<NewAttribute> attrList, NewAttribute localAttr,
			DatabaseInfo database) throws Exception {
		//check whether data types of localAttr and other inherit attrs are compatible
		List<NewAttribute> lowestAttribute = new ArrayList<NewAttribute>();
		if (localAttr != null) {
			String dataType = localAttr.attr.getType();
			for (NewAttribute attr : attrList) {
				Integer ret = DataType.isCompatibleType(database, dataType,
						attr.attr.getType());
				if (ret == null || ret < 0) {
					throw new Exception(
							"inherit attribtue's data type is not compatible with local defined attribute's data type");
				}
			}
			lowestAttribute.add(localAttr);
		} else {
			lowestAttribute.add(attrList.get(0));
			for (int i = 1; i < attrList.size(); i++) {
				String dataType = lowestAttribute.get(0).attr.getType();
				DBAttribute attr = attrList.get(i).attr;
				Integer ret = DataType.isCompatibleType(database, dataType,
						attr.getType());
				if (ret == null) {
					throw new Exception(
							"inherit attribtues' data type are not compatible with each other");
				} else if (ret == 0) {
					lowestAttribute.add(attrList.get(i));
				} else if (ret > 0) {
					//do nothing
				} else {
					lowestAttribute.clear();
					lowestAttribute.add(attrList.get(i));
				}
			}
		}
		return lowestAttribute;
	}

	/**
	 * remove unused resolution when some super class is removed.
	 * 
	 * @param resolutions
	 * @param conflicts
	 */
	private static void removeUnusedResolution(List<DBResolution> resolutions,
			List<String> newSupers) {
		for (int j = 0; j < resolutions.size();) {
			DBResolution resolution = resolutions.get(j);
			boolean found = false;
			for (String superClassName : newSupers) {
				if (superClassName.equals(resolution.getClassName())) {
					found = true;
				}
			}
			//if not found in conflicts, so should be removed
			if (!found) {
				resolutions.remove(j);
			} else {
				j++;
			}
		}
	}

	/**
	 * given a set of super classes, return the conflict attribute information,
	 * including:
	 * <li> attribute name
	 * <li> data type
	 * <li> name of the table which contains the attribute
	 * 
	 * @param database
	 * @param superClasses
	 * @return
	 */
	public static List<String[]> getColumnConflicts(DatabaseInfo database,
			SchemaInfo newSchemaInfo, List<String> superClasses,
			boolean isClassAttr) {
		List<DBAttribute> localAttributes = new ArrayList<DBAttribute>();

		if (isClassAttr) {
			localAttributes.addAll(newSchemaInfo.getLocalClassAttributes());
		} else {
			localAttributes.addAll(newSchemaInfo.getLocalAttributes());
		}

		//		List<String> superClasses = newSchema.getSuperClasses();
		Map<String, List<SchemaInfo>> columnInheritSchemaMap = new HashMap<String, List<SchemaInfo>>();
		addAttributes2StatisticMap(columnInheritSchemaMap, newSchemaInfo,
				localAttributes, isClassAttr);

		addAttributes2StatisticMap(columnInheritSchemaMap, database,
				superClasses, isClassAttr);
		List<String[]> retList = new ArrayList<String[]>();
		for (Iterator<Entry<String, List<SchemaInfo>>> i = columnInheritSchemaMap.entrySet().iterator(); i.hasNext();) {
			Entry<String, List<SchemaInfo>> entry = i.next();
			String columnName = entry.getKey();
			List<SchemaInfo> schemaList = entry.getValue();

			if (schemaList.size() > 1) {
				for (SchemaInfo s : schemaList) {
					DBAttribute a = s.getDBAttributeByName(columnName,
							isClassAttr);
					String[] strs = { columnName, a.getType(), s.getClassname() };
					retList.add(strs);
				}
			}
		}
		return retList;
	}

	/**
	 * add super classes' attribute to map for statistic
	 * 
	 * @param columnSchemaMap the map for statistic,structure: key=attribute
	 *        name, value=List\<SchemaInfo\>
	 * @param database
	 * @param superClasses
	 */
	private static void addAttributes2StatisticMap(
			Map<String, List<SchemaInfo>> columnSchemaMap,
			DatabaseInfo database, List<String> superClasses,
			boolean isClassAttr) {

		List<SchemaInfo> schemalist = new ArrayList<SchemaInfo>();
		for (int i = 0, n = superClasses.size(); i < n; i++) {
			String superClass = superClasses.get(i);
			SchemaInfo superSchema = database.getSchemaInfo(superClass);
			schemalist.add(superSchema);
		}
		addAttributes2StatisticMap(columnSchemaMap, schemalist, isClassAttr);
	}

	/**
	 * add super classes' attribute to map for statistic
	 * 
	 * @param columnSchemaMap
	 * @param superslist
	 */
	private static void addAttributes2StatisticMap(
			Map<String, List<SchemaInfo>> columnSchemaMap,
			List<SchemaInfo> superslist, boolean isClassAttr) {
		for (int i = 0; i < superslist.size(); i++) {
			SchemaInfo superSchema = superslist.get(i);
			List<DBAttribute> list = null;
			if (isClassAttr) {
				list = superSchema.getClassAttributes();
			} else {
				list = superSchema.getAttributes();
			}
			addAttributes2StatisticMap(columnSchemaMap, superSchema, list,
					isClassAttr);
		}
	}

	/**
	 * add attribute to map for statistic
	 * 
	 * @param columnSchemaMap the map for statistic,structure: key=attribute
	 *        name, value=List\<SchemaInfo\>
	 * @param superSchema
	 * @param list
	 */
	private static void addAttributes2StatisticMap(
			Map<String, List<SchemaInfo>> columnSchemaMap,
			SchemaInfo superSchema, List<DBAttribute> list, boolean isClassAttr) {
		for (DBAttribute a : list) {
			String columnName = a.getName();
			List<SchemaInfo> schemaList = columnSchemaMap.get(columnName);
			if (schemaList == null) {
				schemaList = new ArrayList<SchemaInfo>();
				schemaList.add(superSchema);
				columnSchemaMap.put(columnName, schemaList);
			} else {
				boolean found = false;
				for (SchemaInfo s : schemaList) {
					DBAttribute attrIN = s.getDBAttributeByName(columnName,
							isClassAttr);
					if (attrIN.getInherit().equals(a.getInherit())) {
						found = true;
						break;
					}
				}
				if (found) {
					//do nothing
				} else {
					schemaList.add(superSchema);
				}
			}
		}
	}
}
