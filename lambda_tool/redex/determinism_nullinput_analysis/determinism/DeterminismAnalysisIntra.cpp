/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "DeterminismAnalysisIntra.h"

#include <iomanip>
#include <unordered_map>

#include <boost/optional.hpp>

#include "BaseIRAnalyzer.h"
#include "ControlFlow.h"
#include "DexMethodHandle.h"
#include "FiniteAbstractDomain.h"
#include "IRCode.h"
#include "IRInstruction.h"
#include "IROpcode.h"
#include "PatriciaTreeMapAbstractEnvironment.h"
#include "ReducedProductAbstractDomain.h"
#include "Resolver.h"
#include "Show.h"
#include "Trace.h"
// #include "DeterminismDomain.h"
using namespace sparta;

std::ostream& operator<<(std::ostream& output, const DeterminismType& det) {
  switch (det) {
  case DT_BOTTOM: {
    output << "BOTTOM";
    break;
  }
  case IS_DET: {
    output << "DET";
    break;
  }
  case NOT_DET: {
    output << "NOT DET";
    break;
  }
  case DT_TOP: {
    output << "TOP";
    break;
  }
  }
  return output;
}

namespace determinism {
// const DeterminismDomain dt_top(DeterminismType::DT_TOP);
// const DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);
// const DeterminismDomain is_det(DeterminismType::IS_DET);
// const DeterminismDomain not_det(DeterminismType::NOT_DET);
DetLattice det_lattice({DT_BOTTOM, IS_DET, NOT_DET, DT_TOP},
                       {{DT_BOTTOM, IS_DET},
                        {DT_BOTTOM, NOT_DET},
                        {IS_DET, DT_TOP},
                        {NOT_DET, DT_TOP}});
                        
ExpLattice exp_lattice({EX_BOTTOM, UNVISIT, FROM_EX, NO_EX, EX_TOP},
                       {{EX_BOTTOM, UNVISIT},
                        {UNVISIT, FROM_EX},
                        {UNVISIT, NO_EX},
                        {NO_EX, EX_TOP},
                        {FROM_EX, EX_TOP}});
std::ostream& operator<<(std::ostream& output,
                         const DeterminismDomain& detDomain) {
  switch (detDomain.element()) {
  case DT_BOTTOM: {
    output << "BOTTOM";
    break;
  }
  case IS_DET: {
    output << "DET";
    break;
  }
  case NOT_DET: {
    output << "NOT DET";
    break;
  }
  case DT_TOP: {
    output << "TOP";
    break;
  }
  }
  return output;
}
std::ostream& operator<<(std::ostream& output,
                         const ExpDomain& expDomain) {
  switch (expDomain.element()) {
  case EX_BOTTOM: {
    output << "BOTTOM";
    break;
  }
  case UNVISIT: {
    output << "UNVISIT";
    break;
  }
  case FROM_EX: {
    output << "FROM_EX";
    break;
  }
  case NO_EX: {
    output << "NO EX";
    break;
  }
  case EX_TOP: {
    output << "TOP";
    break;
  }
  }
  return output;
}
// std::ostream& operator<<(std::ostream& output,
//                          const CallingContext& cc) {
//   if (cc.is_top()) {
//     output << "calling context is TOP\n";
//   } else if (cc.is_bottom()) {
//     output << "calling context is bottom\n";
//   } else {
//     output << "calling context is neither top nor bottom\n";
//     // output << "[#" << cc.size() << "]";

//     // auto domain = cc.get(1);
//     // output << domain;
//     // output << "[#" << cc.size() << "]";
//     // output << cc.bindings() << std::endl;
//   }

// }

namespace impl {

using namespace ir_analyzer;


// class AbstractObjectEnvironment models the program state under analysis
//                                           BasicAbstractObjectEnvironment,
//                                           ReturnValueDomain,
//                                           CallingContextMap,
//                                           DetFieldPartition>
class Analyzer final : public BaseIRAnalyzer<AbstractObjectEnvironment> {
 public:
  explicit Analyzer(const DexMethod* dex_method,
                    const cfg::ControlFlowGraph& cfg,
                    SummaryQueryFn* summary_query_fn,
                    std::unordered_set<std::string>* reset_det_func,
                    DetFieldPartition* field_partition, IntraAnalyzerParameters ap)
      : BaseIRAnalyzer(cfg),
        m_dex_method(dex_method),
        m_cfg(cfg),
        m_summary_query_fn(summary_query_fn),
        m_reset_det_func(reset_det_func),
        m_field_partition(field_partition), m_ap(ap) {}

