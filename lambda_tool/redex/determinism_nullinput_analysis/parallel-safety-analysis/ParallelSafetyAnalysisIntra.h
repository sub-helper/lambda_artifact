/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <ostream>

#include "BaseIRAnalyzer.h"
#include "FiniteAbstractDomain.h"
#include <boost/optional.hpp>
#include <utility>

// #include "AbstractDomain.h"
// #include "DeterminismDomain.h"
#include "DexClass.h"
#include "DexUtil.h"
#include "HashedAbstractPartition.h"
#include "HashedSetAbstractDomain.h"
#include "IRInstruction.h"
#include "PatriciaTreeMapAbstractEnvironment.h"
#include "PatriciaTreeMapAbstractPartition.h"
#include "DexClass.h"
#include "ReducedProductAbstractDomain.h"
#include <iostream>

using namespace sparta;

struct IntraAnalyzerParameters {
    bool track_exception = false;
};  
enum DeterminismType { DT_BOTTOM, IS_DET, NOT_DET, DT_TOP };
// label whether a basic block comes from an exception block
enum ExceptType { EX_BOTTOM, UNVISIT, FROM_EX, NO_EX, EX_TOP };

std::ostream& operator<<(std::ostream& output, const DeterminismType& det);
std::ostream& operator<<(std::ostream& output, const ExceptType& det);


namespace parallelsafe {
enum class FieldType { INSTANCE, STATIC };

using std::placeholders::_1;

using DetLattice = sparta::BitVectorLattice<DeterminismType, 4, std::hash<int>>;
extern DetLattice det_lattice;
using ExpLattice = sparta::BitVectorLattice<ExceptType, 5, std::hash<int>>;
extern ExpLattice exp_lattice;
using DeterminismDomain = sparta::FiniteAbstractDomain<DeterminismType,
                                                       DetLattice,
                                                       DetLattice::Encoding,
                                                       &det_lattice>;
using ExpDomain = sparta::FiniteAbstractDomain<ExceptType,
                                            ExpLattice,
                                            ExpLattice::Encoding,
                                            &exp_lattice>;

std::ostream& operator<<(std::ostream& output,
                         const DeterminismDomain& detDomain);
std::ostream& operator<<(std::ostream& output,
                         const ExpDomain& expDomain);

// these two classes are exactly the same as the typenames defiend in the Caller struct in ParallelSafetyAnalysis.cpp
using CallingContext =
    sparta::PatriciaTreeMapAbstractPartition<param_index_t,
                                             DeterminismDomain>;
// std::ostream& operator<<(std::ostream& output,
//                          const CallingContext& cc);
// Maps from callsite instruction to the corresponding calling context.
using CallingContextMap =
    sparta::PatriciaTreeMapAbstractEnvironment<const IRInstruction*,
                                               CallingContext>;

// Declare a function type that summarizes the callees' analysis result
using SummaryQueryFn =
    std::function<DeterminismDomain(const IRInstruction*)>;

using DetFieldPartition =
    sparta::PatriciaTreeMapAbstractEnvironment<DexField*, DeterminismDomain>;
// This program state is block-wise, and tracks a set of block that has an
// exception handling block as its ancestor.
// NodeId = Block*;
using ExpBlockPartition = std::unordered_set<cfg::GraphInterface::NodeId>;


// using ClassObjectSourceDomain =
//     sparta::ConstantAbstractDomain<ClassObjectSource>;

using BasicAbstractObjectEnvironment = DeterminismDomain;

// using ClassObjectSourceEnvironment =
//     PatriciaTreeMapAbstractEnvironment<reg_t, DeterminismDomain>;

// using HeapClassArrayEnvironment = PatriciaTreeMapAbstractEnvironment<
//     AbstractHeapAddress,
//     ConstantAbstractDomain<std::vector<DexType*>>>;

using ReturnValueDomain = DeterminismDomain;
using ExpBlockDomain = ExpDomain;
// using DetFieldPartition =
//     PatriciaTreeMapAbstractEnvironment<const DexField*, DeterminismDomain>;
class AbstractObjectEnvironment final
    : public ReducedProductAbstractDomain<AbstractObjectEnvironment,
                                          BasicAbstractObjectEnvironment,
                                          ReturnValueDomain> {
 public:
  using ReducedProductAbstractDomain::ReducedProductAbstractDomain;

  static void reduce_product(std::tuple<BasicAbstractObjectEnvironment,
                                        ReturnValueDomain>& /* product */) {}

  DeterminismDomain get_abstract_obj() const {
    // get the abstract value of the register
    return get<0>();
  }
  void join_abstract_obj(const ReturnValueDomain& domain) {
    apply<0>([=](auto original) { original->join_with(domain); }, true);
  }
  void set_abstract_obj(const DeterminismDomain aobj) {
    // set the abstract value of the register
    apply<0>([=](auto original) { original->set_to_top(); original->meet_with(aobj);}, true);
  }


  ReturnValueDomain get_return_value() const { return get<0>(); }

  void join_return_value(const ReturnValueDomain& domain) {
    apply<1>([=](auto original) { original->join_with(domain); }, true);
  }
  void set_return_value(const ReturnValueDomain& domain) {
    apply<1>([=](auto original) { original->set_to_top(); original->meet_with(domain);}, true);
  }

};
namespace impl {

// Forward declarations.
class Analyzer;

} // namespace impl

using namespace ir_analyzer;



class ParallelSafetyAnalysis final {
 public:
  // If we don't declare a destructor for this class, a default destructor will
  // be generated by the compiler, which requires a complete definition of
  // sra_impl::Analyzer, thus causing a compilation error. Note that the
  // destructor's definition must be located after the definition of
  // sra_impl::Analyzer.
  ~ParallelSafetyAnalysis();

  explicit ParallelSafetyAnalysis(DexMethod* dex_method, IntraAnalyzerParameters ap, CallingContext* context = nullptr, SummaryQueryFn* summary_query_fn = nullptr,
                              std::unordered_set<std::string>* reset_det_func = nullptr);


  DeterminismDomain get_return_value() const;

  /**
   * Return a parameter type array for this invoke method instruction.
   */
  boost::optional<std::vector<DexType*>> get_method_params(
      IRInstruction* invoke_insn) const;
  /*
   * Returns the abstract object (if any) referenced by the register at the
   * given instruction. Note that if the instruction overwrites the register,
   * the abstract object returned is the value held by the register *before*
   * that instruction is executed.
   */
//   boost::optional<DeterminismDomain> get_abstract_object(
//       size_t reg, IRInstruction* insn) const;

//   boost::optional<ClassObjectSource> get_class_source(
//       size_t reg, IRInstruction* insn) const;

  CallingContextMap get_calling_context_partition() const;

 private:
  const DexMethod* m_dex_method;
  // This is the actual class that performs the analysis over the CFG
  // class Analyzer final : public BaseIRAnalyzer<AbstractObjectEnvironment> 
  // class BaseIRAnalyzer : public sparta::MonotonicFixpointIterator<cfg::GraphInterface, Domain> {
  std::unique_ptr<impl::Analyzer> m_analyzer; 
  std::unordered_set<std::string>* reset_det_func = nullptr;
  IntraAnalyzerParameters m_ap;
  void debug_context(CallingContext* context); 
};

} // namespace parallelsafe
