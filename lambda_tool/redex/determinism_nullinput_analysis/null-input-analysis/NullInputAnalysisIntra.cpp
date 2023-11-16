#include "NullInputAnalysisIntra.h"
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
namespace nullinput {
NullInputLattice nullinput_lattice({BOTTOM, SAT, UNSAT, TOP},
                       {{BOTTOM, SAT},
                        {BOTTOM, UNSAT},
                        {SAT, TOP},
                        {UNSAT, TOP}});
std::ostream& operator<<(std::ostream& output,
                         const NullInputDomain& detDomain) {
  switch (detDomain.element()) {
  case BOTTOM: {
    output << "BOTTOM";
    break;
  }
  case SAT: {
    output << "SAT";
    break;
  }
  case UNSAT: {
    output << "UNSAT";
    break;
  }
  case TOP: {
    output << "TOP";
    break;
  }
  }
  return output;
}
namespace impl {

using namespace ir_analyzer;


// class AbstractObjectEnvironment models the program state under analysis
//                                           BasicAbstractObjectEnvironment,
//                                           ReturnValueDomain,
//                                           CallingContextMap,
//                                           DetFieldPartition>
class Analyzer final : public BaseIRAnalyzer<StringSetDomain> {
 public:
  explicit Analyzer(const DexMethod* dex_method,
                    const cfg::ControlFlowGraph& cfg,
                    SummaryQueryFn* summary_query_fn,
                    IntraAnalyzerParameters ap)
      : BaseIRAnalyzer(cfg),
        m_dex_method(dex_method),
        m_cfg(cfg),
        m_summary_query_fn(summary_query_fn),
        m_ap(ap) {}

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
    
    auto init_state = StringSetDomain::bottom();
    
    std::cout << "init state is bottom:" << init_state.is_bottom() << std::endl;


    // Get function signature
    const auto& signature =
        m_dex_method->get_proto()->get_args()->get_type_list();
    auto sig_it = signature.begin();

    std::cout << "testsignature size is" << signature.size() << std::endl;
    // debug_context(context);
    printf("******done processing parameters\n");
    // std::cout << "Debug arrary_object" << d_ins->dest()
    //           << init_state.get_abstract_obj(d_ins->dest()) << std::endl;
    printf("******begin fixpoint iterator on cfg run\n");
    // run is a method inherit from the BaseIRAnalyzer
    MonotonicFixpointIterator::run(init_state);
    printf("******done fixpoint iterator run\n");
    printf("******collect return state starts\n");
    

    // auto env = MonotonicFixpointIterator::get_exit_state_at(m_cfg.exit_block());
    // m_return_value = env.get_return_value();
    // std::cout << "exit state return value domain: " << env.get_return_value() << std::endl;

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
 
