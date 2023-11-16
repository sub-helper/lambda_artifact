/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "DeterminismAnalysisIntra.h"
#include "DexClass.h"
#include "Pass.h"
#include <fstream>
#include <iostream>
class DeterminismAnalysisPass : public Pass {
 public:
  struct AnalysisParameters {
    // For analyzing a subset of functions
    std::vector<std::string> function_names;
    std::unordered_map<std::string, determinism::DeterminismDomain>
        func_domain_map;
    std::unordered_set<std::string> func_reset_det_set;
    bool track_exception = false;
  };
  DeterminismAnalysisPass() : Pass("DeterminismAnalysisPass", Pass::ANALYSIS) {}
  void bind_config() override {
    bind("max_iteration", 10U, m_max_iteration);
    bind("m_func_lables", "", m_function_labels);
    bind("track_exception", false, m_track_exception);

    // printf("print banding %s", m_func_name.c_str());
  }
  void band_config_functionality(AnalysisParameters& param) {
    if (m_track_exception) {
      std::cout << "track exception flag is on\n";
      param.track_exception = true;
    }
  }
  void band_config_func_labels(std::string filename,
                               AnalysisParameters& param) {
    // m_function_labels = filename;
    std::vector<std::string> content;
    std::string line, word;

    std::fstream file(filename, std::ios::in);
    if (file.is_open()) {
      while (getline(file, line)) {
        std::stringstream str(line);
        while (getline(str, word, ','))
          content.push_back(word);
      }
    }
    always_assert_log(content.size() % 2 == 0,
                      "the format of m_function_labels is not correct\n");
    for (int i = 0; i < content.size(); i = i + 2) {
      std::string name = content[i];
      std::string label = content[i + 1];
      if (!check_valid_label(label)) {
        if (!label.compare("FORCEDET")) {
          m_func_reset_det_set.insert(name);
        } else {
          not_reached_log("check the invalid label %s\n", label);
        }
      }
      if (m_func_domain_map.count(name) == 0) {
        printf("add funcname to m_func_domain_map");
        m_func_domain_map[name] = return_det_domain(label);
        std::cout << name << "\t" << label << std::endl;

      } else {
        not_reached_log("duplicate function name %s\n", name);
      }
    }
    printf("finish populating m_func_domain_map size %d",
           m_func_domain_map.size());
    param.func_domain_map = m_func_domain_map;
    param.func_reset_det_set = m_func_reset_det_set;
  }
  void run_pass(DexStoresVector&, ConfigFiles&, PassManager&) override;
  // this ise useful for writing unit tests
  void run(DexMethod* method, XStoreRefs* xstores);
  void run(const Scope& scope,
           unsigned m_max_iteration,
           AnalysisParameters param);
  using Result =
      std::unordered_map<const DexMethod*, determinism::DeterminismDomain>;

  std::shared_ptr<Result> get_result() { return m_result; }

  void destroy_analysis_result() override { m_result = nullptr; }

 private:
  unsigned m_max_iteration;
  std::string m_target_functions;
  bool m_track_exception;
  std::string m_function_labels;
  std::unordered_set<std::string> m_func_reset_det_set;

  std::vector<std::string> vectors_m_target_functions;

  std::shared_ptr<Result> m_result = nullptr;
  std::unordered_map<std::string, determinism::DeterminismDomain>
      m_func_domain_map;
  bool check_valid_label(std::string label) {
    std::set<std::string> valid_labels = {"DET", "TOP", "NOTDET"};
    if (valid_labels.count(label) == 1) {
      return true;
    } else {
      return false;
    }
  }
  determinism::DeterminismDomain return_det_domain(std::string label) {
    if (!label.compare("DET")) {
      determinism::DeterminismDomain obj(DeterminismType::IS_DET);
      return obj;
    } else if (!label.compare("TOP")) {
      determinism::DeterminismDomain obj(DeterminismType::DT_TOP);
      return obj;
    } else {
      determinism::DeterminismDomain obj(DeterminismType::NOT_DET);
      return obj;
    }
  }
};
