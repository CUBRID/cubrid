//#include "parser_expr_compiler.hpp"
#include "parser.h"
#include "parse_tree.h"
#include "regu_var.hpp"
#include <vector>
#include <stack>
#include <string>
#include <sstream>
#include <cassert>
#include "xasl.h"

namespace predication
{
  /* When Add Instruction 1. Add InstCode 2.  Add Instruction  2.PrintInstNode 3.  parser_tree.h opcode */
  enum InstCode
  {
    /* Basic Instructions*/
    HALT,
    JUMP_COND,
    CAST,
    GET_COLUMN_VAR,
    GET_HOST_VAR,
    /* PT_NODE -> InstCode */
    OR, // PT_OR
    AND, // PT_AND
    NOT, // PT_NOT
    EVAL_EQ,
    EVAL_NE,
    EVAL_GT,
    EVAL_LT,
    EVAL_GE,
    EVAL_LE,
    CONDITION_NOT_TRUE_JUMP,
    CONDITION_TRUE_JUMP,
    ARITH_FUNC,
    FUNCTION_ARG0,
    FUNCTION_ARG1,
    FUNCTION_ARG2,
    FUNCTION_ARG3,
    FUNCTION_ARG4,
    MAX,
  };


  class InstNode
  {
    public:
      InstNode() : code_ (InstCode::HALT), arg1_ (0), arg2_ (0), arg3_ (0), arg4_ (0), arg5_ (0), arg6_ (0) {}
      InstCode code_;
      int arg1_; // different usages for different instructions
      int arg2_;
      int arg3_;
      int arg4_;
      int arg5_;
      int arg6_;
  };

  class CompiledPredExpr
  {
    public:
      CompiledPredExpr() : inst_nodes_(), last_inst_idx_ (0), last_regu_var_index_ (0), last_bool_index_ (0) {}
      std::vector<InstNode> *get_inst_nodes()
      {
	return &inst_nodes_;
      }
      void push_inst_node (const InstNode &inst_node)
      {
	inst_nodes_.push_back (inst_node);
      }
      int get_last_inst_idx() const
      {
	return last_inst_idx_;
      }
      int get_last_regu_var_index() const
      {
	return last_regu_var_index_;
      }
      int get_last_bool_index() const
      {
	return last_bool_index_;
      }
      int add_last_bool_index()
      {
	return last_bool_index_++;
      }
      int add_last_regu_var_index()
      {
	return last_regu_var_index_++;
      }
      int add_last_inst_idx()
      {
	return last_inst_idx_++;
      }
      std::string print_script();
      std::string print_inst_nodes (int idx);
    private:
      std::vector<InstNode> inst_nodes_;
      int last_inst_idx_;
      int last_regu_var_index_;
      int last_bool_index_;
  };

  class PreInstNode
  {
    public:
      PreInstNode() : node_type (PT_NODE_TYPE::PT_NODE_NONE), data_type (DB_TYPE::DB_TYPE_NULL), result_idx (0) {}
      PT_NODE_TYPE node_type;
      DB_TYPE data_type;
      int result_idx;
  };

  class PredExprCompiler
  {
    public:
      PredExprCompiler (PT_NODE *pt_node) : pt_node_ (pt_node),
	script_ (new CompiledPredExpr()) {}
      CompiledPredExpr *compile()
      {
	compile_internal (pt_node_);
	//add halt instruction
	script_->get_inst_nodes()->push_back (InstNode());
	return script_;
      }
    private:
      CompiledPredExpr *script_;
      PT_NODE *pt_node_;
      std::stack<PreInstNode> pre_inst_nodes_;
      InstNode pt_node_to_instnode (PT_NODE *node);
      void compile_internal (PT_NODE *pt_node);
      void compile_unit (PT_NODE *node);
  };

} // namespace predication