  void run(CallingContext* context) {
    // We need to compute the initial environment by assigning the parameter
    // registers their correct abstract domain derived from the method's
    // signature.
    // The IOPCODE_LOAD_PARAM_* instructions are pseudo-operations
    // that are used to specify the formal parameters of the method. They must
    // be interpreted separately.
    //
    // Note that we do not try to infer them as STRINGs.
    // Since we don't have the the actual value of the string other than their
    // type being String. Also for CLASSes, the exact Java type they refer to is
    // not available here.
    auto track_exception = m_ap.track_exception;
    std::cout << "track exception is" << track_exception << std::endl;
    DeterminismDomain is_det(DeterminismType::IS_DET);
    auto init_state = AbstractObjectEnvironment::top();
    init_state.set_exp_block_value(ExpBlockDomain(ExceptType::UNVISIT));
    init_state.debugExpBlockDomain();
    m_return_value.set_to_bottom();
    // Get function signature
    const auto& signature =
        m_dex_method->get_proto()->get_args()->get_type_list();
    auto sig_it = signature.begin();

    std::cout << "signature size is" << signature.size() << std::endl;
    // debug_context(context);
    if (sig_it == signature.end()) {
      // return;
    } else {
      param_index_t param_position = 0;
      // set init_state from the caller_context ()
      IRInstruction* d_ins = nullptr;
      reg_t d_reg = 0;
      for (const auto& mie :
           InstructionIterable(m_cfg.get_param_instructions())) {
        IRInstruction* insn = mie.insn;
        printf("process load param %s\n", SHOW(insn));
        switch (insn->opcode()) {
        case IOPCODE_LOAD_PARAM_OBJECT: {
          if (param_position == 0 && !is_static(m_dex_method)) {
            // If the method is not static, the first parameter corresponds to
            // `this`. we always set it to det. We will have m_field_partition
            // to track the case wherein field is not det.
            init_state.set_abstract_obj(insn->dest(), is_det);
            param_position++;
            break;
          } else {
            // This is a regular parameter of the method.
            DexType* type = *sig_it;
            // printf("method not static, parameter is %s\n", type->c_str());
            always_assert(sig_it++ != signature.end());
            printf("parameter position %d\n", param_position);
            if (context->is_bottom()) {
              printf(
                  "encounter a default bottom context, but we set loaded "
                  "parameter to be det\n");
              init_state.set_abstract_obj(insn->dest(), is_det);
              // const auto array_object =
              // init_state.get_abstract_obj(insn->dest());
              // std::cout << "Debug arrary_object" << array_object <<
              // std::endl;
              d_ins = insn;
              param_position++;
              break;
            }
            if (context && !context->get(param_position).is_bottom() &&
                !context->get(param_position).is_top()) {
              // if (context && !context->get(param_position).is_top()) {
              // Parameter domain is provided with the calling context.
              printf("context is neither top nor bottom\n");
              DeterminismDomain IS_bottom(DeterminismType::DT_BOTTOM);
              DeterminismDomain dt_top(DeterminismType::DT_TOP);
              DeterminismDomain is_det(DeterminismType::IS_DET);
              DeterminismDomain not_det(DeterminismType::NOT_DET);
              init_state.set_abstract_obj(insn->dest(),
                                          context->get(param_position));
              param_position++;
              break;
            }
            always_assert(context);
            always_assert(!context->get(param_position).is_bottom());
            always_assert(!context->get(param_position).is_top());
            // if (context->get(param_position).is_bottom()) {
            //   printf("parameter %d is bottom\n", param_position);
            // } else if (context->get(param_position).is_top()) {
            //   printf("parameter %d is top\n", param_position);
            // } else {
            //   printf("TODO: missing branch in processing method
            //   parameter\n");
            // }
            param_position++;
            break;
          }
        }
        case IOPCODE_LOAD_PARAM:
        case IOPCODE_LOAD_PARAM_WIDE: {
          DexType* type = *sig_it;
          always_assert(sig_it++ != signature.end());
          if (context->is_bottom()) {
            printf(
                "encounter a default bottom context, but we set loaded "
                "parameter to be det\n");
            if (insn->has_dest()) {
              init_state.set_abstract_obj(insn->dest(), is_det);
              if (insn->dest_is_wide()) {
                init_state.set_abstract_obj(insn->dest() + 1, is_det);
              }
            }
            // init_state.set_abstract_obj(insn->dest(), is_det);
            // const auto array_object =
            // init_state.get_abstract_obj(insn->dest());
            // std::cout << "Debug arrary_object" << array_object <<
            // std::endl;
            param_position++;
            break;
          }
          if (context && !context->get(param_position).is_bottom() &&
              !context->get(param_position).is_top()) {
            if (insn->has_dest()) {
              init_state.set_abstract_obj(insn->dest(),
                                          context->get(param_position));
              if (insn->dest_is_wide()) {
                init_state.set_abstract_obj(insn->dest() + 1,
                                            context->get(param_position));
              }
            }
          }
          param_position++;
          break;
        }
        default:
          not_reached();
        }
      }
    }// Now, init_state have mapping between each register to its abstractdomain
    printf("******done processing parameters\n");
    // std::cout << "Debug arrary_object" << d_ins->dest()
    //           << init_state.get_abstract_obj(d_ins->dest()) << std::endl;
    printf("******begin fixpoint iterator on cfg run\n");
    // run is a method inherit from the BaseIRAnalyzer
    MonotonicFixpointIterator::run(init_state);
    printf("******done fixpoint iterator run\n");
    printf("******collect return state starts\n");
    if (m_ap.track_exception) {
      for (auto* block : m_cfg.return_blocks()) {
        auto env = get_exit_state_at(block);
        std::cout << "return value domain: " << env.get_return_value() << "exp value domain: " << env.get_exp_block_value() << std::endl;
      }
    }

    auto env = MonotonicFixpointIterator::get_exit_state_at(m_cfg.exit_block());
    m_return_value = env.get_return_value();
    std::cout << "exit state return value domain: " << env.get_return_value() << "exp value domain: " << env.get_exp_block_value() << std::endl;

    printf("******collect return state finishes\n");

    // auto env = MonotonicFixpointIterator::get_exit_state_at(m_cfg.exit_block());
    // std::cout << env.get_return_value() << std::endl;
    // if (method::is_init(m_dex_method)) {
    //   std::cout << "find constructor, now set filed values\n";
    //   auto env =
    //       MonotonicFixpointIterator::get_exit_state_at(m_cfg.exit_block());
    //   DexClass* cls = type_class(m_dex_method->get_class());
    //   set_fields_in_partition(cls,
    //                           env.get_field_environment(),
    //                           FieldType::STATIC,
    //                           m_field_partition);
    // }

    populate_environments(m_cfg);
  }
  void add_except_block(cfg::GraphInterface::NodeId bb) const {
    std::cout << "add new block that has an exception handling ancestor\n";
    m_exp_partition->insert(bb);
    std::cout<<"after add this exception catch block, now size of exp block partition is" << m_exp_partition->size()<<std::endl;
  }
  void analyze_node(const cfg::GraphInterface::NodeId& node,
                    AbstractObjectEnvironment* current_state) const override {
    std::cout<< "analyzing node in self-definied mode" <<std::endl;
    // if this node is an exception handling block, we add it to the
    // ExpBlockPartition that tracks exception information
    std::cout << "--- propagate exception information start ---\n";
    if (node->starts_with_move_exception()) {
      // For a basic block that is the original source of the exception, it
      // always sets the current state to FROM_EX
      std::cout << "analyze a block that handles exception\n";
      current_state->set_exp_block_value(ExpBlockDomain(ExceptType::FROM_EX));
      current_state->debugExpBlockDomain();
    } else {
      std::cout << "analyze a block that does not handle exception\n";
      // For a basic block that is not the original source of the exception, it
      // either preserves (do nothing) the current state or initialize the current state to
      // NO_EX
      if (current_state->get_exp_block_value().equals(ExpBlockDomain(ExceptType::UNVISIT))) {
        current_state->set_exp_block_value(ExpBlockDomain(ExceptType::NO_EX));
      }
      current_state->debugExpBlockDomain();
    }
    // cfg::GraphInterface::NodeId non_const_node = node;
    // if (node->starts_with_move_exception()) {
    //   add_except_block(node);
    // } else {
    //   auto pred_vec = cfg::GraphInterface::predecessors(m_cfg, node);
    //   bool tainted_pred = false;
    //   for (auto itr: pred_vec) {
    //     cfg::GraphInterface::NodeId const& const_pred = itr->src();
    //     std::cout<<"iterate predecessors for exception handling, first instruction is\t";
    //     SHOW(*const_pred->get_first_insn());
    //     printf("\n");
    //     if (m_exp_partition->count(const_pred) != 0) {
    //       printf("pred is in ExpBlockPartition\n");
    //       tainted_pred = true;
    //       break;
    //     }
    //   }
    //   if (tainted_pred) {
    //     add_except_block(node);
    //   }
    // }
    std::cout << "--- propagate exception information end ---\n";
    for (auto& mie : InstructionIterable(node)) {
      analyze_instruction(mie.insn, current_state);
    }
  }
  void analyze_instruction(
      const IRInstruction* insn,
      AbstractObjectEnvironment* current_state) const override {

    ReturnValueDomain callee_return; // we need this value analysis after the
                                     // invocation instruction
    callee_return.set_to_bottom();
    DeterminismDomain dt_top(DeterminismType::DT_TOP);
    DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);
    DeterminismDomain is_det(DeterminismType::IS_DET);
    DeterminismDomain not_det(DeterminismType::NOT_DET);
    printf("process instructions %s\n", SHOW(insn));
    if (opcode::is_an_invoke(insn->opcode())) {
      // general preparation for invoke operations (OPRANGE(an_invoke,
      // OPCODE_INVOKE_VIRTUAL, OPCODE_INVOKE_INTERFACE)).
      printf("process invoke %s, set calling context\n", SHOW(insn));
      CallingContext cc;
      auto srcs = insn->srcs();
      for (param_index_t i = 0; i < srcs.size(); i++) {
        if (i == 0) {
          // the first argument is "this" instead of first
          // function argument
          continue;
        }
        reg_t src = insn->src(i);
        auto aobj = current_state->get_abstract_obj(src);
        cc.set(i, aobj);
        std::cout << "parameter index " << i <<" set from reg" << src << aobj << std::endl;
      }
      if (!cc.is_bottom()) {
        // it stores the calling context. Used for decide state after the invoke
        // instruction
        std::cout << "calling context is not bottom\n";
        current_state->set_calling_context(insn, cc);
      } else {
        std::cout << "calling context is bottom\n";
      }

      if (m_summary_query_fn) {
        callee_return = (*m_summary_query_fn)(insn);
        std::cout << "m_summary_query_fn discovered, reset callee_return to"
                  << callee_return << std::endl;
      }
    }

