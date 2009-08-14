package nbench.script.parser;

import nbench.common.*;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Stack;

public class NParser
{
  final static int SHIFT  = 0;
  final static int REDUCE = 1;
  final static int ERROR  = 2;
  final static int ACCEPT = 3;
  final static HashMap<Integer,HashMap<Integer, Integer>> act_map;
  static
  {
    act_map = new HashMap<Integer, HashMap<Integer, Integer>>();
    LinkedList<Integer> a_class;
    HashMap<Integer, Integer> a_row;
    // TokAdd, TokSub
    a_class = new LinkedList<Integer>();
    a_class.add(new Integer(NToken.TokAdd)); 
    a_class.add(new Integer(NToken.TokSub));
    for(int c : a_class)
    {
      a_row = new HashMap<Integer, Integer>();
      a_row.put(NToken.TokAdd, REDUCE);  //shift for right associativity
      a_row.put(NToken.TokSub, REDUCE);
      a_row.put(NToken.TokMul, SHIFT);
      a_row.put(NToken.TokDiv, SHIFT);
      a_row.put(NToken.TokMod, SHIFT);
      a_row.put(NToken.TokLpr, SHIFT);
      a_row.put(NToken.TokRpr, REDUCE);
      a_row.put(NToken.TokEof, REDUCE);
      act_map.put(c, a_row);
    }
    // TokMul, TokDiv, TokMod
    a_class = new LinkedList<Integer>();
    a_class.add(new Integer(NToken.TokMul));
    a_class.add(new Integer(NToken.TokDiv));
    a_class.add(new Integer(NToken.TokMod));
    for(int c : a_class)
    {
      a_row = new HashMap<Integer, Integer>();
      a_row.put(NToken.TokAdd, REDUCE);
      a_row.put(NToken.TokSub, REDUCE);
      a_row.put(NToken.TokMul, REDUCE);
      a_row.put(NToken.TokDiv, REDUCE);
      a_row.put(NToken.TokMod, REDUCE);
      a_row.put(NToken.TokLpr, SHIFT);
      a_row.put(NToken.TokRpr, REDUCE);
      a_row.put(NToken.TokEof, REDUCE);
      act_map.put(c, a_row);
    }
    // TokLpr
    a_row = new HashMap<Integer, Integer>();
    a_row.put(NToken.TokAdd, SHIFT);
    a_row.put(NToken.TokSub, SHIFT);
    a_row.put(NToken.TokMul, SHIFT);
    a_row.put(NToken.TokDiv, SHIFT);
    a_row.put(NToken.TokMod, SHIFT);
    a_row.put(NToken.TokLpr, SHIFT);
    a_row.put(NToken.TokRpr, SHIFT);
    a_row.put(NToken.TokEof, ERROR);
    act_map.put(NToken.TokLpr, a_row);
    // TokRpr
    a_row = new HashMap<Integer, Integer>();
    a_row.put(NToken.TokAdd, REDUCE);
    a_row.put(NToken.TokSub, REDUCE);
    a_row.put(NToken.TokMul, REDUCE);
    a_row.put(NToken.TokDiv, REDUCE);
    a_row.put(NToken.TokMod, REDUCE);
    a_row.put(NToken.TokLpr, REDUCE);
    a_row.put(NToken.TokRpr, REDUCE);
    a_row.put(NToken.TokEof, REDUCE);
    act_map.put(NToken.TokRpr, a_row);
    // TokEof
    a_row = new HashMap<Integer, Integer>();
    a_row.put(NToken.TokAdd, SHIFT);
    a_row.put(NToken.TokSub, SHIFT);
    a_row.put(NToken.TokMul, SHIFT);
    a_row.put(NToken.TokDiv, SHIFT);
    a_row.put(NToken.TokMod, SHIFT);
    a_row.put(NToken.TokLpr, SHIFT);
    a_row.put(NToken.TokRpr, SHIFT);
    a_row.put(NToken.TokEof, ACCEPT);
    act_map.put(NToken.TokEof, a_row);
  }
  /**
   */
  private static void 
  shift (Stack<Integer> op_stack, int tok)
  {
    op_stack.push(tok);
  }
  /**
   */
  private static void
  reduce_paren( Stack<NNode> val_stack, Stack<Integer> op_stack)
  throws Exception
  {
    Integer top_tok;
    NNode red = null;
    op_stack.pop();  // ')'
    while( (top_tok = op_stack.peek()) != null && 
	top_tok.intValue() != NToken.TokLpr)
    {
      reduce_binop(val_stack, op_stack);
    }
    op_stack.pop();  // '('
  }
  /**
   */
  private static void 
  reduce_binop (Stack<NNode> val_stack, Stack<Integer> op_stack)
  throws Exception
  {
    NBinOpNode binop_node;
    NNode left = val_stack.pop();
    NNode right = val_stack.pop();
    int top_op = op_stack.pop().intValue();
    if(left == null || right == null)
      throw new NBenchException("Error:bin_op left or right missing");
    switch(top_op)
    {
    case NToken.TokAdd: 
      binop_node = new NBinOpNode(NBinOpNode.ADD, left, right);
      break;
    case NToken.TokSub: 
      binop_node = new NBinOpNode(NBinOpNode.SUB, left, right);
      break;
    case NToken.TokMul: 
      binop_node = new NBinOpNode(NBinOpNode.MUL, left, right);
      break;
    case NToken.TokDiv: 
      binop_node = new NBinOpNode(NBinOpNode.DIV, left, right);
      break;
    case NToken.TokMod: 
      binop_node = new NBinOpNode(NBinOpNode.MOD, left, right);
      break;
    default:
      throw new NBenchException("Error: unsupported binary operation:" 
	+ top_op);
    }
    val_stack.push(binop_node);
  }
  /**
   */
  private static void 
  reduce (Stack<NNode> val_stack, Stack<Integer> op_stack)
  throws Exception
  {
    if(op_stack.peek().intValue() == NToken.TokRpr)
      reduce_paren(val_stack, op_stack);
    else
      reduce_binop(val_stack, op_stack);
  }
  /**
   */
  public static NNode parse_expr (String expr) 
  throws Exception
  {
    NTokenizer tis = new NTokenizer(expr);
    Stack<NNode> val_stack = new Stack<NNode>();
    Stack<Integer> op_stack = new Stack<Integer>();
    int tok;
    op_stack.push(NToken.TokEof);
    while((tok = tis.nextToken()) != NToken.TokError)
    {
      int top_op;
      int action;
      if(tok == NToken.TokVal)
      {
	val_stack.push(new NValueNode((Value)tis.tokenVal()));
	continue;
      }
      else if (tok == NToken.TokId)
      {
	val_stack.push(new NIdentifierNode((String)tis.tokenVal()));
	continue;
      }

      top_op = op_stack.peek().intValue();
      action = act_map.get(top_op).get(tok).intValue();
//System.out.println("-------\nval_stack:" + val_stack);
//System.out.println("op_stack:" + op_stack);
//System.out.println("top_op:" + top_op + ",tok:" + tok + ",action:" + action);
      switch(action)
      {
      case SHIFT:
	shift(op_stack, tok);
	break;
      case REDUCE:
	if(tok == NToken.TokRpr)
	  op_stack.push(tok);
	reduce(val_stack, op_stack);
	if(tok != NToken.TokEof && tok != NToken.TokRpr)
	  op_stack.push(tok);
	break;
      case ACCEPT:
      {
	NNode retnode = val_stack.pop();
	if(retnode == null || val_stack.size() != 0)
	  throw new NBenchException("Error: value stack not empty:" 
		+ val_stack.toString());
	return retnode;
      }
      case ERROR:
      default:
	throw new NBenchException("Error");
      }
    }
    if(tok == NToken.TokError)
      throw new NBenchException("Token Error");
    throw new NBenchException("Parser logic error");
  }
}

