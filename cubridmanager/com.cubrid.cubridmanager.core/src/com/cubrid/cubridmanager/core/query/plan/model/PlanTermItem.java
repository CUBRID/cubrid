package com.cubrid.cubridmanager.core.query.plan.model;

/**
 * 
 * Plan Term sub item model class
 * 
 * PlanTermItem Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class PlanTermItem {

	private String condition = null;
	private String attribute = null;
	
	public String getCondition() {
		return condition;
	}
	
	public void setCondition(String condition) {
		this.condition = condition;
	}

	public String getAttribute() {
		return attribute;
	}

	public void setAttribute(String attribute) {
		this.attribute = attribute;
	}
	
}
