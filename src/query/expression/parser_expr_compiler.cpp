#include "parser_expr_compiler.hpp"
#include "parser.h"
#include "parse_tree.h"
#include "regu_var.hpp"
#include <vector>
#include <stack>
#include <string>
#include <sstream>
#include <cassert>

namespace predication
{
  static bool has_left_child (PT_NODE *node)
  {
    if (node->node_type != PT_EXPR)
      {
	return false;
      }

    return node->info.expr.arg1 != NULL;
  }

  static bool has_right_child (PT_NODE *node)
  {
    if (node->node_type != PT_EXPR)
      {
	return false;
      }

    return node->info.expr.arg2 != NULL;
  }

  static PT_NODE *get_left_child (PT_NODE *node)
  {
    assert (node->node_type == PT_EXPR);
    return node->info.expr.arg1;
  }
  static PT_NODE *get_right_child (PT_NODE *node)
  {
    assert (node->node_type == PT_EXPR);
    return node->info.expr.arg2;
  }

  void PredExprCompiler::compile_internal (PT_NODE *pt_node)
  {
    if (pt_node == nullptr)
      {
	return;
      }

    if (has_left_child (pt_node))
      {
	compile_internal (get_left_child (pt_node));
      }

    if (has_right_child (pt_node))
      {
	compile_internal (get_right_child (pt_node));
      }

    compile_unit (pt_node);
  }

  /*
  pt_to_pred_expr_local_with_arg  참고
  */
  InstNode
  PredExprCompiler::pt_node_to_instnode (PT_NODE *node)
  {
    InstNode inst_node;
    PreInstNode pre_inst_node;
    PT_NODE *save_node;
    DB_TYPE data_type = DB_TYPE_NULL;

    if (node)
      {
	save_node = node;

	CAST_POINTER_TO_NODE (node);
	pre_inst_node.node_type = node->node_type;

	if (node->node_type == PT_EXPR)
	  {
	    /* TODO: function화 및 전부 채우기 */
	    switch (node->info.expr.op)
	      {
	      case PT_OR:
		inst_node.code_ = InstCode::OR;
		break;

	      case PT_AND:
		inst_node.code_ = InstCode::AND;
		break;

	      case PT_NOT:
		inst_node.code_ = InstCode::NOT;
		break;

	      case PT_EQ:
	      {
		PreInstNode pre_inst_node2 = pre_inst_nodes_.top();
		DB_TYPE data_type2 = pre_inst_node2.data_type;
		pre_inst_nodes_.pop();
		PreInstNode pre_inst_node1 = pre_inst_nodes_.top();
		DB_TYPE data_type1 = pre_inst_node1.data_type;
		pre_inst_nodes_.pop();

		// make pre_inst_node
		pre_inst_node.node_type = node->node_type;
		pre_inst_node.data_type = data_type1;
		pre_inst_node.result_idx = script_->add_last_bool_index();
		pre_inst_nodes_.push (pre_inst_node);
		// make inst_node
		inst_node.code_ = InstCode::EVAL_EQ;
		inst_node.arg1_ = pre_inst_node1.result_idx;
		inst_node.arg2_ = pre_inst_node2.result_idx;
		inst_node.arg3_ = pre_inst_node.result_idx;

		break;
	      }

	      default:
		assert (false);
		break;
	      }
	    return inst_node;
	  }

	if (node->node_type == PT_NAME)
	  {
	    // make pre_inst_node
	    pre_inst_node.node_type = node->node_type;
	    pre_inst_node.data_type = pt_type_enum_to_db (node->type_enum);
	    pre_inst_node.result_idx = script_->add_last_regu_var_index();
	    pre_inst_nodes_.push (pre_inst_node);
	    // make inst_node
	    inst_node.code_ = InstCode::GET_COLUMN_VAR;
	    inst_node.arg1_ = node->info.name.location;
	    inst_node.arg2_ = pre_inst_node.result_idx;
	    return inst_node;
	  }

	if (node->node_type == PT_VALUE)   /* TODO: Change as Constant*/
	  {
	    // make pre_inst_node
	    pre_inst_node.node_type = node->node_type;
	    pre_inst_node.data_type = pt_type_enum_to_db (node->type_enum);
	    pre_inst_node.result_idx = script_->add_last_regu_var_index();
	    pre_inst_nodes_.push (pre_inst_node);
	    // make inst_node
	    inst_node.code_ = InstCode::GET_HOST_VAR;
	    inst_node.arg1_ = node->info.host_var.index;
	    inst_node.arg2_ = pre_inst_node.result_idx;
	    return inst_node;
	  }

	if (node->node_type == PT_HOST_VAR)   /* TODO: Change as Constant*/
	  {
	    // make pre_inst_node
	    pre_inst_node.node_type = node->node_type;
	    pre_inst_node.data_type = pt_type_enum_to_db (node->type_enum);
	    pre_inst_node.result_idx = script_->add_last_regu_var_index();
	    pre_inst_nodes_.push (pre_inst_node);
	    // make inst_node
	    inst_node.code_ = InstCode::GET_HOST_VAR;
	    inst_node.arg1_ = node->info.value.host_var_index;
	    inst_node.arg2_ = pre_inst_node.result_idx;
	    return inst_node;
	  }

	node = save_node;
      }

    return inst_node;
  }

