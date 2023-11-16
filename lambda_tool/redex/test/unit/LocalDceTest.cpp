/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include "DexAsm.h"
#include "DexUtil.h"
#include "IRAssembler.h"
#include "IRCode.h"
#include "InstructionLowering.h"
#include "LocalDcePass.h"
#include "MethodOverrideGraph.h"
#include "Purity.h"
#include "RedexTest.h"
#include "ScopeHelper.h"
#include "Show.h"
#include "VirtualScope.h"

struct LocalDceTryTest : public RedexTest {
  DexMethod* m_method;

  LocalDceTryTest() {
    // Calling get_vmethods under the hood initializes the object-class, which
    // we need in the tests to create a proper scope
    get_vmethods(type::java_lang_Object());

    auto args = DexTypeList::make_type_list({});
    auto proto = DexProto::make_proto(type::_void(), args);
    m_method =
        DexMethod::make_method(type::java_lang_Object(),
                               DexString::make_string("testMethod"), proto)
            ->make_concrete(ACC_PUBLIC | ACC_STATIC, false);
    m_method->set_code(std::make_unique<IRCode>(m_method, 1));
  }

  ~LocalDceTryTest() {}
};

// We used to wrongly delete try items when just one of the the TRY_START /
// TRY_END markers was inside an unreachable block. We would remove both
// markers even though it was still bracketing live code. This test
// checks to see that we preserve the TRY markers while removing the
// relevant dead code.
TEST_F(LocalDceTryTest, deadCodeAfterTry) {
  // setup
  using namespace dex_asm;

  auto code = m_method->get_code();
  auto exception_type = DexType::make_type("Ljava/lang/Exception;");
  auto catch_start = new MethodItemEntry(exception_type);

  auto goto_mie = new MethodItemEntry(dasm(OPCODE_GOTO));
  auto target = new BranchTarget(goto_mie);

  code->push_back(target);
  // this TRY_START is in a block that is live
  code->push_back(TRY_START, catch_start);
  // this invoke will be considered live code by the dce analysis
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(*goto_mie);
  // this TRY_END is in a block that is dead code
  code->push_back(TRY_END, catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(*catch_start);
  code->push_back(dasm(OPCODE_RETURN_VOID));
  code->set_registers_size(0);

  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  instruction_lowering::lower(m_method);
  m_method->sync();

  // check that the dead const/16 opcode is removed, but that the try item
  // is preserved
  EXPECT_EQ(m_method->get_dex_code()->get_instructions().size(), 3);
  EXPECT_EQ(m_method->get_dex_code()->get_tries().size(), 1);
}

// Check that we correctly delete try blocks if all the code they are
// bracketing is unreachable
TEST_F(LocalDceTryTest, unreachableTry) {
  // setup
  using namespace dex_asm;

  auto code = m_method->get_code();
  auto exception_type = DexType::make_type("Ljava/lang/Exception;");
  auto catch_start = new MethodItemEntry(exception_type);

  auto goto_mie = new MethodItemEntry(dasm(OPCODE_GOTO));
  auto target = new BranchTarget(goto_mie);

  code->push_back(target);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(*goto_mie);
  // everything onwards is unreachable code because of the goto

  code->push_back(TRY_START, catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(TRY_END, catch_start);
  code->push_back(*catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->set_registers_size(0);

  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  instruction_lowering::lower(m_method);
  m_method->sync();

  EXPECT_EQ(m_method->get_dex_code()->get_instructions().size(), 2);
  EXPECT_EQ(m_method->get_dex_code()->get_tries().size(), 0);
}

/*
 * Check that if a try block contains no throwing opcodes, we remove it
 * entirely, as well as the catch that it was supposed to throw to.
 *
 * Note that if a catch block at the end of a method is removed, it is
 * necessary to remove any tries that formerly targeted it, as catch target
 * offsets that point beyond the end of a method are a verification error.
 */
TEST_F(LocalDceTryTest, deadCatch) {
  // setup
  using namespace dex_asm;

  auto code = m_method->get_code();
  auto exception_type = DexType::make_type("Ljava/lang/Exception;");
  auto catch_start = new MethodItemEntry(exception_type);

  code->push_back(TRY_START, catch_start);
  code->push_back(dasm(OPCODE_RETURN_VOID));
  code->push_back(TRY_END, catch_start);
  code->push_back(*catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->set_registers_size(0);

  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  instruction_lowering::lower(m_method);
  m_method->sync();

  EXPECT_EQ(m_method->get_dex_code()->get_instructions().size(), 1);
  EXPECT_EQ(m_method->get_dex_code()->get_tries().size(), 0);
}

/*
 * Check that if a try block contains no throwing opcodes, we remove it
 * entirely, even if there are other blocks keeping its target catch live.
 */
TEST_F(LocalDceTryTest, tryNeverThrows) {
  // setup
  using namespace dex_asm;

  auto code = m_method->get_code();
  auto exception_type = DexType::make_type("Ljava/lang/Exception;");
  auto catch_start = new MethodItemEntry(exception_type);

  // this try wraps an opcode which may throw, should not be removed
  code->push_back(TRY_START, catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(TRY_END, catch_start);
  // this one doesn't wrap a may-throw opcode
  code->push_back(TRY_START, catch_start);
  code->push_back(dasm(OPCODE_CONST, {0_v, 0_L}));
  code->push_back(TRY_END, catch_start);
  code->push_back(dasm(OPCODE_INVOKE_STATIC, m_method, {}));
  code->push_back(*catch_start);
  code->push_back(dasm(OPCODE_RETURN_VOID));
  code->set_registers_size(1);

  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  instruction_lowering::lower(m_method);
  m_method->sync();

  EXPECT_EQ(m_method->get_dex_code()->get_instructions().size(), 3);
  EXPECT_EQ(m_method->get_dex_code()->get_tries().size(), 1);
}

TEST_F(LocalDceTryTest, deadIf) {
  // setup
  using namespace dex_asm;

  auto if_mie = new MethodItemEntry(dasm(OPCODE_IF_EQZ, {0_v}));
  auto target1 = new BranchTarget(if_mie);
  IRCode* code = m_method->get_code();
  code->push_back(*if_mie); // branch to target1
  code->push_back(target1);
  code->push_back(dasm(OPCODE_RETURN_VOID));
  code->set_registers_size(1);

  fprintf(stderr, "BEFORE:\n%s\n", SHOW(code));
  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  auto has_if =
      std::find_if(code->begin(), code->end(), [if_mie](MethodItemEntry& mie) {
        return &mie == if_mie;
      }) != code->end();

  // the if should be gone
  EXPECT_FALSE(has_if);
}

TEST_F(LocalDceTryTest, deadCast) {
  // setup
  using namespace dex_asm;

  auto check_cast_mie = new MethodItemEntry(
      dasm(OPCODE_CHECK_CAST, DexType::make_type("Ljava/lang/Void;"), {0_v}));
  IRCode* code = m_method->get_code();
  code->push_back(dasm(OPCODE_CONST, {0_v, 0_L}));
  code->push_back(*check_cast_mie); // branch to target1
  code->push_back(dasm(IOPCODE_MOVE_RESULT_PSEUDO_OBJECT, {0_v}));
  code->push_back(dasm(OPCODE_RETURN_VOID));
  code->set_registers_size(1);

  fprintf(stderr, "BEFORE:\n%s\n", SHOW(code));
  std::unordered_set<DexMethodRef*> pure_methods;
  LocalDce(pure_methods).dce(code);
  auto has_check_cast = std::find_if(code->begin(), code->end(),
                                     [check_cast_mie](MethodItemEntry& mie) {
                                       return &mie == check_cast_mie;
                                     }) != code->end();

  // the if should be gone
  EXPECT_FALSE(has_check_cast);
}

struct LocalDceEnhanceTest : public RedexTest {};

static std::unordered_set<DexMethodRef*> get_no_side_effect_methods(
    const Scope& scope) {
  std::unordered_set<DexMethodRef*> pure_methods;
  auto override_graph = method_override_graph::build_graph(scope);
  std::unordered_set<const DexMethod*> computed_no_side_effects_methods;
  compute_no_side_effects_methods(scope, override_graph.get(), pure_methods,
                                  &computed_no_side_effects_methods);
  for (auto m : computed_no_side_effects_methods) {
    pure_methods.insert(const_cast<DexMethod*>(m));
  }
  return pure_methods;
}

TEST_F(LocalDceEnhanceTest, NoImplementorIntfTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));

  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_INTERFACE);
  create_abstract_method(a_cls, "m", void_void);

  scope.push_back(a_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, HaveImplementorWithoutSideEffectsTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));

  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_ABSTRACT);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls = create_internal_class(b_type, a_type, {});

  DexType* c_type = DexType::make_type("LC;");
  DexClass* c_cls = create_internal_class(c_type, b_type, {});
  create_empty_method(c_cls, "m", void_void);

  scope.push_back(a_cls);
  scope.push_back(b_cls);
  scope.push_back(c_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, HaveImplementorWithSideEffectsTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));

  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_ABSTRACT);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls = create_internal_class(b_type, a_type, {});

  DexType* c_type = DexType::make_type("LC;");
  DexClass* c_cls = create_internal_class(c_type, b_type, {});
  create_throwing_method(c_cls, "m", void_void);

  scope.push_back(a_cls);
  scope.push_back(b_cls);
  scope.push_back(c_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, NoImplementorTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));
  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_ABSTRACT);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls = create_internal_class(b_type, a_type, {});

  scope.push_back(a_cls);
  scope.push_back(b_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, HaveImplementorIntfWithSideEffectsTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));
  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_INTERFACE);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls =
      create_internal_class(b_type, type::java_lang_Object(), {a_type});
  create_throwing_method(b_cls, "m", void_void);

  scope.push_back(a_cls);
  scope.push_back(b_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, HaveImplementorIntfWithoutSideEffectsTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));
  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_INTERFACE);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls =
      create_internal_class(b_type, type::java_lang_Object(), {a_type});

  scope.push_back(a_cls);
  scope.push_back(b_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (return-void)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceEnhanceTest, NoImplementorMayAllocateRegistersTest) {
  Scope scope = create_empty_scope();
  auto void_t = type::_void();
  auto void_void =
      DexProto::make_proto(void_t, DexTypeList::make_type_list({}));
  DexType* a_type = DexType::make_type("LA;");
  DexClass* a_cls = create_internal_class(a_type, type::java_lang_Object(), {},
                                          ACC_PUBLIC | ACC_ABSTRACT);
  create_abstract_method(a_cls, "m", void_void);

  DexType* b_type = DexType::make_type("LB;");
  DexClass* b_cls = create_internal_class(b_type, a_type, {});

  scope.push_back(a_cls);
  scope.push_back(b_cls);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-virtual (v0) "LA;.m:()V")
      (return-void)
    )
  )");

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (const v0 0)
      (throw v0)
    )
  )");
  const auto& pure_methods = get_no_side_effect_methods(scope);
  auto override_graph = method_override_graph::build_graph(scope);
  LocalDce ldce(pure_methods, override_graph.get(),
                /* may_allocate_registers */ true);
}

