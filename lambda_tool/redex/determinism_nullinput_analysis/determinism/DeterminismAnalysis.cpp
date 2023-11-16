/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "DeterminismAnalysis.h"
#include "DeterminismAnalysisIntra.h"
// Define an abstract domain as a summary for a method. The summary should
// contain what properties we are interested in knowing such a method.
#include "Trace.h"
#include<json/writer.h>


#include "ConstantAbstractDomain.h"
#include "ControlFlow.h"
#include "DexClass.h"
#include "HashedAbstractPartition.h"
#include "HashedSetAbstractDomain.h"
#include "IRCode.h"
#include "IRInstruction.h"
#include "InstructionAnalyzer.h"
#include "PatriciaTreeMapAbstractEnvironment.h"
#include "PatriciaTreeMapAbstractPartition.h"
#include "FieldOpTracker.h"
#include "Show.h"
#include "SpartaInterprocedural.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdio>

namespace {
using namespace determinism;

using namespace sparta;
using namespace sparta_interprocedural;

constexpr const IRInstruction* CURRENT_PARTITION_LABEL = nullptr;
DetFieldPartition instance_fields;

// Description for the analysis

// - We initialize the determinism of each method to Top, which means unknown or
//   potentially infinite depth of calls.
// - Every step will progressively reduce the depth by considering the cases
//   where the depth is known and is not Top.
// - The steps are iterated until a global fixpoint for the summaries is found.

// Callsite is mostly used to describe calling context. It can be partitioned
// based on call edges. In this analysis, the call depth is irrelevant to the
// calling context, so we leave it unused.
struct Caller {
  /*
   * ArgumentDomain describes the determinism-valued arguments for a given
   * method or callsite. The n'th argument will be represented by a binding of n
   * to a DeterminismDomain instance.
   */
  using ArgumentDomain = determinism::CallingContext;

  //     sparta::PatriciaTreeMapAbstractPartition<param_index_t,
  //                                              DeterminismDomain>;
  using Domain =
      sparta::PatriciaTreeMapAbstractEnvironment<const IRInstruction*, ArgumentDomain>;
  /* Note: Domain is an abstraction of the
   * execution paths starting from the entry point of a method and ending at an
   * invoke instruction, hence the use of an abstract partitioning.
   *
   * At method entry, this map contains a single item, a binding of the null
   * pointer to an ArgumentDomain representing the input arguments (assume the
   * method will always have the same input). At method exit, this map will have
   * bindings from all the invoke-* instructions contained in the method to the
   * ArgumentDomains representing the arguments passed to the callee.
   */
  //     its usage is when analyze_edge during interprocedural analysis, because
  //     it needs to get entry_state_at_callee.
  // entry_state_at_dest.set(CURRENT_PARTITION_LABEL,
  //                       exit_state_at_source.get(insn))
  //                       ()IPConstantPropagationAnalysis.cpp;
  // A map from callee to its calling context.
  // The default of Domain produces bottom()

