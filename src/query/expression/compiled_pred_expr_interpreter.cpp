#include "parser_expr_compiler.hpp" // CompiledPredExpr
#include "storage_common.h"
namespace predication
{
 class Context;
  /* --------------- Instruction Functions ------       --------- */
  using InstructionFunc = void (*)(Context *ctx, InstNode &inst_node, int *idx);

  /* Instruction Table InstCode -> InstructionFunc */
  InstructionFunc InstructionTable[static_cast<int>(InstCode::MAX)]
  {
    halt_func, //HALT,
    jump_cond_func, //JUMP_COND,
    cast_func, //CAST,
    get_column_var_func, //GET_COLUMN_VAR,
    get_host_var_func, //GET_HOST_VAR,
    /* PT_NODE -> InstCode */
    or_func, //OR
    and_func, //AND
    not_func, //NOT
    eval_eq_func, //EVAL_EQ
    eval_ne_func, //EVAL_NE
    eval_gt_func, //EVAL_GT
    eval_lt_func, //EVAL_LT
    eval_ge_func, //EVAL_GE
    eval_le_func, //EVAL_LE
    condition_not_true_jump_func, //CONDITION_NOT_TRUE_JUMP
    condition_true_jump_func, //CONDITION_TRUE_JUMP
    arith_func, //ARITH_FUNC
    function_arg0_func, //FUNCTION_ARG0
    function_arg1_func, //FUNCTION_ARG1
    function_arg2_func, //FUNCTION_ARG2
    function_arg3_func, //FUNCTION_ARG3
    function_arg4_func, //FUNCTION_ARG4
  };

  void halt_func (Context *ctx, InstNode &inst_node, int *idx)
  {
  }

  void jump_cond_func (Context *ctx, InstNode &inst_node, int *idx)
  {
  }   

  void cast_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void get_column_var_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void get_host_var_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void or_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void and_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void not_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void eval_eq_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void eval_ne_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void eval_gt_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void eval_lt_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void eval_ge_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void eval_le_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void condition_not_true_jump_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void condition_true_jump_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void arith_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void function_arg0_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void function_arg1_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void function_arg2_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  void function_arg3_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }

  void function_arg4_func (Context *ctx, InstNode &inst_node, int *idx)
  {
    return;
  }
  
  /* --------------- CompiledPredExprEvaluator --------------- */
  
  class CompiledPredExprEvaluator
  {
    public:
      CompiledPredExprEvaluator (const CompiledPredExpr &expr, Context *ctx) : expr_ (expr), ctx_ (ctx) {}
      DB_LOGICAL eval (const DB_VALUE *db_values, int values_count);
      void eval_internal (const DB_VALUE *db_values, int values_count);
    private:
      const CompiledPredExpr &expr_;
      Context *ctx_;
  };

  class Context
  {
    public:
      Context (int regu_var_count, int bool_var_count, RECDES *current_row_) : regu_var_count_ (regu_var_count), bool_var_count_ (bool_var_count), current_row_ (current_row_) {}
    private:
      int regu_var_count_;
      int bool_var_count_;
      std::vector<DB_VALUE> *regu_var_;
      std::vector<DB_LOGICAL> *bool_var_;
      RECDES *current_row_;
  };

  Context::Context (int regu_var_count, int bool_var_count, RECDES *current_row_) : regu_var_count_ (regu_var_count), bool_var_count_ (bool_var_count), current_row_ (current_row_)
  {
    regu_var_ = new std::vector<DB_VALUE> (regu_var_count);
    bool_var_ = new std::vector<DB_LOGICAL> (bool_var_count);
  }

  Context::~Context ()
  {
    delete regu_var_;
    delete bool_var_;
  }

CompiledPredExprEvaluator::CompiledPredExprEvaluator (const CompiledPredExpr &expr, Context *ctx) : expr_ (expr), ctx_ (ctx)
{
}

void CompiledPredExprEvaluator::eval ()
{
  std::vector<InstNode> *inst_nodes = expr_.get_inst_nodes();
  int idx = 0;
  while (idx < inst_nodes->size())
    {
      InstNode &inst_node = (*inst_nodes)[idx];
      InstructionTable[static_cast<int>(inst_node.code_)](ctx_, inst_node, &idx);
      idx++;
    }
}

/** main function **/

DB_LOGICAL eval_data_filter (OID * oid, RECDES * recdesp, const CompiledPredExpr &compiled_pred_expr)
{
  Context ctx (compiled_pred_expr.get_last_regu_var_index(), compiled_pred_expr.get_last_bool_index(), recdesp);
  CompiledPredExprEvaluator evaluator (compiled_pred_expr, &ctx);

  
  evaluator.eval();
}
} // namespace predication