  void analyze_node(const cfg::GraphInterface::NodeId& node,
                    StringSetDomain* current_state) const override {
    std::cout<< "analyzing node in self-definied mode" <<std::endl;
    // if this node is an exception handling block, we add it to the
    // ExpBlockPartition that tracks exception information
    // IRInstruction* insn = node->get_last_insn();
    bool first_node = false;
    auto first_it = node->get_first_insn();
    auto last_it = node->get_last_insn();
    if (last_it == node->end() || first_it == node->end()) {
      return;
    }
    always_assert_log(last_it != node->end(), "cannot analyze empty block");
    always_assert_log(first_it != node->end(), "cannot analyze empty block");
    auto first_insn = first_it->insn;
    auto last_insn = last_it->insn;
    auto first_op = first_insn->opcode();
    auto last_op = last_insn->opcode();
    std::cout << "debug first and last insn" << show(first_insn) << show(last_insn) << std::endl; 
    if (first_op != OPCODE_GOTO && first_op != OPCODE_IF_EQZ && first_op != OPCODE_IF_NEZ) {
      if (last_op != OPCODE_RETURN_OBJECT) {
        if ((last_op == OPCODE_IF_EQZ || last_op == OPCODE_IF_NEZ) && (first_op == IOPCODE_LOAD_PARAM || first_op == IOPCODE_LOAD_PARAM_OBJECT)) {
          // this is the first null check block
          first_node = true;
        } else {
          std::cout << "this block is not pure null check, so erase previous state\n";
          if (!current_state->is_bottom()) {
            current_state->set_to_bottom();
          }
          return;
        }

      } else {
        std::cout << "encounter a return reg block\n";
        // if (!first_insn->has_literal()) {
        //   not_reached_log("the instruction does not have a literal\n");
        // }
        if (first_op == OPCODE_CONST && first_insn->has_literal()&& first_insn->get_literal() == 0) {
          std::cout << "encounter a return null block\n";
          std::cout << show(first_insn) << std::endl;
        } else if (first_op == OPCODE_RETURN_OBJECT) {
          std::cout << "encounter a return null block that only has 1 instruction\n";
        } else {
            std::cout << "does not encounter a return null block, print first instruction for debugging\n";
            std::cout << show(first_insn) << std::endl;
            current_state->set_to_bottom();
        }
        
        return;
      }
    } 
      // The first instruction is: goto, if_eqz, and if_nez
      // The last instructin for the first block is either if_eqz or if_nez
      // We must verify whether the reg in currentstate indeed corresponds to
      // reg==null
      // for each predecessor, we verify if its edge correspond to the true path
      // for reg == null
    std::cout << "encounter a possible null check block\n";
    
    if (!current_state->is_bottom()) {
      std::cout << "null check on predecessor's\n";
      auto anchors = current_state->elements();
      for (auto p: node->preds()) {
        auto src_node = p->src();
        auto src_last_insn = src_node->get_last_insn()->insn;
        auto src_last_insn_op = src_last_insn->opcode();
        auto last_reg_name = DexString::make_string(std::to_string(src_last_insn->src(0)));
        auto edge = p;
        if (src_last_insn_op != OPCODE_IF_EQZ && src_last_insn_op != OPCODE_IF_NEZ) {
          std::cout << "predecessor's last insn is" << show(src_last_insn) << std::endl;
          std::cout << "the predecessor's branch is not from null check, remove the reg from the current state" << last_reg_name << std::endl;
          current_state->remove(last_reg_name);
        }
        switch (src_last_insn_op) {
          // check if the edge takes the reg == null branch. if not, remove
          // reg from the current_state
          case OPCODE_IF_EQZ:
            if (edge->type() == cfg::EdgeType::EDGE_BRANCH) {
              break;
            }
          case OPCODE_IF_NEZ:
            if (edge->type() == cfg::EdgeType::EDGE_GOTO) {
              break;
            }
          default: 
            std::cout << "the edge is false for reg == null branch, remove the reg from the current state\n";
            current_state->remove(last_reg_name);
        }
        std::cout << "finish examine predecessors, now debug current state\n";
      }
    }
    if (first_node) {
      std::cout << "process first node of the cfg\n";

      // for first node, do 2 things:
      // 1. create a vector to store function parameters
      // done in populate_environment
      // 2. add reg to current_state 
      auto reg_name = DexString::make_string(std::to_string(last_insn->src(0)));
      std::cout << "add reg" << show(reg_name) << "\n";
      if (current_state->is_bottom()) {
        current_state->join_with(StringSetDomain());
      }
      current_state->add(reg_name);
      debug_stringset(*current_state);
    } else {
      // now, we add possible reg to the current_state
      if (first_op == OPCODE_GOTO) {
        // do nothing, goto just preserve the state
        std::cout << "go to just preserve the state\n";
      } else {
        // add the register that is being compared to the current state
        auto reg_name = DexString::make_string(std::to_string(first_insn->src(0)));
        std::cout << "add reg" << show(reg_name) << "\n";
        if (current_state->is_bottom()) {
          current_state->join_with(StringSetDomain());
        }
        current_state->add(reg_name);
        debug_stringset(*current_state);
      }
    }
    

    // if (node->preds().size() > 1 && op != OPCODE_RETURN) {
    //   // This means the current block is not guaranteed to come from any "reg ==
    //   // null"
    //   current_state->add(DexString::make_string("invalid")); 
    // } else if (node->preds().size() == 1) {
    //   auto only_predecessor = (node->preds().front())->src();
    //   if (current_state->is_bottom()) {
    //     std::cout << "there is no reg == null yet\n";
    //   } else {
    //     always_assert_log(current_state->elements().size() > 0, "there must be at least one reg == null");
    //     if (op != OPCODE_GOTO) {
    //       // We currently makes the current state invalid if the block is not
    //       // for managing null input handling (assuming only goto and return
    //       // block is for it)
    //       current_state->add(DexString::make_string("invalid")); 
    //     } else {
    //       // a goto block preserve the current state
    //     }

    //   }
      
    // }
    
    for (auto& mie : InstructionIterable(node)) {
      analyze_instruction(mie.insn, current_state);
    }
    
  }
  void analyze_instruction(
      const IRInstruction* insn,
      StringSetDomain* current_state) const override {
    printf("process instructions %s\n", SHOW(insn));
    std::cout << "--- instruction analysis end ---\n";
  }