    switch (insn->opcode()) {
      printf("process non-invoke %s\n", SHOW(insn));

    case IOPCODE_LOAD_PARAM:
    case IOPCODE_LOAD_PARAM_OBJECT:
    case IOPCODE_LOAD_PARAM_WIDE: {
      // IOPCODE_LOAD_PARAM_* instructions have been processed before the
      // analysis.
      const auto parameter = current_state->get_abstract_obj(insn->dest());
      std::cout << "Parameter's fact value: " << parameter << std::endl;
      break;
    }
    case OPCODE_MOVE:
    case OPCODE_MOVE_OBJECT: {
      // not_reached();
      // (move-object v0 v2)
      // v0 is the dest, v2 is the src
      always_assert(insn->srcs_size() == 1);
      const auto dest = current_state->get_abstract_obj(insn->dest());

      const auto aobj = current_state->get_abstract_obj(insn->src(0));
      std::cout << "set from" << dest << "to" << aobj;
      current_state->set_abstract_obj(insn->dest(), aobj);
      break;

      // const auto obj = aobj.get_object();
    }
    case OPCODE_NEW_ARRAY: {
      always_assert(insn->srcs_size() == 1);
      const auto aobj = current_state->get_abstract_obj(insn->src(0));
      // The value of the new_array is determinied by its size register
      current_state->set_abstract_obj(RESULT_REGISTER, aobj);
      break;
    }

    case IOPCODE_MOVE_RESULT_PSEUDO_OBJECT: {
      // const auto aobj = current_state->get_abstract_obj(RESULT_REGISTER);
      // current_state->set_abstract_obj(insn->dest(), aobj);
      // break;
    }
    case IOPCODE_MOVE_RESULT_PSEUDO_WIDE:
    case IOPCODE_MOVE_RESULT_PSEUDO: {
      if (tmp_field_name != nullptr) {
        std::cout << "add reg to fied mapping for IOPCODE_MOVE_RESULT\n";
        // DexField *previous_field_name = &std::move(*tmp_field_name);
        m_reg_field_mapping->insert({insn->dest(), tmp_field_name});
        tmp_field_name = nullptr;
      }

    }
    case OPCODE_MOVE_RESULT: {
      // Move the object result of the most recent invoke-kind into the
      // indicated register. This must be done as the instruction immediately
      // after an invoke-kind or filled-new-array whose (object) result is not
      // to be ignored; anywhere else is invalid.
    }
    case OPCODE_MOVE_RESULT_WIDE: {
    }
    case OPCODE_MOVE_RESULT_OBJECT: {
      // Move the object result of the most recent invoke-kind into the
      // indicated register. This must be done as the instruction immediately
      // after an invoke-kind or filled-new-array whose (object) result is not
      // to be ignored; anywhere else is invalid.
      const auto aobj = current_state->get_abstract_obj(RESULT_REGISTER);
      current_state->set_abstract_obj(insn->dest(), aobj);
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, aobj);
      }
      //   const auto obj = aobj.get_object();
      //   if (obj && obj->obj_kind == AbstractObjectKind::CLASS) {
      //     current_state->set_class_source(
      //         insn->dest(),
      //         current_state->get_class_source(RESULT_REGISTER));
      //   }
      std::cout << "result reg" << aobj << "dest reg" << insn->dest()
                << std::endl;
      break;
    }
    case OPCODE_CMP_LONG:
    case OPCODE_CMPG_DOUBLE:
    case OPCODE_CMPG_FLOAT:
    case OPCODE_CMPL_DOUBLE:
    case OPCODE_CMPL_FLOAT: {
      always_assert_log(insn->has_dest(), "CMP always has dest");
      operation_with_three_operands_last_is_variable(insn, current_state);
      break;
    }