  void PredExprCompiler::compile_unit (PT_NODE *node)
  {
    script_->get_inst_nodes()->push_back (pt_node_to_instnode (node));
    script_->add_last_inst_idx();
  }

  /* ============================ Dump ============================*/
  std::string CompiledPredExpr::print_inst_nodes (int idx)
  {
    std::stringstream ss;

    InstNode inst = this->inst_nodes_[idx];

    switch (inst.code_)
      {
      case InstCode::HALT:
	ss << "HALT : get result from logicvar(" << inst.arg1_ << ")";
	break;

      case InstCode::JUMP_COND:
	ss << "JUMP_COND";
	break;

      case InstCode::CAST:
	ss << "CAST : with arg1: reguvar(" << inst.arg1_ << ") TO arg2: reguvar(" << inst.arg2_ << ") TO arg3: reguvar(" <<
	   inst.arg3_ << ")";
	break;

      case InstCode::GET_COLUMN_VAR:
	ss << "GET_COLUMN_VAR : \n arg1: from nth column(" << inst.arg1_ << ") TO arg2: reguvar(" << inst.arg2_ << ")";
	break;

      case InstCode::GET_HOST_VAR:
	ss << "GET_HOST_VAR : \n arg1: from nth hostvar(" << inst.arg1_ << ") TO arg2: reguvar(" << inst.arg2_ << ")";
	break;

      case InstCode::OR:
	ss << "OR";
	break;

      case InstCode::AND:
	ss << "AND";
	break;

      case InstCode::NOT:
	ss << "NOT";
	break;

      case InstCode::EVAL_EQ:
	ss << "EVAL_EQ : \n arg1: reguvar(" << inst.arg1_ << ") = arg2: reguvar(" << inst.arg2_ << ") TO arg3: logicvar(" <<
	   inst.arg3_ << ")";
	break;

      case InstCode::EVAL_NE:
	ss << "EVAL_NE";
	break;

      case InstCode::EVAL_GT:
	ss << "EVAL_GT";
	break;

      case InstCode::EVAL_LT:
	ss << "EVAL_LT";
	break;

      case InstCode::EVAL_GE:
	ss << "EVAL_GE";
	break;

      case InstCode::EVAL_LE:
	ss << "EVAL_LE";
	break;

      case InstCode::CONDITION_TRUE_JUMP:
	ss << "CONDITION_TRUE_JUMP";
	break;

      case InstCode::FUNCTION_ARG0:
	ss << "FUNCTION_ARG0";
	break;

      case InstCode::FUNCTION_ARG1:
	ss << "FUNCTION_ARG1";
	break;

      case InstCode::FUNCTION_ARG2:
	ss << "FUNCTION_ARG2";
	break;

      case InstCode::FUNCTION_ARG3:
	ss << "FUNCTION_ARG3";
	break;

      case InstCode::FUNCTION_ARG4:
	ss << "FUNCTION_ARG4";
	break;

      case InstCode::MAX:
	ss << "MAX";
	break;

      default:
	assert (false);
	break;
      }

    return ss.str();
  }




bool
pred_expr_to_compiled_pred_expr (PT_NODE * node_list, CompiledPredExpr *compiled_pred_expr)
{
  PRED_EXPR *cnf_pred, *dnf_pred, *temp;
  PT_NODE *node, *cnf_node, *dnf_node;
  int dummy;
  int num_dnf, i;

  cnf_pred = NULL;
  for (node = node_list; node; node = node->next)
    {
      cnf_node = node;

      CAST_POINTER_TO_NODE (cnf_node);

      predication::PredExprCompiler pred_expr_compiler(cnf_node);
      compiled_pred_expr = pred_expr_compiler.compile();
    }

  return true;
}

  std::string CompiledPredExpr::print_script()
  {
    std::stringstream ss;
    ss << "=========================================================\n";
    ss << "script size : " << inst_nodes_.size() << "\n";
    ss << "regulator variable count : " << last_regu_var_index_ << "\n";
    ss << "bool variable count : " << this->last_bool_index_ << "\n";

    for (int i = 0; i < this->last_inst_idx_; ++i)
      {
	ss << i << " : ";
	ss << this->print_inst_nodes (i);
	ss << "\n";
      }

    ss << "=========================================================\n";
    return ss.str();
  }
} // namespace predication