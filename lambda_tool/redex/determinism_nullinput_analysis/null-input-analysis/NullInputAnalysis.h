/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include "NullInputAnalysisIntra.h"
#include "NullInputAnalysis.h"
#include "DexClass.h"
#include "Pass.h"
#include <fstream>
#include <iostream>
class NullInputAnalysisPass : public Pass {
 public:
  struct AnalysisParameters {
    // For analyzing a subset of functions
    std::vector<std::string> function_names;
  };
  NullInputAnalysisPass() : Pass("NullInputAnalysisPass", Pass::ANALYSIS) {}
  void bind_config() override {
    bind("max_iteration", 10U, m_max_iteration);
    // bind("track_exception", false, m_track_exception);

    // printf("print banding %s", m_func_name.c_str());
  }
  void band_config_functionality(AnalysisParameters& param) {
    // if (m_track_exception) {
    //   std::cout << "track exception flag is on\n";
    //   param.track_exception = true;
    // }
  }
  
  void run_pass(DexStoresVector&, ConfigFiles&, PassManager&) override;
  // this ise useful for writing unit tests
  void run(DexMethod* method, XStoreRefs* xstores);
  void run(const Scope& scope,
           unsigned m_max_iteration,
           AnalysisParameters param);
  using Result =
      std::unordered_map<const DexMethod*, nullinput::NullInputDomain>;

  std::shared_ptr<Result> get_result() { return m_result; }

  void destroy_analysis_result() override { m_result = nullptr; }

 private:
  unsigned m_max_iteration;

  std::vector<std::string> vectors_m_target_functions;

  std::shared_ptr<Result> m_result = nullptr;



};
