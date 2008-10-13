package cubridmanager.query;

public class StructQueryPlan {
	public String query;
	public String plan;

	public StructQueryPlan(String query, String plan) {
		this.query = new String(query);
		this.plan = new String(plan);
	}
}