  Domain analyze_edge(const std::shared_ptr<call_graph::Edge>& edge,
                      const Domain& exit_state_at_source) {
    // this function sets up the entry_state_at_dest (starting context) for analyzing of functions
    auto ins = edge->invoke_insn();
    printf("enter analyze_edge in Determinismanalysis.cpp %s\n", SHOW(ins));
    auto caller = edge->caller()->method();
    if (edge->caller()->is_entry()) {
      printf("caller is a ghost entry block\n");
    } else {
      printf("caller is not a ghost entry block\n");
      printf("caller method is %s\n", SHOW(caller));
    }
    auto callee = edge->callee()->method();
    if (edge->callee()->is_exit()) {
      printf("callee is a ghost exit block\n");
    } else {
      printf("callee is not a ghost exit block\n");
      printf("callee method is %s\n", SHOW(callee));
    }
    if (!callee) {
      printf("callee is nullptr, return a default bottom domain\n");
      return Domain::bottom(); // it means the invoked method is a dead point
    }
    printf("callee is not nullptr\n");

    Domain arg_domain;
    // the default partition should produce the bottom value.
    // what does it do?
    // 1. If find the existing abstract domain at the label ins
    // 2. It updates the domain following the defined lambda function
    // I think the current implementation is problemetic: the current lambda
    // function does nothing. Instead, I think it should be just getting the
    // argument domain from the Domain structure arg_domain.update(ins,
    // [&](const ArgumentDomain&) {
    //   return exit_state_at_source.get(ins);
    // });
    // return arg_domain;
    // create a fresh Domain
    Domain entry_state_at_dest;
    auto insn = edge->invoke_insn();
    if (insn == nullptr) {
      printf("invoke instruction is nullptr, get a top argumentdomain\n");
      entry_state_at_dest.set(CURRENT_PARTITION_LABEL, ArgumentDomain::top());
    } else {
      printf("invoke instruction is not nullptr, try get domain from exit_state_at_source\n");
      entry_state_at_dest.set(CURRENT_PARTITION_LABEL,
                              exit_state_at_source.get(insn));
    }
    std::cout << "new entry state is" << CURRENT_PARTITION_LABEL << entry_state_at_dest.get(insn) << std::endl;
    return entry_state_at_dest;
  }
};

// Core part of the analysis. This analyzer should be similar to an
// intraprocedural analysis, except that we have access to the summaries and the
// calling context.
// summaries sound useful for dealing with native functions
// it should be derived from the Intraprocedural?!
template <typename Base>
class DeterminismFunctionAnalyzer : public Base {
 private:
  const DexMethod* m_method;
  // It store the return value anlaysis result from the intra-analyzer
  DeterminismDomain m_domain;


