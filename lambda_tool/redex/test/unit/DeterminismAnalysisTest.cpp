/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "DeterminismAnalysis.h"

#include "CallGraph.h"
#include "Creators.h"
#include "IRAssembler.h"
#include "MethodOverrideGraph.h"
#include "RedexTest.h"
#include "Walkers.h"

using namespace determinism;

struct DeterminismAnalysisTest : public RedexTest {
 public:
  DeterminismAnalysisTest() {
    auto cls_o = DexType::make_type("LO;");
    ClassCreator creator(cls_o);
    creator.set_super(type::java_lang_Object());

    auto m_init = assembler::method_from_string(R"(
      (method (public constructor) "LO;.<init>:()V"
       (
        (return-void)
       )
      )
    )");
    creator.add_method(m_init);
    m_cls_o = creator.create();
  }

 protected:
  void prepare_scope(Scope& scope) { scope.push_back(m_cls_o); }
  void run_opt(Scope& scope,
               DeterminismDomain expect_domain,
               std::string function_name) {
    DeterminismAnalysisPass::AnalysisParameters param;
    DeterminismAnalysisPass analysis;
    analysis.band_config_func_labels(
        "/home/pandahome/Research/lambda_tool/redex/config/function_labels.csv",
        param);
    analysis.run(scope, 10, param);
    auto results = analysis.get_result();
    auto got = results->find(function_name);
    if (got == results->end()) {
      always_assert_log("does not find analysis result for the name %s",
                        function_name.c_str());
    } else {
      std::cout << got->first << " found is " << got->second;
      EXPECT_TRUE(expect_domain.equals(got->second));
    }

    // printf(analysis.get_result())
  }

  DexClass* m_cls_arg;
  DexClass* m_cls_o;
  DexMethod* m_method_call;
  DeterminismDomain is_det() {
    DeterminismDomain d(DeterminismType::IS_DET);
    return d;
  }
};

TEST_F(DeterminismAnalysisTest, RandTest) {
  // The expected result is don't know
  Scope scope;
  prepare_scope(scope);

  auto cls_a = DexType::make_type("LA;");

  ClassCreator creator(cls_a);

  creator.set_super(type::java_lang_Object());
  // Todo: method (public static) "LA;.bar:(LO;)LO will fail
  auto meth_bar = assembler::method_from_string(R"(
    (method (public static) "LA;.RAND:(Long;)V"
     (
      (load-param-object v3)
      (if-nez v3 :true)
      (const v0 0)
      (return-object v0)

      (:true)
      (new-instance "LRandom;")
      (move-result-pseudo-object v0)
      (invoke-virtual (v3) "Ljava/lang/Long;.longValue:()J")
      (move-result-wide v1)
      (invoke-direct (v0 v1) "Ljava/util/Random;.<init>:(J)V")
      (invoke-virtual (v0) "Ljava/util/Random;.nextDouble:()D")
      (move-result-wide v0)
      (invoke-static (v0) "Ljava/lang/Double;.valueOf:(D)Ljava/lang/Double")
      (move-result-object v0)
      (return-object v0)
      
     )
    )
  )");
  creator.add_method(meth_bar);
  printf("finish setting up method\n");
  meth_bar->rstate.set_root();
  // creator.add_method(meth_foo);
  scope.push_back(creator.create());
  printf("now run analysis\n");

  run_opt(scope, DeterminismDomain::top(), "RAND");
}
TEST_F(DeterminismAnalysisTest, IfTest) {
  Scope scope;
  prepare_scope(scope);

  auto cls_a = DexType::make_type("LA;");

  ClassCreator creator(cls_a);

  creator.set_super(type::java_lang_Object());
  // Todo: method (public static) "LA;.bar:(LO;)LO will fail
  auto meth_bar = assembler::method_from_string(R"(
    (method (public static) "LA;.IF:(ZL;L;)V"
     (
      (load-param v1)
      (load-param-object v2)
      (load-param-object v3)
      (if-eqz v1 :true)
      (invoke-virtual (v1) "Ljava/lang/Boolean;.booleanValue:()Z")
      (move-result v0)
      (if-eqz v0 :true)

      (move-object v0 v2)
      (goto :true2)
      (:true)
      (move-object v0 v3)
      (goto :true2)
      (:true2)
      (return-object v0)
     )
    )
  )");
  creator.add_method(meth_bar);
  printf("finish setting up method\n");
  meth_bar->rstate.set_root();
  // creator.add_method(meth_foo);
  scope.push_back(creator.create());
  printf("now run analysis\n");

  run_opt(scope, is_det(), "IF");
}
TEST_F(DeterminismAnalysisTest, CoalesceTest) {
  // COALESCE function in buildinfunctions.dex
  // The expected analysis outcome is that the return value is deterministic
  // The analyzed langauge construct is array index, add operation, comparison
  Scope scope;
  prepare_scope(scope);

  auto cls_a = DexType::make_type("LA;");
  ClassCreator creator(cls_a);
  creator.set_super(type::java_lang_Object());
  // Todo: method (public static) "LA;.bar:(LO;)LO will fail
  auto meth_bar = assembler::method_from_string(R"(
    (method (public static) "LA;.Coalesce:(LO;)V"
     (
      (load-param-object v2)

      (const v0 0)
      (:L1)
      (array-length v2)
      (move-result-pseudo v1)

      (if-ge v0 v1 :true)
      (aget-object v2 v0)
      (move-result-pseudo v1)
      (if-eqz v1 :true2)
      (aget-object v2 v0)
      (move-result-pseudo v1)
      (return-object v1)
      (:true2)
      (add-int/lit8 v0 v0 1) 
      (goto :L1)
      (:true)
      (const v0 0)
      (return-object v0)
     )
    )
  )");
  creator.add_method(meth_bar);

  // auto meth_foo = assembler::method_from_string(R"(
  //   (method (public static) "LA;.foo:()V"
  //    (
  //     (new-instance "LO;")
  //     (move-result-pseudo-object v0)
  //     (invoke-direct (v0) "LO;.<init>:()V")
  //     (invoke-static (v0) "LA;.bar:(LO;)V")
  //     (return-void)
  //    )
  //   )
  // )");
  printf("finish setting up method\n");
  meth_bar->rstate.set_root();
  // creator.add_method(meth_foo);
  scope.push_back(creator.create());
  call_graph::Graph cg = call_graph::single_callee_graph(
      *method_override_graph::build_graph(scope), scope);
  walk::code(scope, [](DexMethod*, IRCode& code) {
    code.build_cfg(/* editable */ false);
  });
  run_opt(scope, is_det(), "Coalesce");
  //   GlobalTypeAnalyzer gta(cg);
  //   gta.run({{CURRENT_PARTITION_LABEL, ArgumentTypeEnvironment()}});

  //   auto& graph = gta.get_call_graph();

  //   auto foo_arg_env =
  //       gta.get_entry_state_at(graph.node(meth_foo)).get(CURRENT_PARTITION_LABEL);
  //   EXPECT_TRUE(foo_arg_env.is_top());
  //   auto bar_arg_env =
  //       gta.get_entry_state_at(graph.node(meth_bar)).get(CURRENT_PARTITION_LABEL);
  //   EXPECT_EQ(bar_arg_env,
  //             ArgumentTypeEnvironment({{0, get_type_domain("LO;")}}));
}
