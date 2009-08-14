package com.cubrid.cubridmanager.core.query.plan;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.query.plan.model.PlanCost;
import com.cubrid.cubridmanager.core.query.plan.model.PlanNode;
import com.cubrid.cubridmanager.core.query.plan.model.PlanRoot;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm;
import com.cubrid.cubridmanager.core.query.plan.PlanParser;

public class PlanParserTest extends TestCase {

	protected void setUp() throws Exception {
		super.setUp();
	}

	protected void tearDown() throws Exception {
		super.tearDown();
	}
	
	protected String loadPlanExmaple(String filepath)
	{
		FileReader fr = null;
		BufferedReader br = null;
		try
		{
			StringBuilder sb = new StringBuilder();
			fr = new FileReader(new File("src/com/cubrid/cubridmanager/core/query/plan/"+filepath));
			br = new BufferedReader(fr);
			for (;;)
			{
				String line = br.readLine();
				if (line == null)
					break;
				sb.append(line).append("\n");
			}
			return sb.toString();
		}
		catch (Exception ex)
		{
			return null;
		}
		finally
		{
			try { br.close(); } catch (Exception ignored) {}
			try { fr.close(); } catch (Exception ignored) {}
		}
	}

	/**
	 * simple plan
	 * 
	 * @throws Exception
	 */
	public void testExam01() throws Exception {
		String planString = loadPlanExmaple("plan01.txt");
		
		PlanParser parser = new PlanParser();
		boolean bool = parser.doParse(planString);
		assertTrue(bool);
		
		PlanRoot planRoot = parser.getPlanTree(0);
		assertNotNull(planRoot);
		
		PlanNode planNode = planRoot.getPlanNode();
		
		assertEquals(planNode.getMethod(), "idx-join (inner join)");
		assertEquals(planNode.getDepth(), 1);
		
		PlanCost planCost = planNode.getCost();
		assertNotNull(planCost);
		assertEquals(planCost.getCard(), 40);
		assertEquals(String.valueOf(planCost.getFixedCpu()), "0.0");
		assertEquals(String.valueOf(planCost.getFixedDisk()), "2.0");
		assertEquals(String.valueOf(planCost.getFixedTotal()), "2.0");
		assertEquals(String.valueOf(planCost.getVarCpu()), "100.3");
		assertEquals(String.valueOf(planCost.getVarDisk()), "275.0");
		assertEquals(String.valueOf(planCost.getVarTotal()), "375.0");
		
		assertNull(planNode.getTable());
		assertNull(planNode.getIndex());
		assertNull(planNode.getEdge());
		assertNull(planNode.getFilter());
		assertNull(planNode.getSort());
		assertNull(planNode.getOrder());
		
		PlanTerm sargs = planNode.getSargs();
		assertNotNull(sargs);
		
		assertNotNull(planNode.getChildren());
		assertEquals(planNode.getChildren().size(), 2);
	}
	
	/**
	 * complicated plan
	 * 
	 * @throws Exception
	 */
	public void testExam02() throws Exception {
		String planString = loadPlanExmaple("plan02.txt");
		
		PlanParser parser = new PlanParser();
		boolean bool = parser.doParse(planString);
		assertTrue(bool);
		
		int subPlanCount = parser.countPlanTree();
		assertEquals(10, subPlanCount);
		
		for (int i = 0; i < subPlanCount; i++) {
			PlanRoot planRoot = parser.getPlanTree(i);
			assertNotNull(planRoot);
		}
	}
	
}