  StringSetDomain get_return_value() {
    std::cout << "get return value\n";
    auto result = get_exit_state();
    debug_stringset(result);

    return result;
    // return m_return_value; 
  }
  std::vector<DexString*> get_parametersset() {
    return m_parameters;
  }

  StringSetDomain get_exit_state() const {
    return get_exit_state_at(m_cfg.exit_block());
  }
  
 private:
  const DexMethod* m_dex_method;
  const cfg::ControlFlowGraph& m_cfg;
  std::unordered_map<IRInstruction*, StringSetDomain> m_environments;
  std::vector<DexString*> m_parameters;
  mutable StringSetDomain m_return_value;
  std::unordered_set<std::string>* m_reset_det_func;
  IntraAnalyzerParameters m_ap;

  // a function that summarizes the callees' analysis result
  SummaryQueryFn* m_summary_query_fn;

  void default_semantics(const IRInstruction* insn,
                         StringSetDomain* current_state) const {
    // For instructions that are transparent for this analysis, we just need to
    // clobber the destination registers in the abstract environment. Note that
    // this also covers the MOVE_RESULT_* and MOVE_RESULT_PSEUDO_* instructions
    // following operations that are not considered by this analysis (it looks
    // like the reflectionanalysis only cares about object involving
    // operations). Hence, the effect of those operations is correctly
    // abstracted away regardless of the size of the destination register.
    // std::cout << "default semantics: just preserve" << current_state->get_abstract_obj()<< std::endl;
    
    // bool do_anything = false;
    // if (insn->has_dest()) {
    //   do_anything = true;
    //   std::cout << "has dest" << insn->dest() << std::endl;

    //   current_state->set_abstract_obj(insn->dest(), DeterminismDomain(DeterminismType::DT_TOP));
    //   if (insn->dest_is_wide()) {
    //     current_state->set_abstract_obj(insn->dest() + 1, DeterminismDomain(DeterminismType::DT_TOP));
    //   }
    // }
    // // We need to invalidate RESULT_REGISTER if the instruction writes into
    // // this register.
    // if (insn->has_move_result_any()) {
    //   do_anything = true;
    //   std::cout << "set value from previous result reg"
    //             << current_state->get_abstract_obj(RESULT_REGISTER)
    //             << std::endl;
    //   current_state->set_abstract_obj(DeterminismDomain(DeterminismType::DT_TOP));
    // }
    // if (do_anything) {
    //   std::cout << "be careful with what has been done\n";
    // }
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
    printf("enter populate_env, populate parameter set\n");
    param_index_t param_position = 0;
    for (const auto& mie :
           InstructionIterable(cfg.get_param_instructions())) {
      IRInstruction* insn = mie.insn;
      printf("process load param %s\n", SHOW(insn));
      if (insn->opcode() == IOPCODE_LOAD_PARAM || insn->opcode() == IOPCODE_LOAD_PARAM_OBJECT || insn->opcode() == IOPCODE_LOAD_PARAM_WIDE) {
        // if (insn->srcs_size() > 0) {
        if (param_position == 0 && !is_static(m_dex_method)) {
          // the first parameter correspond to 'this', not parameter
        } else {
          auto parameter_register_name = DexString::make_string(std::to_string(insn->dest()));
          m_parameters.push_back(parameter_register_name);
        }
        param_position++;
      }
    }

  }
    // m_environments.reserve(cfg.blocks().size() * 16);
    // for (cfg::Block* block : cfg.blocks()) {
    //   StringSetDomain current_state = get_entry_state_at(block);
    //   for (auto& mie : InstructionIterable(block)) {
    //     IRInstruction* insn = mie.insn;
    //     m_environments.emplace(insn, current_state);
    //     analyze_instruction(insn, &current_state);
    //   }
    // }