 public:
  explicit DeterminismFunctionAnalyzer(const DexMethod* method)
      : m_method(method), m_domain(DeterminismDomain::top()) {}
  // This is the main function for performing intraprocedural analysis
  void debug_boolean_parameters() {
    printf("track_exception");
    std::cout << this->get_analysis_parameters()->track_exception << std::endl;
  }
  void analyze() override {
    // std::vector<std::string> function_names;
    for (int i = 0; i < this->get_analysis_parameters()->function_names.size();
         i++) {
      printf("get_analysis_parameters check %s\n",
             this->get_analysis_parameters()->function_names[i].c_str());
    }
    if (m_method) {
      printf("Intra anlaysis on a function %s \n",
             m_method->get_name()->c_str());

    } else {
      // ghost exit or ghost entry?
      printf(
          "Intra anlaysis on a function that is ghost enetry or exit\n");
      return;
    }
    // This very long lambda function queries about the data flow fact after
    // function call
    // Input: instruction that invokes a method, Output: DeterminismDomain
    determinism::SummaryQueryFn query_fn =
        [&](const IRInstruction* insn) -> DeterminismDomain {
      auto callees = call_graph::resolve_callees_in_graph(
          *this->get_call_graph(), m_method, insn);
      auto &map = (*this->get_call_graph()).get_insn_to_callee();
      std::cout << "size of insn->calee" << map.size()<<std::endl;
      DeterminismDomain ret = DeterminismDomain::bottom();
      auto func_name = insn->get_method()->c_str();
      auto class_name = insn->get_method()->get_class()->c_str();
      std::cout << "callee name is" << class_name << "." << func_name
                << std::endl;
      std::string buf(class_name);
      // buf stores the class_name+func_name
      buf.append(func_name);
      // second_buff stores the wildcard for the entire function (for easier
      // function labeling)
      std::string second_buff(class_name);
      second_buff.append("*");
      // Here is how it utilizes previous analysis results of callee functions
      if (this->get_analysis_parameters()->func_domain_map.count(buf)) {
        std::cout << "find and exisitng label from individual function lable"
                  << (this->get_analysis_parameters()->func_domain_map)[buf]
                  << std::endl;
        return (this->get_analysis_parameters()->func_domain_map)[buf];
      } else if (this->get_analysis_parameters()->func_domain_map.count(
                     second_buff)) {
        std::cout
            << "find and exisitng label from class that has wildcard"
            << (this->get_analysis_parameters()->func_domain_map)[second_buff]
            << std::endl;
        return (this->get_analysis_parameters()->func_domain_map)[second_buff];
      } else {
        std::cout << SHOW(insn) << "does not find manual label for callee" << std::endl;
        // always_assert_log(callees.size() > 0,
        //                   "%s does not find callee or existing label",
        //                   SHOW(insn));
      }
      printf("now try to find summary of the callee\n");
      for (const DexMethod* method : callees) {
        auto domain =
            this->get_summaries()->get(method, DeterminismDomain::top());
        ret.join_with(domain);
      }
      if (ret.is_bottom()) {
        // Two possible reasons on why the return value is bottom: 1. callee is not in the graph. 2. callee returns void.
        // always_assert_log(callees.size() == 0,
        //                   "callee is not in the graph, and callee has no manual label, so we return top as a default",
        //                   SHOW(insn));
        return DeterminismDomain::top();
      }

      return ret;
    };
    // CallerContext has the type: Callsite::Domain;
    printf("try to get calling context\n");
    auto context = this->get_caller_context()->get(CURRENT_PARTITION_LABEL);
    if (context.is_bottom()) {
      printf("in analysis.cpp, encounter a default bottom context\n");
    } else {
      printf("in analysis.cpp, encounter a non-bottom context\n");
    }
    std::unordered_set<std::string> func_resetdet_set =
        this->get_analysis_parameters()->func_reset_det_set;
    // Flow-insensitive Intraprocedural anlaysis is performed when the analysis
    // is initialized. I think it should use the registry member varaible in the
    // class of Intraprocedural base
    printf("enter intra analysis\n");
    debug_boolean_parameters();
    IntraAnalyzerParameters ap;
    ap.track_exception = this->get_analysis_parameters()->track_exception;
    determinism::DeterminismAnalysis analysis(const_cast<DexMethod*>(m_method),ap,
                                              &context, &query_fn,
                                              &func_resetdet_set, &instance_fields);
    printf("finish intra analysis\n");                                          
    // After intra analysis is done, we need to update the following things:
    // 1. Use the intraprocedural result to update m_domain, which later (in summarize()) will be
    //    used to update registry (function summary)
    // the initial assumption of the function result is top. So we refine it
    // using the meet operator.
    m_domain.meet_with(analysis.get_return_value());
    printf("now try to update the calling context\n");
    // 2. Update the calling context
    auto partition = analysis.get_calling_context_partition();
    // this data structure is propagated in the analyze_node function
    if (!partition.is_top() && !partition.is_bottom()) {
      printf("now try to update the calling context\n");
      for (const auto& entry : partition.bindings()) {
        auto insn = entry.first;
        auto calling_context = entry.second;
        std::cout << "instruction is " << show(insn) << std::endl;
        std::cout << "new calling context that needs update" << calling_context << std::endl;
        //  << std::endl;

        auto op = insn->opcode();
        always_assert(opcode::is_an_invoke(op));
        // get callees for this particular invoke instruction
        auto callees = call_graph::resolve_callees_in_graph(
            *this->get_call_graph(), m_method, insn);
        // TODO
        this->get_caller_context()->update(
              insn, [&](const determinism::CallingContext& original_context)
              {
                return calling_context.join(original_context);
              });
        // for (const DexMethod* method : callees) {
        //   this->get_caller_context()->update(
        //       insn, [&](const determinism::CallingContext& original_context)
        //       {
        //         return calling_context.join(original_context);
        //       });
        // }        
      }
    }
  }
  // this is simple because it only analyzes invoke instruction
  void analyze_insn(IRInstruction* insn) {
    // if (opcode::is_an_invoke(insn->opcode())) {
    //   analyze_invoke(insn);
    // }
  }
  DeterminismDomain get_return_value() { return m_domain; }