    case OPCODE_CONST_WIDE:
    case OPCODE_CONST: {
      DeterminismDomain is_det(DeterminismType::IS_DET);
      current_state->set_abstract_obj(insn->dest(), is_det);
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, is_det);
      }
      break;
    }
    case OPCODE_CONST_STRING: {
      DeterminismDomain is_det(DeterminismType::IS_DET);
      current_state->set_abstract_obj(RESULT_REGISTER, is_det);
      break;
    }
    case OPCODE_CONST_CLASS: {
      DeterminismDomain is_det(DeterminismType::IS_DET);
      current_state->set_abstract_obj(RESULT_REGISTER, is_det);
      break;
    }
    case OPCODE_CHECK_CAST: {
      // the result depends on the source reg
      const auto aobj = current_state->get_abstract_obj(insn->src(0));
      current_state->set_abstract_obj(RESULT_REGISTER, aobj);
      break;
      // const auto aobj = current_state->get_abstract_obj(insn->src(0));
      // current_state->set_abstract_obj(
      //     RESULT_REGISTER,
      //     AbstractObjectDomain(
      //         AbstractObject(AbstractObjectKind::OBJECT, insn->get_type())));
      // const auto obj = aobj.get_object();
      // if (obj && obj->obj_kind == AbstractObjectKind::CLASS) {
      //   current_state->set_class_source(
      //       RESULT_REGISTER, current_state->get_class_source(insn->src(0)));
      // }
      // Note that this is sound. In a concrete execution, if the check-cast
      // operation fails, an exception is thrown and the control point
      // following the check-cast becomes unreachable, which corresponds to
      // _|_ in the abstract domain. Any abstract state is a sound
      // approximation of _|_.
      break;
    }
    case OPCODE_INSTANCE_OF: {
      // always DET regardless of input
      operation_with_single_operand(insn, current_state);
      // DeterminismDomain is_det(DeterminismType::IS_DET);
      // current_state->set_abstract_obj(RESULT_REGISTER, is_det);
      break;
      // const auto aobj = current_state->get_abstract_obj(insn->src(0));
      // auto obj = aobj.get_object();
      // // Append the referenced type here to the potential dex types list.
      // // Doing this increases the type information we have at the reflection
      // // site. It's up to the user of the analysis  how to interpret this
      // // information.
      // if (obj && (obj->obj_kind == AbstractObjectKind::OBJECT) &&
      //     obj->dex_type) {
      //   auto dex_type = insn->get_type();
      //   if (obj->dex_type != dex_type) {
      //     obj->potential_dex_types.insert(dex_type);
      //     current_state->set_abstract_obj(
      //         insn->src(0),
      //         AbstractObjectDomain(AbstractObject(obj->obj_kind,
      //         obj->dex_type,
      //                                             obj->potential_dex_types)));
      //   }
      // }

      // break;
    }
    case OPCODE_AGET_BYTE:
    case OPCODE_AGET_CHAR:
    case OPCODE_AGET_WIDE:
    case OPCODE_AGET_OBJECT: {
      // det_reg's DET depends on input
      // if the array is dt-top, the result is top
      // if the array is non-det, the result is nondet
      // if the array is det, the result is determined by the offset
      // default_semantics(insn, current_state);
      // break;
      // const auto source_object =
      // current_state->get_abstract_obj(insn->src(0));
      always_assert(insn->srcs_size() == 2);
      for (reg_t src : insn->srcs()) {
        printf("%s\n", SHOW(src));
      }
      const auto array_object = current_state->get_abstract_obj(insn->src(0));
      const auto offset_object = current_state->get_abstract_obj(insn->src(1));
      std::cout << "Debug arrary_object" << array_object << std::endl;

      if (array_object.is_top()) {
        current_state->set_abstract_obj(RESULT_REGISTER, dt_top);
        break;
      }
      if (array_object.equals(not_det)) {
        current_state->set_abstract_obj(RESULT_REGISTER, not_det);
        break;
      }
      if (array_object.equals(is_det)) {
        current_state->set_abstract_obj(RESULT_REGISTER, offset_object);
        break;
      }
      printf("finish aget_object\n");
      break;
    }
    case OPCODE_APUT_BYTE:
    case OPCODE_APUT_CHAR:
    case OPCODE_APUT:
    case OPCODE_APUT_OBJECT: {
      // insn format: aput <source> <array> <offset>
      // array_object's DET depends on input
      // if the array is dt-top, the result is top
      // if the array is non-det, the result is nondet
      // if the array is det, the result is determined by the offset and source
      // reg
      DeterminismDomain dt_top(DeterminismType::DT_TOP);
      DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);
      DeterminismDomain is_det(DeterminismType::IS_DET);
      DeterminismDomain not_det(DeterminismType::NOT_DET);
      always_assert(insn->srcs_size() == 3);
      const auto source_object = current_state->get_abstract_obj(insn->src(0));
      const auto array_object = current_state->get_abstract_obj(insn->src(1));
      const auto offset_object = current_state->get_abstract_obj(insn->src(2));

      if (array_object.equals(is_det)) {
        if (offset_object.equals(is_det) && source_object.equals(is_det)) {
          // the array object is still det
          break;
        } else {
          // the array_object's DET will get contaminated to be top
          DeterminismDomain result(offset_object.element());
          result.join_with(source_object);
          current_state->set_abstract_obj(insn->src(1), result);
          std::cout << "update array's value to" << result << std::endl;
          break;
        }
      } else {
        // the DET of the array is unchanged
        break;
      }
      // if (source_object && source_object->obj_kind == CLASS && array_object
      // &&
      //     array_object->is_known_class_array() && offset_object &&
      //     offset_object->obj_kind == INT) {

      //   auto type = source_object->dex_type;
      //   boost::optional<int64_t> offset = offset_object->dex_int;
      //   boost::optional<std::vector<DexType*>> class_array =
      //       current_state->get_heap_class_array(array_object->heap_address)
      //           .get_constant();

      //   if (offset && class_array && *offset >= 0 &&
      //       class_array->size() > (size_t)*offset) {
      //     (*class_array)[*offset] = type;
      //     current_state->set_heap_class_array(
      //         array_object->heap_address,
      //         ConstantAbstractDomain<std::vector<DexType*>>(*class_array));
      //   }
      // }
      // if (source_object && source_object->is_known_class_array()) {
      //   current_state->set_heap_addr_to_top(source_object->heap_address);
      // }
      // default_semantics(insn, current_state);
      // break;
    }
    case OPCODE_IPUT:
    case OPCODE_IPUT_WIDE:
    case OPCODE_IPUT_BOOLEAN:
    case OPCODE_IPUT_OBJECT: {
      if (insn->has_field()) {
        auto field = resolve_field(insn->get_field());
        const auto f_type = insn->get_field()->get_type();
        const auto f_cls = insn->get_field()->get_class();
        std::cout << "iput find field" << field->c_str() << f_type->c_str()
                  << f_cls->c_str() << std::endl;
        DeterminismDomain is_det(DeterminismType::IS_DET);
        current_state->set_field_value(
            field, current_state->get_abstract_obj(insn->src(0)));
        auto result = current_state->get_field_value(field);
        // current_state->set_abstract_obj(RESULT_REGISTER, result);
        std::cout << "set iget result to" << result << std::endl;
        break;
      }
    }
    case OPCODE_SPUT_OBJECT: {
      default_semantics(insn, current_state);
      break;
      //   const auto source_object =
      //       current_state->get_abstract_obj(insn->src(0)).get_object();
      //   if (source_object && source_object->is_known_class_array()) {
      //     current_state->set_heap_addr_to_top(source_object->heap_address);
      //   }
      //   break;
    }
    case OPCODE_IGET:
    case OPCODE_IGET_BOOLEAN:
    case OPCODE_IGET_WIDE:
    case OPCODE_IGET_OBJECT: {
      // reg0 is the dest reg, reg1 points to the this instance
      if (insn->has_field()) {
        auto field = resolve_field(insn->get_field());
        if (field == nullptr) {
          default_semantics(insn, current_state);
          break;
        } else {
          auto result = current_state->get_field_value(field);
          if (result.is_bottom()) {
            not_reached_log("field's value is bottom\n");
          }
          std::cout << "iget find field" << field->c_str() << std::endl;
          current_state->set_abstract_obj(RESULT_REGISTER, result);
          std::cout << "iget result is " << result  << "set to RESULT_REGISTER"<< std::endl;
          tmp_field_name = field;
          std::cout << tmp_field_name->c_str() << std::endl;

          
          break;
        }
      } else {
        not_reached_log("iget opcode does not have filed\n");
      }
    }
    case OPCODE_SGET_OBJECT: {
      default_semantics(insn, current_state);
      break;
      // always_assert(insn->has_field());
      // const auto field = insn->get_field();
      // DexType* primitive_type = check_primitive_type_class(field);
      // if (primitive_type) {
      //   // The field being accessed is a Class object to a primitive type
      //   // likely being used for reflection
      //   auto aobj = AbstractObject(AbstractObjectKind::CLASS,
      //   primitive_type); current_state->set_abstract_obj(RESULT_REGISTER,
      //                                   AbstractObjectDomain(aobj));
      //   current_state->set_class_source(
      //       RESULT_REGISTER,
      //       ClassObjectSourceDomain(ClassObjectSource::REFLECTION));
      // } else {
      //   update_non_string_input(current_state, insn, field->get_type());
      // }
      // break;
    }
    case OPCODE_NEW_INSTANCE: {
      DeterminismDomain is_det(DeterminismType::IS_DET);
      current_state->set_abstract_obj(RESULT_REGISTER, is_det);
      break;

      // current_state->set_abstract_obj(
      //     RESULT_REGISTER,
      //     AbstractObjectDomain(
      //         AbstractObject(AbstractObjectKind::OBJECT, insn->get_type())));
      // break;
    }

    case OPCODE_NEG_DOUBLE:
    case OPCODE_NEG_INT:
    case OPCODE_NEG_LONG:
    case OPCODE_NEG_FLOAT:
    case OPCODE_LONG_TO_INT:
    case OPCODE_LONG_TO_FLOAT:
    case OPCODE_LONG_TO_DOUBLE:
    case OPCODE_DOUBLE_TO_INT:
    case OPCODE_DOUBLE_TO_FLOAT:
    case OPCODE_DOUBLE_TO_LONG:
    case OPCODE_INT_TO_BYTE:
    case OPCODE_INT_TO_CHAR:
    case OPCODE_INT_TO_LONG:
    case OPCODE_INT_TO_DOUBLE: {
      always_assert(insn->srcs_size() == 1 && insn->has_dest());
      current_state->set_abstract_obj(
          insn->dest(), current_state->get_abstract_obj(insn->src(0)));
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(
            insn->dest() + 1, current_state->get_abstract_obj(insn->src(0)));
      }
      break;
    }
    case OPCODE_REM_INT_LIT8:
    case OPCODE_USHR_INT_LIT8:
    case OPCODE_AND_INT_LIT8:
    case OPCODE_XOR_INT_LIT8:
    case OPCODE_DIV_INT_LIT8:
    case OPCODE_MUL_INT_LIT8:
    case OPCODE_SHL_INT_LIT8:
    case OPCODE_SHR_INT_LIT8:
    case OPCODE_AND_INT_LIT16:
    case OPCODE_XOR_INT_LIT16:
    case OPCODE_DIV_INT_LIT16:
    case OPCODE_MUL_INT_LIT16:
    case OPCODE_ADD_INT_LIT16:
    case OPCODE_ADD_INT_LIT8: {
      operation_with_three_operands_last_is_constant(insn, current_state);
      break;
    }
    case OPCODE_AND_INT:
    case OPCODE_AND_LONG:
    case OPCODE_OR_INT:
    case OPCODE_OR_LONG:
    case OPCODE_XOR_INT:
    case OPCODE_XOR_LONG:
    case OPCODE_SHL_INT:
    case OPCODE_SHL_LONG:
    case OPCODE_SHR_INT:
    case OPCODE_SHR_LONG:
    case OPCODE_REM_DOUBLE:
    case OPCODE_REM_FLOAT:
    case OPCODE_REM_INT:
    case OPCODE_REM_LONG:
    case OPCODE_DIV_DOUBLE:
    case OPCODE_DIV_FLOAT:
    case OPCODE_DIV_INT:
    case OPCODE_DIV_LONG:
    case OPCODE_ADD_INT:
    case OPCODE_ADD_DOUBLE:
    case OPCODE_ADD_FLOAT:
    case OPCODE_ADD_LONG:
    case OPCODE_SUB_INT:
    case OPCODE_SUB_DOUBLE:
    case OPCODE_SUB_FLOAT:
    case OPCODE_MUL_LONG:
    case OPCODE_MUL_DOUBLE:
    case OPCODE_MUL_FLOAT:
    case OPCODE_MUL_INT:
    case OPCODE_SUB_LONG: {
      operation_with_three_operands_last_is_variable(insn, current_state);
      break;
    }
    case OPCODE_IF_EQ:
    case OPCODE_IF_NE:
    case OPCODE_IF_LT:
    case OPCODE_IF_LE:
    case OPCODE_IF_GT:
    case OPCODE_IF_GE:

    case OPCODE_IF_NEZ:
    case OPCODE_IF_LTZ:
    case OPCODE_IF_GEZ:
    case OPCODE_IF_GTZ:
    case OPCODE_IF_LEZ:
    case OPCODE_IF_EQZ: {
      // operation_with_single_operand(insn, current_state);
      // break;
      always_assert_log(!insn->has_move_result_any(),
                        "comparison has a result");
      always_assert_log(!insn->has_dest(), "comparison has a dest");

      // operation_with_two_operands(insn, current_state);
      break;
    }
    case OPCODE_MOVE_WIDE: {
      always_assert(insn->has_dest());
      always_assert(insn->srcs_size() == 1);
      always_assert(insn->srcs_size() == 1 && insn->has_dest());
      current_state->set_abstract_obj(
          insn->dest(), current_state->get_abstract_obj(insn->src(0)));
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(
            insn->dest() + 1, current_state->get_abstract_obj(insn->src(0)));
      }
      break;
    }
    case OPCODE_MOVE_EXCEPTION: {
      std::cout << "encounter move exception" << std::endl;

      // always_assert(insn->has_dest());
      // DeterminismDomain is_det(DeterminismType::IS_DET);
      // current_state->set_abstract_obj(insn->dest(), is_det);

      break;
    }
    case OPCODE_ARRAY_LENGTH: {
      operation_with_single_operand(insn, current_state);
      break;
    }
    case OPCODE_FILLED_NEW_ARRAY: {
      default_semantics(insn, current_state);
      break;
      // auto array_type = insn->get_type();
      // always_assert(type::is_array(array_type));
      // auto component_type = type::get_array_component_type(array_type);
      // AbstractObject aobj(AbstractObjectKind::OBJECT, insn->get_type());
      // if (component_type == type::java_lang_Class()) {
      //   auto arg_count = insn->srcs_size();
      //   std::vector<DexType*> known_types;
      //   known_types.reserve(arg_count);

      //   // collect known types from the filled new array
      //   for (auto src_reg : insn->srcs()) {
      //     auto reg_obj =
      //     current_state->get_abstract_obj(src_reg).get_object(); if (reg_obj
      //     && reg_obj->obj_kind == CLASS && reg_obj->dex_type) {
      //       known_types.push_back(reg_obj->dex_type);
      //     }
      //   }

      //   if (known_types.size() == arg_count) {
      //     AbstractHeapAddress addr = allocate_heap_address();
      //     ConstantAbstractDomain<std::vector<DexType*>>
      //     heap_array(known_types); current_state->set_heap_class_array(addr,
      //     heap_array); aobj = AbstractObject(AbstractObjectKind::OBJECT,
      //     addr);
      //   }
      // }

      // current_state->set_abstract_obj(RESULT_REGISTER,
      //                                 AbstractObjectDomain(aobj));
      // break;
    }
    case OPCODE_INVOKE_VIRTUAL: {
      // invoke-virtual {v0}, Ljava/util/Random;.nextDouble:()D // method@0228
      // deoes not have dest reg
      // v0 is the class that invokes the virtual method, i.e.,
      // Ljava/util/Random
      always_assert_log(!insn->has_dest(),
                        "invoke-virtual is expected to have zero dest");

      // std::cout << " func is" <<
      // insn->get_method()->get_class()->get_name()->c_str() << "+" <<
      // insn->get_method()->get_class()->get_name()->c_str();
      if (check_reset_func(insn, current_state)) {
        break;
      }
    }
    case OPCODE_INVOKE_STATIC: {
      //  invoke-static {v0, v1}, Ljava/lang/Double;.valueOf
      // does not have dest reg
      // v0 is the parameter, v1 is useless
      // note: invoke_static can have 0 src reg
      always_assert_log(!insn->has_dest(),
                        "invoke-static is expected to have zero dest");
      always_assert_log(insn->has_move_result_any(), "expect have move result");
      if (check_reset_func(insn, current_state)) {
        break;
      }
      if (insn->srcs_size() == 0) {
        DeterminismDomain is_det(DeterminismType::IS_DET);
        // the result is only determined by the callee_return
        process_virtual_call(insn, is_det, current_state, callee_return);
        break;
      }
    }
    case OPCODE_INVOKE_INTERFACE:
    case OPCODE_INVOKE_SUPER:
    case OPCODE_INVOKE_DIRECT: {
      // invoke-direct {v0, v1, v2}, Ljava/util/Random;.<init>:(J)V
      // does not have dest: v0 is the Random object, v1 is the parameter, v2 is
      // I don't know
      always_assert_log(!insn->has_dest(),
                        "invoke_direct is not expected to have dest reg");
      always_assert_log(
          (insn->srcs_size() >= 1),
          "invoke_direct should have at least one src reg that represent this");
      always_assert_log(insn->has_move_result_any(), "expect have move result");
      if (check_reset_func(insn, current_state)) {
        break;
      }
      if (insn->srcs_size() >= 2) {
        // the function has at least one argument
        // we use the join operator on these parameters, because we are not sure
        // how they are processed in the function
        std::cout << "the function has at least 2 arguments\n";
        DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);

        DeterminismDomain receiver = dt_bottom;
        // for (unsigned int i = 1; i < insn->srcs_size(); i++) {
        //   receiver.join_with(current_state->get_abstract_obj(insn->src(i)));
        // }
        // for (reg_t src : insn->srcs()) {
        //   std::cout << src << current_state->get_abstract_obj(src) <<
        //   std::endl;
        //   receiver.join_with(current_state->get_abstract_obj(src));
        // }
        if (is_static(m_dex_method)) {
          // the first parameter is a function parameter
          receiver.join_with(current_state->get_abstract_obj(insn->src(0)));
        }
        for (unsigned int i = 0; i < insn->srcs_size(); i++) {
          std::cout << insn->src(i)
                    << current_state->get_abstract_obj(insn->src(i))
                    << std::endl;
          receiver.join_with(current_state->get_abstract_obj(insn->src(i)));
        }
        std::cout << "receiver reg is" << receiver << std::endl;
        process_virtual_call(insn, receiver, current_state, callee_return);
        break;
      } else {
        // the function does not have argument
        std::cout << "the function has zero argument\n";
        auto receiver = current_state->get_abstract_obj(insn->src(0));
        std::cout << "receiver reg is" << insn->src(0) << receiver << std::endl;
        process_virtual_call(insn, receiver, current_state, callee_return);
        break;
      }

      // update_return_object_and_invalidate_heap_args(current_state, insn,
      //                                               callee_return);
      // break;
    }
    case OPCODE_RETURN_VOID: {
      break;
    }
    case OPCODE_RETURN:
    case OPCODE_RETURN_WIDE:
    case OPCODE_RETURN_OBJECT: {
      // default_semantics(insn, current_state);
      // break;
      
      printf("find return object for %s!!!!!! %s\n",
             m_dex_method->get_name()->c_str(),
             SHOW(insn));
      current_state->debugExpBlockDomain();
      // DeterminismDomain original = this->m_return_value;
      // DeterminismDomain joined_value = current_state->get_abstract_obj(insn->src(0));
      // std::cout << "joined return value is" << joined_value;
      // this->m_return_value.join_with(joined_value);
      // DeterminismDomain after = this->m_return_value;
      // if (!original.equals(DeterminismDomain(DeterminismType::DT_BOTTOM)) && original.leq(after) && !original.equals(after)) {
      //   std::cout << "merge return block loses precision " << this->m_return_value
      //           << std::endl;
      // }
      
      // std::cout << "new return value" << this->m_return_value << std::endl;
      current_state->set_return_value(current_state->get_abstract_obj(insn->src(0)));
      break;
    }
    case OPCODE_GOTO: {
      break;
    }
    default: {
      printf("process default semantics %s\n", SHOW(insn));
      default_semantics(insn, current_state);
    }
    }
  }

  boost::optional<DeterminismDomain> get_abstract_object(
      size_t reg, IRInstruction* insn) const {
    auto it = m_environments.find(insn);
    if (it == m_environments.end()) {
      return boost::none;
    }
    return it->second.get_abstract_obj(reg);
  }

  // boost::optional<ClassObjectSource> get_class_source(
  //     size_t reg, IRInstruction* insn) const {
  //   auto it = m_environments.find(insn);
  //   if (it == m_environments.end()) {
  //     return boost::none;
  //   }
  //   return it->second.get_class_source(reg).get_constant();
  // }

  ReturnValueDomain get_return_value() const { return m_return_value; }

  AbstractObjectEnvironment get_exit_state() const {
    return get_exit_state_at(m_cfg.exit_block());
  }
  void process_virtual_call(const IRInstruction* insn,
                            const DeterminismDomain& receiver,
                            AbstractObjectEnvironment* current_state,
                            const DeterminismDomain& callee_return) const {
    if (receiver.is_bottom()) {
      if (insn->has_dest()) {
        std::cout << "has dest" << insn->dest() << std::endl;

        current_state->set_abstract_obj(insn->dest(),
                                        DeterminismDomain::bottom());
        if (insn->dest_is_wide()) {
          current_state->set_abstract_obj(insn->dest() + 1,
                                          DeterminismDomain::bottom());
        }
      }
      if (insn->has_move_result_any()) {
        std::cout
            << "has move_result_any(), so set value from previous result reg"
            << current_state->get_abstract_obj(RESULT_REGISTER) << "to"
            << DeterminismDomain::bottom() << std::endl;
        current_state->set_abstract_obj(RESULT_REGISTER,
                                        DeterminismDomain::bottom());
      }
      return;
    }
    if (insn->has_dest()) {
      std::cout << "the instruction has dest\n";
    }
    DeterminismDomain result =
        operation_with_two_domains(receiver, callee_return, current_state);
    std::cout << "result of callee and args are" << result << std::endl;
    std::string method_name(insn->get_method()->get_name()->c_str());
    std::string class_name(insn->get_method()->get_class()->c_str());
    if (method_name.find("init") != std::string::npos) {
      if (insn->srcs_size() == 0) {
        // we don't analyze static field for now.
        std::cout << "encounter static function;" << method_name << std::endl;
        return;
      }
      current_state->set_abstract_obj(insn->src(0), result);
      return;
    }
    if (method_name.find("set") != std::string::npos && class_name.find("Calendar") == std::string::npos) {
      // class constructor does not have dest reg or remove any
      std::cout << "find function set() that set this instance field or dest reg" << insn->src(0) << "to"
                << callee_return << std::endl;
      if (insn->srcs_size() == 0) {
        // we don't analyze static field for now.
        std::cout << "encounter static function;" << method_name << std::endl;
        return;
      }
      if (m_reg_field_mapping->count(insn->src(0)) > 0) {
        std::cout << "find reg" << insn->src(0) << "in field mapping\n";
        auto field = m_reg_field_mapping->at(insn->src(0));
        current_state->set_field_value(field, callee_return);
        std::cout << "updated field value is" << current_state->get_field_value(field) << std::endl;
      } else {
        current_state->set_abstract_obj(insn->src(0), callee_return);

      }
        
      return;
    }
    if (method_name.find("append") != std::string::npos) {
      // class constructor does not have dest reg or remove any
      // this branch is to deal with string.append
      std::cout << "find function append set this instance" << insn->src(0) << "to"
                << result << std::endl;
      if (insn->srcs_size() == 0) {
        // we don't analyze static field for now.
        std::cout << "encounter static function;" << method_name << std::endl;
        return;
      }
      current_state->set_abstract_obj(insn->src(0), result);
      return;
    }
    if (insn->has_dest()) {
      std::cout << "has dest" << insn->dest() << std::endl;

      current_state->set_abstract_obj(insn->dest(), result);
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, result);
      }
    }
    if (insn->has_move_result_any()) {
      std::cout
          << "has move_result_any(), so set value from previous result reg"
          << current_state->get_abstract_obj(RESULT_REGISTER) << "to" << result
          << std::endl;
      current_state->set_abstract_obj(RESULT_REGISTER, result);
    }
  }
  bool analyze_gets_helper(const DetFieldPartition* whole_program_state,
                           const IRInstruction* insn,
                           AbstractObjectEnvironment* current_state) {
    if (whole_program_state == nullptr) {
      return false;
    }
    auto field = resolve_field(insn->get_field());
    if (field == nullptr) {
      return false;
    }
    // auto value = whole_program_state->get_field_value(field);
    // if (value.is_top()) {
    //   return false;
    // }
    // env->set(RESULT_REGISTER, value);
    // return true;
  }
  bool check_reset_func(const IRInstruction* insn,
                        AbstractObjectEnvironment* current_state) const {
    std::string class_name =
        insn->get_method()->get_class()->get_name()->c_str();
    std::string func_name = insn->get_method()->get_name()->c_str();
    std::string whole_name(class_name);
    whole_name.append(func_name);
    // std::cout << "whole name" << whole_name;
    if (this->m_reset_det_func->count(whole_name)) {
      std::cout << "find reset func!\n";
      // For now, the reset function changes the dest reg
      DeterminismDomain is_det(DeterminismType::IS_DET);
      // iterate its function argument
      DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);

      DeterminismDomain receiver = dt_bottom;
      for (unsigned int i = 1; i < insn->srcs_size(); i++) {
        std::cout << insn->src(i)
                  << current_state->get_abstract_obj(insn->src(i)) << std::endl;
        receiver.join_with(current_state->get_abstract_obj(insn->src(i)));
      }
      if (receiver.equals(is_det)) {
        std::cout << "func args are all det. so set class reg back to be det register number is:"
                  << insn->src(0) << std::endl;
        current_state->set_abstract_obj(insn->src(0), is_det);
        return true;
      } else {
        current_state->set_abstract_obj(insn->src(0), DeterminismDomain::top());
        return true;
      }

    } else {
      return false;
    }
  }

 private:
  const DexMethod* m_dex_method;
  const cfg::ControlFlowGraph& m_cfg;
  std::unordered_map<IRInstruction*, AbstractObjectEnvironment> m_environments;
  mutable ReturnValueDomain m_return_value;
  std::unordered_set<std::string>* m_reset_det_func;
  ExpBlockPartition* m_exp_partition = new ExpBlockPartition();
  DetFieldPartition* m_field_partition;
  mutable DexField* tmp_field_name;
  mutable RegFieldMapping * m_reg_field_mapping = new RegFieldMapping();
  IntraAnalyzerParameters m_ap;

  // a function that summarizes the callees' analysis result
  SummaryQueryFn* m_summary_query_fn;

  void operation_with_three_operands_last_is_constant(
      const IRInstruction* insn,
      AbstractObjectEnvironment* current_state) const {
    // (add-int/lit8 v0 v0 1)
    // A is dest, B is src
    // the result is determined by B
    // for (reg_t src : insn->srcs()) {
    //   printf("%s\n", SHOW(src));
    // }
    always_assert(insn->srcs_size() == 1);

    printf("three operands instruction %s\n", SHOW(insn));

    // const auto a_object = current_state->get_abstract_obj(insn->src(0));
    const auto b_object = current_state->get_abstract_obj(insn->src(0));

    std::cout << "propagate value" << b_object << std::endl;
    if (insn->has_dest()) {
      std::cout << "has dest" << insn->dest() << std::endl;

      current_state->set_abstract_obj(insn->dest(), b_object);
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, b_object);
      }
    } else {
      std::cout << "Carefule!!! no dest found but set the RESULT reg"
                << std::endl;
      current_state->set_abstract_obj(RESULT_REGISTER, b_object);
    }
    // current_state->set_abstract_obj(insn->dest(), b_object);
  }
  void operation_with_three_operands_last_is_variable(
      const IRInstruction* insn,
      AbstractObjectEnvironment* current_state) const {
    // For instructions that involve three regs A, B, C
    // A is dest, B, C is src
    // the result is determined by B and C
    always_assert(insn->srcs_size() == 2);

    printf("three operands instruction %s\n", SHOW(insn));

    // const auto a_object = current_state->get_abstract_obj(insn->src(0));
    const auto b_object = current_state->get_abstract_obj(insn->src(0));
    const auto c_object = current_state->get_abstract_obj(insn->src(1));

    DeterminismDomain result =
        operation_with_two_domains(b_object, c_object, current_state);
    if (insn->has_dest()) {
      std::cout << "has dest" << insn->dest() << std::endl;

      current_state->set_abstract_obj(insn->dest(), result);
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, result);
      }
    }
  }
  DeterminismDomain operation_with_two_domains(
      const DeterminismDomain a_object,
      const DeterminismDomain b_object,
      AbstractObjectEnvironment* current_state) const {
    DeterminismDomain dt_top(DeterminismType::DT_TOP);
    DeterminismDomain dt_bottom(DeterminismType::DT_BOTTOM);
    DeterminismDomain is_det(DeterminismType::IS_DET);
    DeterminismDomain not_det(DeterminismType::NOT_DET);
    std::cout << "two operands:" << a_object << b_object << "result is";
    if (a_object.is_top()) {
      std::cout << dt_top << std::endl;
      return DeterminismDomain::top();
      // current_state->set_abstract_obj(RESULT_REGISTER,
      //                                 DeterminismDomain::top());
    }
    if (a_object.equals(not_det)) {
      std::cout << not_det << std::endl;
      return not_det;
      // current_state->set_abstract_obj(RESULT_REGISTER, not_det);
    }
    if (a_object.equals(is_det)) {
      std::cout << b_object << std::endl;
      return b_object;
      // current_state->set_abstract_obj(RESULT_REGISTER, b_object);
    }
  }
  void operation_with_single_operand(
      const IRInstruction* insn,
      AbstractObjectEnvironment* current_state) const {
    // For instructions that involve operation only on one reg, such as
    // array-length, add_int: The result is determined by the abstract value of
    // the reg
    printf("single operand instruction %s\n", SHOW(insn));
    always_assert(insn->srcs_size() == 1);
    std::cout << "propagate value "
              << current_state->get_abstract_obj(insn->src(0)) << std::endl;
    current_state->set_abstract_obj(
        RESULT_REGISTER, current_state->get_abstract_obj(insn->src(0)));
  }

  void default_semantics(const IRInstruction* insn,
                         AbstractObjectEnvironment* current_state) const {
    // For instructions that are transparent for this analysis, we just need to
    // clobber the destination registers in the abstract environment. Note that
    // this also covers the MOVE_RESULT_* and MOVE_RESULT_PSEUDO_* instructions
    // following operations that are not considered by this analysis (it looks
    // like the reflectionanalysis only cares about object involving
    // operations). Hence, the effect of those operations is correctly
    // abstracted away regardless of the size of the destination register.
    std::cout << "default semantics" << std::endl;
    bool do_anything = false;
    if (insn->has_dest()) {
      do_anything = true;
      std::cout << "has dest" << insn->dest() << std::endl;

      current_state->set_abstract_obj(insn->dest(), DeterminismDomain(DeterminismType::DT_TOP));
      if (insn->dest_is_wide()) {
        current_state->set_abstract_obj(insn->dest() + 1, DeterminismDomain(DeterminismType::DT_TOP));
      }
    }
    // We need to invalidate RESULT_REGISTER if the instruction writes into
    // this register.
    if (insn->has_move_result_any()) {
      do_anything = true;
      std::cout << "set value from previous result reg"
                << current_state->get_abstract_obj(RESULT_REGISTER)
                << std::endl;
      current_state->set_abstract_obj(RESULT_REGISTER, DeterminismDomain(DeterminismType::DT_TOP));
    }
    if (do_anything) {
      std::cout << "be careful with what has been done\n";
    }
  }

  // After the fixpoint iteration completes, we replay the analysis on all
  // blocks and we cache the abstract state at each instruction. This cache is
  // used by get_abstract_object() to query the state of a register at a given
  // instruction. Since we use an abstract domain based on Patricia trees, the
  // memory footprint of storing the abstract state at each program point is
  // small.
  void populate_environments(const cfg::ControlFlowGraph& cfg) {
    // We reserve enough space for the map in order to avoid repeated
    // rehashing during the computation.
    printf("enter populate_env\n");
    m_environments.reserve(cfg.blocks().size() * 16);
    for (cfg::Block* block : cfg.blocks()) {
      AbstractObjectEnvironment current_state = get_entry_state_at(block);
      for (auto& mie : InstructionIterable(block)) {
        IRInstruction* insn = mie.insn;
        m_environments.emplace(insn, current_state);
        analyze_instruction(insn, &current_state);
      }
    }
  }
  void debug_context(CallingContext* context) {
    // printf("debug calling context size %d\n", context->size());
    for (int idx = 0; idx < 5; idx++) {
      std::cout << idx << context->get(idx) << std::endl;
    }
    // printf("finish debug calling context\n");
  }
};

} // namespace impl