  void debug_stringset(StringSetDomain s) const{
    // By design, the analysis can't generate the Top value.
    always_assert(!s.is_top());
    if (s.is_bottom()) {
      // This means that some code in the method is unreachable.
      std::cout << "string set is bottom\n";
      return;
    }
    auto anchors = s.elements();
    if (anchors.empty()) {
      // The denotation of the anchor set is just the `null` reference. This is
      // represented by a special points-to variable.
      std::cout << "string set is empty\n";
      return;
    } else {
        std::cout << "string set is not empty\n";

        for (const DexString* e: anchors) {
            std::cout << e->c_str() << ";";
        }
        std::cout << std::endl;

    }
  }

};

} // namespace impl

NullInputAnalysis::~NullInputAnalysis() {
  // TODO
}
// TODO: pass another parameter as static field
NullInputAnalysis::NullInputAnalysis(
    DexMethod* dex_method,
    IntraAnalyzerParameters ap,
    CallingContext* context,
    SummaryQueryFn* summary_query_fn)
    : m_dex_method(dex_method), m_ap(ap) {
  always_assert(dex_method != nullptr);
  IRCode* code = dex_method->get_code();
  if (code == nullptr) {
    return;
  }
  code->build_cfg(/* editable */ false);
  cfg::ControlFlowGraph& cfg = code->cfg();
  cfg.calculate_exit_block();
  m_analyzer = std::make_unique<impl::Analyzer>(
      dex_method, cfg, summary_query_fn, m_ap);
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

NullInputDomain NullInputAnalysis::get_nullinput_result() const {
  std::cout << "invoke get_nullinput_result\n";
  NullInputDomain result = NullInputDomain::bottom();
  StringSetDomain parameterset = StringSetDomain();
  auto code = m_dex_method->get_code();
  if (code == nullptr) {
    return result;
  }
  auto nullinput_regs = m_analyzer->get_return_value();
  auto parameters = m_analyzer->get_parametersset();
  parameterset.add(parameters.begin(), parameters.end());
  std::cout << "debug parameters set\n";
  for (auto item: parameters) {
    std::cout << show(*item);
  }
  if (nullinput_regs.equals(parameterset)) {
    result.join_with(NullInputDomain(NullInputType::SAT));
  } else if (parameterset.leq(nullinput_regs)) {
    // the parameterset is a subset of null regs
    result.join_with(NullInputDomain(NullInputType::SAT));
  } else {
    result.join_with(NullInputDomain(NullInputType::UNSAT));
  }
  std::cout <<m_dex_method->c_str()<< "get nullinput result is" << result << std::endl;
  return result;
}

StringSetDomain NullInputAnalysis::get_null_check_result_null() const {
  std::cout << "get return value from the m_analyzer()\n";
  if (!m_analyzer) {
    // Method has no code, or is a native method.
    return StringSetDomain::top();
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

CallingContextMap NullInputAnalysis::get_calling_context_partition() const {
  return CallingContextMap::top();

  // if (m_analyzer == nullptr) {
  // }
  // printf("return a non top calling contextmap\n");
  // return this->m_analyzer->get_exit_state().get_calling_context_partition();
}

} // namespace parallelsafe