  void analyze_invoke(IRInstruction* insn) {
    auto callee = insn->get_method();
    auto callee_method =
        resolve_method(callee, opcode_to_search(insn), m_method);
    if (callee_method) {
      // DepthDomain::top() is the default value of the get function
      printf("method resovled%s\n", callee_method->get_name()->c_str());
      // auto summary =
      //     this->get_summaries()->get(callee_method, DepthDomain::top());
      // if (summary.is_value()) {
      //   m_domain.join_with(DepthDomain(summary.depth() + 1u));
      // } else {
      //   m_domain.join_with(summary);
      // }
    } else {
      printf("method didnot resovled%s\n", callee->get_name()->c_str());

      // m_domain.join_with(DepthDomain(1u));
    }
  }
  void summarize() override {
    printf("m_intraprocedural finish analyze(), now enter summarize()");
    if (!m_method) {
      std::cout << "empty method nullptr, does not update registry\n";
      return;
    }
    std::cout << m_method->get_name()->c_str()
              << " finish intra analyzer, now try to update the function "
                 "summary registry"
              << m_domain << std::endl;
    this->get_summaries()->maybe_update(m_method, [&](DeterminismDomain& old) {
      if (old == m_domain) {
        // no change will be made
        return false;
      }
      old = m_domain; // overwrite previous value
      return true;
    });
    // this->get_summaries()->update(
    //     m_method, [&](const DeterminismDomain&) { return m_domain; });
    // std::cout << "finish update the function summary registry" << std::endl;
    // TODO: Need additional program state information
  }
};

// The adaptor supplies the necessary typenames to the analyzer so that template
// instantiation assembles the different parts. It's also possible to override
// type aliases in the adaptor base class.
struct DeterminismAnalysisAdaptor : public BottomUpAnalysisAdaptorBase {
  // Registry is used to hold the summaries.
  // Provide typenames to the class InterproceduralAnalyzer, defined in
  // Analyzer.h
  // Internal data structure is: ConcurrentMap<const DexMethod*, Summary> m_map;
  using Registry = MethodSummaryRegistry<DeterminismDomain>;
  // track a method's domain
  // used to decide when the global fixpoint is reached

  using FunctionSummary = DeterminismDomain;
  // Here is what in Analyzer.h : using FunctionAnalyzer = typename
  // Analysis::template FunctionAnalyzer<Intraprocedural<Registry,
  // CallerContext, CallGraph, AnalysisParameters>>;
  template <typename IntraproceduralBase>
  using FunctionAnalyzer = DeterminismFunctionAnalyzer<IntraproceduralBase>;
  using Callsite = Caller;
  // callercontext is callsite::domain, which in our case is caller::domain,
  // which is sparta::HashedAbstractPartition<const IRInstruction*, ArgumentDomain>;
};

using Analysis =
    InterproceduralAnalyzer<DeterminismAnalysisAdaptor,
                            DeterminismAnalysisPass::AnalysisParameters>;
// declare Analysis as an alias for InterproceduralAnalyzer.
// InterproceduralAnalyzer will provide the typenames to the FunctionAnalyzer,
// and implicitly the IntraproceduralBase

} // namespace
void DeterminismAnalysisPass::run(const Scope& scope,
                                  unsigned m_max_iteration,
                                  AnalysisParameters param) {
  band_config_func_labels(m_function_labels, param);
  band_config_functionality(param);
  // field_op_tracker::FieldStatsMap field_stats = field_op_tracker::analyze(scope);
  DeterminismDomain is_det(DeterminismType::IS_DET);
  auto analysis = Analysis(scope, m_max_iteration, &param);
  analysis.run();
  std::string analysisOutputPath = getenv ("ANALYSIS_OUTPUT");
  std::string currentdatetime = getenv("current_date_time");
  std::unordered_map<std::string, Json::Value> final_result;
  // Json::Value vec_result(Json::arrayValue);
  std::cout << "now prepares print out anlaysis result to json\n";
  for (const auto& entry : analysis.registry.get_map()) {
    Json::Value method_entry;
    method_entry["name"] = show(entry.first);
    method_entry["lattice"] = entry.second.element();
    std::string class_name = ((entry.first)->get_class())->c_str();
    class_name.pop_back();
    std::string delimiter = "/";
    std::string abb_class_name(class_name.substr(class_name.rfind("/") + 1));
    // vec_result.append(method_entry);
    std::cout << "print analysis result" << show(entry.first) << " -> "
              << entry.second.element() << std::endl;
    auto got = final_result.find(abb_class_name);
    if (got == final_result.end()) {
      // first method of the class encountered
      Json::Value vec_result(Json::arrayValue);
      vec_result.append(method_entry);
      final_result.insert(make_pair(abb_class_name, vec_result));

    } else {
      got->second.append(method_entry);
    }
  }
  for (auto itr: final_result) {
    std::string outputJsonFile;
    if (strcmp(currentdatetime.c_str(), "") == 0) {
      outputJsonFile = analysisOutputPath + "/" + itr.first + "_" + "det.json";
    } else {
      outputJsonFile = analysisOutputPath + "/" + currentdatetime + "/" + itr.first + "_" + "det.json";
    }
    std::cout << "current date time is" << ";" << currentdatetime << ";"  << outputJsonFile << std::endl;
    remove(outputJsonFile.c_str());
    std::ofstream o(outputJsonFile, std::ios_base::app);
    std::cout << outputJsonFile << std::endl;
    o << std::setw(4) << itr.second << std::endl;
  }

}

