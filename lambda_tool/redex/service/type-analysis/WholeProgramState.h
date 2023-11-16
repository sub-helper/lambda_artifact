/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/optional/optional_io.hpp>

#include "DexTypeEnvironment.h"
#include "HashedAbstractPartition.h"
#include "InstructionAnalyzer.h"
#include "Trace.h"
#include <iostream>
#include<json/writer.h>
#include <cstdio>
#include <fstream>
#include <sstream>
std::ostream& operator<<(std::ostream& out, const DexField* field);

std::ostream& operator<<(std::ostream& out, const DexMethod* method);

namespace type_analyzer {

namespace global {

class GlobalTypeAnalyzer;

} // namespace global

using DexTypeFieldPartition =
    sparta::HashedAbstractPartition<const DexField*, DexTypeDomain>;

using DexTypeMethodPartition =
    sparta::HashedAbstractPartition<const DexMethod*, DexTypeDomain>;

class WholeProgramState {
 public:
  // By default, the field and method partitions are initialized to Bottom.
  WholeProgramState() = default;

  WholeProgramState(const Scope&,
                    const global::GlobalTypeAnalyzer&,
                    const std::unordered_set<DexMethod*>& non_true_virtuals,
                    const ConcurrentSet<const DexMethod*>& any_init_reachables);

  void set_to_top() {
    m_field_partition.set_to_top();
    m_method_partition.set_to_top();
  }

  bool leq(const WholeProgramState& other) const {
    return m_field_partition.leq(other.m_field_partition) &&
           m_method_partition.leq(other.m_method_partition);
  }

  /*
   * Returns our best approximation of the field type.
   * For unknown fields or fields with no type mapping, we simply return top.
   * It will never return Bottom.
   */
  DexTypeDomain get_field_type(const DexField* field) const {
    if (!m_known_fields.count(field)) {
      return DexTypeDomain::top();
    }
    auto domain = m_field_partition.get(field);
    if (domain.is_bottom()) {
      TRACE(TYPE, 5, "Missing type for field %s", show_field(field).c_str());
      return DexTypeDomain::top();
    }
    return domain;
  }

  /*
   * Returns our best static approximation of the return type.
   * For unknown methods, we simply return top.
   * A method that maps to Bottom indicates that a method never returns (i.e. it
   * throws or loops indefinitely). However, for now we still return top. We
   * don't want to propagate Bottom to local analysis.
   */
  DexTypeDomain get_return_type(const DexMethod* method) const {
    if (!m_known_methods.count(method)) {
      return DexTypeDomain::top();
    }
    auto domain = m_method_partition.get(method);
    if (domain.is_bottom()) {
      TRACE(TYPE, 5, "Missing type for method %s", show_method(method).c_str());
      return DexTypeDomain::top();
    }
    return domain;
  }

  size_t get_num_resolved_fields() {
    size_t cnt = 0;
    for (auto& pair : m_field_partition.bindings()) {
      if (!pair.second.is_top()) {
        ++cnt;
      }
    }
    return cnt;
  }

  size_t get_num_resolved_methods() {
    size_t cnt = 0;
    for (auto& pair : m_method_partition.bindings()) {
      if (!pair.second.is_top()) {
        ++cnt;
      }
    }
    return cnt;
  }

  bool is_any_init_reachable(const DexMethod* method) const {
    return m_any_init_reachables.count(method);
  }

  /*
   * The nullness results is only guaranteed to be correct after the execution
   * of clinit and ctors.
   * TODO: The complete solution requires some kind of call graph analysis from
   * the clinit and ctor.
   */
  bool can_use_nullness_results(const DexMethod* method) const {
    return !method::is_init(method) && !method::is_clinit(method) &&
           !is_any_init_reachable(method);
  }

  // For debugging
  std::string print_field_partition_diff(const WholeProgramState& other) const;

  std::string print_method_partition_diff(const WholeProgramState& other) const;

  friend std::ostream& operator<<(std::ostream& out,
                                  const WholeProgramState& wps) {
    out << wps.m_field_partition << std::endl;
    out << wps.m_method_partition;
    return out;
  }
  boost::optional<DexTypeDomain> get_type_for_method_with_known_type(
      const DexMethodRef* method) const {
    if (m_known_method_returns.find(method) != m_known_method_returns.end()) {
      return m_known_method_returns.find(method)->second;
    }
    return boost::none;
  }
  void print_analysis_result() {
    std::cout << "print analysis result";

    // for (auto it : m_known_method_returns) {
    //   std::cout << it.first->get_name()->c_str() << "->";
    //   auto type_result = it.second;
    //   if (type_result.is_not_null()) {
    //     std::cout << "not null\n";
    //   } else if (type_result.is_null()) {
    //     std::cout << "is null\n";
    //   } else if (type_result.is_nullable()) {
    //     std::cout << "is nullable\n";
    //   }
    // }
    
  }

 private:
  void analyze_clinits_and_ctors(const Scope&,
                                 const global::GlobalTypeAnalyzer&,
                                 DexTypeFieldPartition*);
  void setup_known_method_returns();

  void collect(const Scope& scope, const global::GlobalTypeAnalyzer&);

  void collect_field_types(
      const IRInstruction* insn,
      const DexTypeEnvironment& env,
      ConcurrentMap<const DexField*, std::vector<DexTypeDomain>>* field_tmp);

  bool collect_return_types(
      const IRInstruction* insn,
      const DexTypeEnvironment& env,
      const DexMethod* method,
      ConcurrentMap<const DexMethod*, std::vector<DexTypeDomain>>* method_tmp);

  bool is_reachable(const global::GlobalTypeAnalyzer&, const DexMethod*) const;

  // To avoid "Show.h" in the header.
  static std::string show_field(const DexField* f);
  static std::string show_method(const DexMethod* m);

  // Track the set of fields that we can correctly analyze.
  // The unknown fields can be written to by non-dex code or through reflection.
  // We currently do not have the infrastructure to analyze these cases
  // correctly.
  std::unordered_set<const DexField*> m_known_fields;
  // Unknown methods will be treated as containing / returning Top.
  std::unordered_set<const DexMethod*> m_known_methods;
  // Methods reachable from clinit that read static fields and reachable from
  // ctors that raed instance fields.
  ConcurrentSet<const DexMethod*> m_any_init_reachables;

  DexTypeFieldPartition m_field_partition;
  DexTypeMethodPartition m_method_partition;
  std::unordered_map<const DexMethodRef*, DexTypeDomain> m_known_method_returns;
};

class WholeProgramAwareAnalyzer final
    : public InstructionAnalyzerBase<WholeProgramAwareAnalyzer,
                                     DexTypeEnvironment,
                                     const WholeProgramState*> {

 public:
  static bool analyze_iget(const WholeProgramState* whole_program_state,
                           const IRInstruction* insn,
                           DexTypeEnvironment* env);

  static bool analyze_sget(const WholeProgramState* whole_program_state,
                           const IRInstruction* insn,
                           DexTypeEnvironment* env);

  static bool analyze_invoke(const WholeProgramState* whole_program_state,
                             const IRInstruction* insn,
                             DexTypeEnvironment* env);
};

} // namespace type_analyzer
