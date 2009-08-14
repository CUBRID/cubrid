package com.cubrid.cubridmanager.core.cubrid.queryplan.model;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.query.plan.model.PlanCost;
import com.cubrid.cubridmanager.core.query.plan.model.PlanNode;
import com.cubrid.cubridmanager.core.query.plan.model.PlanRoot;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTable;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTermItem;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm.PlanTermType;

public class QueryPlanTest extends SetupEnvTestCase {
	public void testModelPlanCost() {
		PlanCost bean = new PlanCost();
		bean.setFixedTotal(10);
		assertEquals(bean.getFixedTotal(), 10.0f);
		bean.setFixedCpu(8);
		assertEquals(bean.getFixedCpu(), 8.0f);
		bean.setFixedDisk(9);
		assertEquals(bean.getFixedDisk(), 9.0f);
		bean.setVarTotal(8);
		assertEquals(bean.getVarTotal(), 8.0f);
		bean.setVarCpu(6);
		assertEquals(bean.getVarCpu(), 6.0f);
		bean.setVarDisk(7);
		assertEquals(bean.getVarDisk(), 7.0f);
		bean.setCard(4);
		assertEquals(bean.getCard(), 4);
		assertEquals(bean.toString() == null, false);

	}

	public void testModelPlanNode() {
		PlanNode bean = new PlanNode();
		bean.setDepth(5);
		assertEquals(bean.getDepth(), 5);
		bean.setMethod("method");
		assertEquals(bean.getMethod(), "method");
		bean.setPosition("position");
		assertEquals(bean.getPosition(), "position");
		bean.setCost(new PlanCost());
		assertEquals(bean.getCost().getClass(), PlanCost.class);
		bean.setTable(new PlanTable());
		assertEquals(bean.getTable().getClass(), PlanTable.class);
		bean.setIndex(new PlanTerm());
		assertEquals(bean.getIndex().getClass(), PlanTerm.class);
		bean.setEdge(new PlanTerm());
		assertEquals(bean.getEdge().getClass(), PlanTerm.class);
		bean.setSargs(new PlanTerm());
		assertEquals(bean.getSargs().getClass(), PlanTerm.class);
		bean.setFilter(new PlanTerm());
		assertEquals(bean.getFilter().getClass(), PlanTerm.class);
		bean.setSort("sort");
		assertEquals(bean.getSort(), "sort");
		bean.setOrder("order");
		assertEquals(bean.getOrder(), "order");
		assertEquals(bean.toString() == null, false);
		bean.getChildren();
		bean.newChild();
	}

	public void testModelPlanRoot() {
		PlanRoot bean = new PlanRoot();
		bean.setSql("sql");
		assertEquals(bean.getSql(), "sql");
		bean.setRaw("raw");
		assertEquals(bean.getRaw(), "raw");
		bean.setPlanNode(new PlanNode());
		assertEquals(bean.getPlanNode().getClass(), PlanNode.class);
		bean.getPlainSql();
		assertEquals(bean.toString() == null, false);
	}

	public void testModelPlanTable() {
		PlanTable bean = new PlanTable();
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.setCard(4);
		assertEquals(bean.getCard(), 4);
		bean.setPage(4);
		assertEquals(bean.getPage(), 4);
		assertEquals(bean.toString() == null, false);
		assertEquals(bean.toString() == null, false);
	}

	public void testModelPlanTerm() {
		PlanTerm bean = new PlanTerm();
		bean.setType(PlanTermType.EDGE);
		assertEquals(bean.getType(), PlanTermType.EDGE);
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.addTermItem(new PlanTermItem());
		// assertEquals(bean.getTermItem() instanceof List, true);
		 bean.getTypeString();
		 bean.getTermString();
		 bean.getTermItems();
		assertEquals(bean.toString() == null, false);
	}

	public void testModelPlanTermItem() {
		PlanTermItem bean = new PlanTermItem();
		bean.setCondition("condition");
		assertEquals(bean.getCondition(), "condition");
		bean.setAttribute("attribute");
		assertEquals(bean.getAttribute(), "attribute");
		assertEquals(bean.toString() == null, false);
	}
}