DeterminismAnalysis::~DeterminismAnalysis() {
  // TODO
}
// TODO: pass another parameter as static field
DeterminismAnalysis::DeterminismAnalysis(
    DexMethod* dex_method,
    IntraAnalyzerParameters ap,
    CallingContext* context,
    SummaryQueryFn* summary_query_fn,
    std::unordered_set<std::string>* reset_det_func,
    DetFieldPartition* fields)
    : m_dex_method(dex_method), m_ap(ap) {
  always_assert(dex_method != nullptr);
  IRCode* code = dex_method->get_code();
  if (code == nullptr) {
    return;
  }
  if (fields == nullptr) {
    not_reached();
  }
  code->build_cfg(/* editable */ false);
  cfg::ControlFlowGraph& cfg = code->cfg();
  cfg.calculate_exit_block();
  m_analyzer = std::make_unique<impl::Analyzer>(
      dex_method, cfg, summary_query_fn, reset_det_func, fields, m_ap);
  printf("enter m_analyzer->run(context)\n");
  m_analyzer->run(context);
  // m_analyzer->get_analysis_result();
}

// void ReflectionAnalysis::get_reflection_site(
//     const reg_t reg,
//     IRInstruction* insn,
//     std::map<reg_t, ReflectionAbstractObject>* abstract_objects) const {
//   auto aobj = m_analyzer->get_abstract_object(reg, insn);
//   if (!aobj) {
//     return;
//   }
//   if (is_not_reflection_output(*aobj)) {
//     return;
//   }
//   boost::optional<ClassObjectSource> cls_src =
//       aobj->obj_kind == AbstractObjectKind::CLASS
//           ? m_analyzer->get_class_source(reg, insn)
//           : boost::none;
//   if (aobj->obj_kind == AbstractObjectKind::CLASS &&
//       cls_src == ClassObjectSource::NON_REFLECTION) {
//     return;
//   }
//   if (traceEnabled(REFL, 5)) {
//     std::ostringstream out;
//     out << "reg " << reg << " " << *aobj << " ";
//     if (cls_src) {
//       out << *cls_src;
//     }
//     out << std::endl;
//     TRACE(REFL, 5, " reflection site: %s", out.str().c_str());
//   }
//   (*abstract_objects)[reg] = ReflectionAbstractObject(*aobj, cls_src);
// }