void DeterminismAnalysisPass::run_pass(DexStoresVector& stores,
                                       ConfigFiles& /* conf */,
                                       PassManager& /* pm */) {

  TRACE(MDA, 1,
        "[return max depth analysis config debug: target function name file] "
        "%s",
        SHOW(m_target_functions));

  // read m_target_functions file
  // std::vector<std::vector<std::string>> content;
  // std::vector<std::string> row;
  // std::string line, word;

  // std::fstream file(m_target_functions, std::ios::in);
  // if (file.is_open()) {
  //   while (getline(file, line)) {
  //     row.clear();

  //     std::stringstream str(line);

  //     while (getline(str, word, ','))
  //       row.push_back(word);
  //     content.push_back(row);
  //   }
  // } else
  //   TRACE(MDA, 1, "[return max depth analysis] could not open file %s",
  //         SHOW(m_target_functions));
  // for (int i = 0; i < content.size(); i++) {
  //   for (int j = 0; j < content[i].size(); j++) {
  //     TRACE(MDA, 1, "%s\t",
  //           SHOW(content[i][j]));
  //   }

  // }
  std::vector<std::string> content;
  std::string line, word;

  std::fstream file(m_target_functions, std::ios::in);
  if (file.is_open()) {
    while (getline(file, line)) {
      std::stringstream str(line);
      while (getline(str, word, ','))
        content.push_back(word);
    }
  } else
    TRACE(MDA, 1, "[return max depth analysis] could not open file %s",
          SHOW(m_target_functions));
  for (int i = 0; i < content.size(); i++) {
    TRACE(MDA, 1, "%s\t", SHOW(content[i]));
  }
  TRACE(MDA, 1, "[return max depth analysis]  %s\n",
        "target function read done");
  AnalysisParameters param;
  for (int i = 0; i < content.size(); i++) {
    printf("add target function name %s\n", content[i].c_str());
    param.function_names.push_back(content[i]);
    vectors_m_target_functions.push_back(content[i]);
  }
  for (auto it : stores) {
    // printf("%s\n",it.get_name());
    // const std::vector<DexClasses> classes = it.get_dexen();
    // for (auto itc: classes) {
    //   for (auto itcc: itc) {
    //     printf("%s\n", itcc->get_name()->c_str());
    //     std::vector<DexMethod*> all_methods = itcc->get_all_methods();
    //     for (auto itccm: all_methods) {
    //       printf("%s\n", itccm->get_name()->c_str());
    //     }
    //   }

    // }
    // printf("done with the previous store\n");
  }
  // using Scope = std::vector<DexClass*>;
  Scope analyze_scope = build_class_scope(stores);
  run(analyze_scope, m_max_iteration, param);
}

static DeterminismAnalysisPass s_pass;