TEST_F(LocalDceTryTest, invoked_static_method_with_pure_external_barrier) {
  ClassCreator creator(DexType::make_type("LNativeTest;"));
  creator.set_super(type::java_lang_Object());

  auto native_method =
      DexMethod::make_method("LNativeTest;.native:()V")
          ->make_concrete(ACC_PUBLIC | ACC_STATIC | ACC_NATIVE, false);

  auto method = DexMethod::make_method("LNativeTest;.test:()V")
                    ->make_concrete(ACC_PUBLIC | ACC_STATIC, false);
  method->set_code(assembler::ircode_from_string(R"(
                    (
                      (invoke-static () "LNativeTest;.native:()V")
                      (return-void)
                    )
                    )"));
  creator.add_method(method);

  auto code = assembler::ircode_from_string(R"(
    (
      (invoke-static () "LNativeTest;.test:()V")
      (return-void)
    )
  )");
  auto expected_code = assembler::ircode_from_string(R"(
    (
      (return-void)
    )
  )");

  Scope scope{type_class(type::java_lang_Object()), creator.create()};
  std::unordered_set<DexMethodRef*> pure_methods = {native_method};
  // We are computing other no-side-effects method just like the LocalDcePass
  // would.
  auto override_graph = method_override_graph::build_graph(scope);
  std::unordered_set<const DexMethod*> computed_no_side_effects_methods;
  compute_no_side_effects_methods(scope, override_graph.get(), pure_methods,
                                  &computed_no_side_effects_methods);
  for (auto m : computed_no_side_effects_methods) {
    pure_methods.insert(const_cast<DexMethod*>(m));
  }
  LocalDce ldce(pure_methods);
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceTryTest, normalize_new_instances) {
  auto code = assembler::ircode_from_string(R"(
    (
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v0)
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v1)
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v2)
      (invoke-direct (v0) "Ljava/lang/Object;.<init>:()V")
      (invoke-direct (v1) "Ljava/lang/Object;.<init>:()V")
      (invoke-direct (v2) "Ljava/lang/Object;.<init>:()V")
      (return-void)
    )
  )");
  auto expected_code = assembler::ircode_from_string(R"(
    (
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v0)
      (invoke-direct (v0) "Ljava/lang/Object;.<init>:()V")
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v1)
      (invoke-direct (v1) "Ljava/lang/Object;.<init>:()V")
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v2)
      (invoke-direct (v2) "Ljava/lang/Object;.<init>:()V")
      (return-void)
    )
  )");

  Scope scope{type_class(type::java_lang_Object())};
  LocalDce ldce({});
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}

TEST_F(LocalDceTryTest, normalize_new_instances_no_aliases) {
  // This is currently a limitation of the normalization; could be improved one
  // day.
  auto code = assembler::ircode_from_string(R"(
    (
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v0)

      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v1)
      (move-object v3 v1)

      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v2)
      (invoke-direct (v0) "Ljava/lang/Object;.<init>:()V")
      (invoke-direct (v1) "Ljava/lang/Object;.<init>:()V")
      (invoke-direct (v2) "Ljava/lang/Object;.<init>:()V")
      (return-object v3)
    )
  )");
  auto expected_code = assembler::ircode_from_string(R"(
    (
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v1)
      (move-object v3 v1)

      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v0)
      (invoke-direct (v0) "Ljava/lang/Object;.<init>:()V")
      (invoke-direct (v1) "Ljava/lang/Object;.<init>:()V")
      (new-instance "Ljava/lang/Object;")
      (move-result-pseudo-object v2)
      (invoke-direct (v2) "Ljava/lang/Object;.<init>:()V")
      (return-object v3)
    )
  )");

  LocalDce ldce({});
  IRCode* ircode = code.get();
  ldce.dce(ircode);
  EXPECT_CODE_EQ(ircode, expected_code.get());
}