// ReflectionSites ReflectionAnalysis::get_reflection_sites() const {
//   ReflectionSites reflection_sites;
//   auto code = m_dex_method->get_code();
//   if (code == nullptr) {
//     return reflection_sites;
//   }
//   auto reg_size = code->get_registers_size();
//   for (auto& mie : InstructionIterable(code)) {
//     IRInstruction* insn = mie.insn;
//     std::map<reg_t, ReflectionAbstractObject> abstract_objects;
//     for (size_t i = 0; i < reg_size; i++) {
//       get_reflection_site(i, insn, &abstract_objects);
//     }
//     get_reflection_site(RESULT_REGISTER, insn, &abstract_objects);

//     if (!abstract_objects.empty()) {
//       reflection_sites.push_back(std::make_pair(insn, abstract_objects));
//     }
//   }
//   return reflection_sites;
// }

DeterminismDomain DeterminismAnalysis::get_return_value() const {
  if (!m_analyzer) {
    // Method has no code, or is a native method.
    return DeterminismDomain::top();
  }
  return m_analyzer->get_return_value();
}

// boost::optional<std::vector<DexType*>> ReflectionAnalysis::get_method_params(
//     IRInstruction* invoke_insn) const {
//   auto code = m_dex_method->get_code();
//   IRInstruction* move_result_insn = nullptr;
//   auto ii = InstructionIterable(code);
//   for (auto it = ii.begin(); it != ii.end(); ++it) {
//     auto* insn = it->insn;
//     if (insn == invoke_insn) {
//       move_result_insn = std::next(it)->insn;
//       break;
//     }
//   }
//   if (!move_result_insn ||
//       !opcode::is_a_move_result(move_result_insn->opcode())) {
//     return boost::none;
//   }
//   auto arg_param = get_abstract_object(RESULT_REGISTER, move_result_insn);
//   if (!arg_param ||
//       arg_param->obj_kind != reflection::AbstractObjectKind::METHOD) {
//     return boost::none;
//   }
//   return arg_param->dex_type_array;
// }

// bool ReflectionAnalysis::has_found_reflection() const {
//   return !get_reflection_sites().empty();
// }

// boost::optional<AbstractObject> ReflectionAnalysis::get_abstract_object(
//     size_t reg, IRInstruction* insn) const {
//   if (m_analyzer == nullptr) {
//     return boost::none;
//   }
//   return m_analyzer->get_abstract_object(reg, insn);
// }

CallingContextMap DeterminismAnalysis::get_calling_context_partition() const {
  if (m_analyzer == nullptr) {
    return CallingContextMap::top();
  }
  printf("return a non top calling contextmap\n");
  return this->m_analyzer->get_exit_state().get_calling_context_partition();
}

} // namespace determinism
