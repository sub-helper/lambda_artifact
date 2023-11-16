/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ParallelSafetyAnalysisIntra.h"

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

// std::ostream& operator<<(std::ostream& output, const DeterminismType& det) {
//   switch (det) {
//   case DT_BOTTOM: {
//     output << "BOTTOM";
//     break;
//   }
//   case IS_DET: {
//     output << "SAFE";
//     break;
//   }
//   case NOT_DET: {
//     output << "UNSAFE";
//     break;
//   }
//   case DT_TOP: {
//     output << "TOP";
//     break;
//   }
//   }
//   return output;
// }

namespace parallelsafe {
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
    output << "SAFE";
    break;
  }
  case NOT_DET: {
    output << "UNSAFE";
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
                    IntraAnalyzerParameters ap)
      : BaseIRAnalyzer(cfg),
        m_dex_method(dex_method),
        m_cfg(cfg),
        m_summary_query_fn(summary_query_fn),
        m_reset_det_func(reset_det_func),
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
    auto track_exception = m_ap.track_exception;
    std::cout << "track exception is" << track_exception << std::endl;
    auto init_state = AbstractObjectEnvironment::top();
    init_state.set_abstract_obj(DeterminismDomain(DeterminismType::IS_DET));
    init_state.set_return_value(DeterminismDomain(DeterminismType::IS_DET));
    std::cout << "init state is" << init_state.get_abstract_obj() << std::endl;


    m_return_value.set_to_bottom();
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
    

    auto env = MonotonicFixpointIterator::get_exit_state_at(m_cfg.exit_block());
    m_return_value = env.get_return_value();
    std::cout << "exit state return value domain: " << env.get_return_value() << std::endl;

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
                    AbstractObjectEnvironment* current_state) const override {
    std::cout<< "analyzing node in self-definied mode" <<std::endl;
    // if this node is an exception handling block, we add it to the
    // ExpBlockPartition that tracks exception information
    std::cout << "--- propagate exception information start ---\n";
    if (node->starts_with_move_exception()) {
      // For a basic block that is the original source of the exception, it
      // always sets the current state to FROM_EX
      std::cout << "analyze a block that handles exception, that makes the UDF not parallel safe\n";
      current_state->set_abstract_obj(DeterminismDomain(DeterminismType::NOT_DET));
    } else {
      std::cout << "--- propagate exception information end ---\n";
      for (auto& mie : InstructionIterable(node)) {
        analyze_instruction(mie.insn, current_state);
      }
    }
  }
  void analyze_instruction(
      const IRInstruction* insn,
      AbstractObjectEnvironment* current_state) const override {
    printf("process instructions %s\n", SHOW(insn));
    std::cout << "--- instruction analysis start ---\n";
    ReturnValueDomain callee_return; // we need this value analysis after the
                                     // invocation instruction
    callee_return.set_to_bottom();
    if (opcode::is_an_invoke(insn->opcode())) {
      if (m_summary_query_fn) {
        callee_return = (*m_summary_query_fn)(insn);
        std::cout << "find callee return" << callee_return;
        current_state->join_abstract_obj(callee_return);
      } else {
        current_state->join_abstract_obj(DeterminismDomain(DeterminismType::NOT_DET));
      }
      return;
    }
    switch (insn->opcode()) {
    case OPCODE_CONST_STRING: {
      printf("process constant string %s\n", SHOW(insn), insn->get_string());
      std::string query_string = insn->get_string()->str();
      if (query_string.find("update") != std::string::npos) {
        // The update query makes the UDF parallel unsafe
        current_state->set_abstract_obj(DeterminismDomain(DeterminismType::NOT_DET));
      }
      break;

    }
    case OPCODE_RETURN:
    case OPCODE_RETURN_WIDE:
    case OPCODE_RETURN_OBJECT: {
    }
    default: {
      printf("process default semantics %s\n", SHOW(insn));
      default_semantics(insn, current_state);
      }
    }

  }

  ReturnValueDomain get_return_value() const {
    return get_exit_state().get_return_value();
    // return m_return_value; 
  }

  AbstractObjectEnvironment get_exit_state() const {
    return get_exit_state_at(m_cfg.exit_block());
  }
  
 private:
  const DexMethod* m_dex_method;
  const cfg::ControlFlowGraph& m_cfg;
  std::unordered_map<IRInstruction*, AbstractObjectEnvironment> m_environments;
  mutable ReturnValueDomain m_return_value;
  std::unordered_set<std::string>* m_reset_det_func;
  ExpBlockPartition* m_exp_partition = new ExpBlockPartition();
  DetFieldPartition* m_field_partition;
  IntraAnalyzerParameters m_ap;

  // a function that summarizes the callees' analysis result
  SummaryQueryFn* m_summary_query_fn;

  void default_semantics(const IRInstruction* insn,
                         AbstractObjectEnvironment* current_state) const {
    // For instructions that are transparent for this analysis, we just need to
    // clobber the destination registers in the abstract environment. Note that
    // this also covers the MOVE_RESULT_* and MOVE_RESULT_PSEUDO_* instructions
    // following operations that are not considered by this analysis (it looks
    // like the reflectionanalysis only cares about object involving
    // operations). Hence, the effect of those operations is correctly
    // abstracted away regardless of the size of the destination register.
    std::cout << "default semantics: just preserve" << current_state->get_abstract_obj()<< std::endl;
    
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

ParallelSafetyAnalysis::~ParallelSafetyAnalysis() {
  // TODO
}
// TODO: pass another parameter as static field
ParallelSafetyAnalysis::ParallelSafetyAnalysis(
    DexMethod* dex_method,
    IntraAnalyzerParameters ap,
    CallingContext* context,
    SummaryQueryFn* summary_query_fn,
    std::unordered_set<std::string>* reset_det_func)
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
      dex_method, cfg, summary_query_fn, reset_det_func, m_ap);
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

DeterminismDomain ParallelSafetyAnalysis::get_return_value() const {
  std::cout << "get return value from the m_analyzer()\n";
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

CallingContextMap ParallelSafetyAnalysis::get_calling_context_partition() const {
  return CallingContextMap::top();

  // if (m_analyzer == nullptr) {
  // }
  // printf("return a non top calling contextmap\n");
  // return this->m_analyzer->get_exit_state().get_calling_context_partition();
}

} // namespace parallelsafe

