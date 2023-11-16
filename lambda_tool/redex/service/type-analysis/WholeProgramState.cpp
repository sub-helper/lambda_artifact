/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "WholeProgramState.h"

#include "BaseIRAnalyzer.h"
#include "Dominators.h"
#include "GlobalTypeAnalyzer.h"
#include "Resolver.h"
#include "Show.h"
#include "Walkers.h"

using namespace type_analyzer;

std::ostream& operator<<(std::ostream& out, const DexField* field) {
  out << SHOW(field);
  return out;
}

std::ostream& operator<<(std::ostream& out, const DexMethod* method) {
  out << SHOW(method);
  return out;
}

/* Map of method to known return type - Esepecially for the Boxed values. TODO
 * construct the list.*/
std::unordered_map<const char*, const char*> STATIC_METHOD_TO_TYPE_MAP = {
    {"Ljava/lang/Boolean;.valueOf:(Z)Ljava/lang/Boolean;",
     "Ljava/lang/Boolean;"},
    {"Ljava/lang/Character;.valueOf:(C)Ljava/lang/Character;",
     "Ljava/lang/Character;"},
    {"Ljava/lang/Byte;.valueOf:(B)Ljava/lang/Byte;", "Ljava/lang/Byte;"},
    {"Ljava/lang/Integer;.valueOf:(I)Ljava/lang/Integer;",
     "Ljava/lang/Integer;"},
    {"Ljava/lang/Long;.valueOf:(J)Ljava/lang/Long;", "Ljava/lang/Long;"},
    {"Ljava/lang/Float;.valueOf:(F)Ljava/lang/Float;", "Ljava/lang/Float;"},
    {"Ljava/lang/Double;.valueOf:(D)Ljava/lang/Double;", "Ljava/lang/Double;"},
    {"Ljava/lang/String;.valueOf:(C)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.valueOf:(D)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.valueOf:(F)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.valueOf:(I)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.valueOf:(J)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.valueOf:(Z)Ljava/lang/String;", "Ljava/lang/String;"},
    {"Ljava/lang/String;.replace:(Ljava/lang/CharSequence;Ljava/lang/"
     "CharSequence;)Ljava/lang/String;",
     "Ljava/lang/String;"}};

namespace {

bool is_reference(const DexField* field) {
  return type::is_object(field->get_type());
}

bool returns_reference(const DexMethod* method) {
  auto rtype = method->get_proto()->get_rtype();
  return type::is_object(rtype);
}

/*
 * If a static field is not populated in clinit, it is implicitly null.
 */
void set_sfields_in_partition(const DexClass* cls,
                              const DexTypeEnvironment& env,
                              DexTypeFieldPartition* field_partition) {
  for (auto& field : cls->get_sfields()) {
    if (!is_reference(field)) {
      continue;
    }
    auto domain = env.get(field);
    if (!domain.is_top()) {
      TRACE(TYPE, 5, "%s has type %s after <clinit>", SHOW(field),
            SHOW(domain));
      always_assert(field->get_class() == cls->get_type());
    } else {
      TRACE(TYPE, 5, "%s has null type after <clinit>", SHOW(field));
      domain = DexTypeDomain::null();
    }
    field_partition->set(field, domain);
  }
}

/*
 * If an instance field is not populated in ctor, it is implicitly null.
 * Note that a class can have multipl ctors. If an instance field is not
 * initialized in any ctor, it is nullalbe. That's why we need to join the type
 * mapping across all ctors.
 */
void set_ifields_in_partition(const DexClass* cls,
                              const DexTypeEnvironment& env,
                              DexTypeFieldPartition* field_partition) {
  for (auto& field : cls->get_ifields()) {
    if (!is_reference(field)) {
      continue;
    }
    auto domain = env.get(field);
    if (!domain.is_top()) {
      TRACE(TYPE, 5, "%s has type %s after <init>", SHOW(field), SHOW(domain));
      always_assert(field->get_class() == cls->get_type());
    } else {
      TRACE(TYPE, 5, "%s has null type after <init>", SHOW(field));
      domain = DexTypeDomain::null();
    }
    field_partition->update(field, [&domain](auto* current_type) {
      current_type->join_with(domain);
    });
  }
}

bool analyze_gets_helper(const WholeProgramState* whole_program_state,
                         const IRInstruction* insn,
                         DexTypeEnvironment* env) {
  auto field = resolve_field(insn->get_field());
  if (field == nullptr || !type::is_object(field->get_type())) {
    return false;
  }
  auto field_type = whole_program_state->get_field_type(field);
  if (field_type.is_top()) {
    return false;
  }
  env->set(RESULT_REGISTER, field_type);
  return true;
}

} // namespace

namespace type_analyzer {

WholeProgramState::WholeProgramState(
    const Scope& scope,
    const global::GlobalTypeAnalyzer& gta,
    const std::unordered_set<DexMethod*>& non_true_virtuals,
    const ConcurrentSet<const DexMethod*>& any_init_reachables)
    : m_any_init_reachables(any_init_reachables) {
  // Exclude fields we cannot correctly analyze.
  walk::fields(scope, [&](DexField* field) {
    if (!type::is_object(field->get_type())) {
      return;
    }
    // We assume that a field we cannot delete is marked by a Proguard keep rule
    // or an annotation. The reason behind is that the field is referenced by
    // non-dex code.
    if (!can_delete(field) || field->is_external() || is_volatile(field)) {
      return;
    }
    m_known_fields.emplace(field);
  });

  // TODO: revisit this for multiple callee call graph.
  // Put non-root non true virtual methods in known methods.
  for (const auto& non_true_virtual : non_true_virtuals) {
    if (!root(non_true_virtual) && non_true_virtual->get_code()) {
      m_known_methods.emplace(non_true_virtual);
    }
  }
  walk::code(scope, [&](DexMethod* method, const IRCode&) {
    if (!method->is_virtual() && method->get_code()) {
      // Put non virtual methods in known methods.
      m_known_methods.emplace(method);
    }
  });
  setup_known_method_returns();
  analyze_clinits_and_ctors(scope, gta, &m_field_partition);
  collect(scope, gta);
}

std::string WholeProgramState::show_field(const DexField* f) { return show(f); }
std::string WholeProgramState::show_method(const DexMethod* m) {
  return show(m);
}
void WholeProgramState::setup_known_method_returns() {
  for (auto& p : STATIC_METHOD_TO_TYPE_MAP) {
    auto method = DexMethod::make_method(p.first);
    auto type = DexTypeDomain(
        DexType::make_type(DexString::make_string(p.second)), NOT_NULL);
    m_known_method_returns.insert(std::make_pair(method, type));
  }
}

/*
 * We initialize the type mapping of all fields using the result of the local
 * FieldTypeEnvironment of clinits and ctors. We do so in order to correctly
 * initialize the NullnessDomain for fields. A static or instance field is
 * implicitly null if not initialized with non-null value in clinit or ctor
 * respectively.
 *
 * The implicit null value is not visible to the rest of the program before the
 * execution of clinit or ctor. That's why we don't want to simply initialize
 * all fields as null. That way we are overly conservative. A final instance
 * field that is always initialized in ctors is not nullable to the rest of the
 * program.
 *
 * TODO:
 * There are exceptions of course. That is before the end of the ctor, our
 * nullness result is not sound. If a ctor calls another method, that method
 * could access an uninitialiezd instance field on the class. We don't cover
 * this case correctly right now.
 */
void WholeProgramState::analyze_clinits_and_ctors(
    const Scope& scope,
    const global::GlobalTypeAnalyzer& gta,
    DexTypeFieldPartition* field_partition) {
  for (DexClass* cls : scope) {
    auto clinit = cls->get_clinit();
    if (clinit) {
      IRCode* code = clinit->get_code();
      auto& cfg = code->cfg();
      auto lta = gta.get_local_analysis(clinit);
      auto env = lta->get_exit_state_at(cfg.exit_block());
      set_sfields_in_partition(cls, env, field_partition);
    } else {
      set_sfields_in_partition(cls, DexTypeEnvironment::top(), field_partition);
    }

    const auto& ctors = cls->get_ctors();
    for (auto* ctor : ctors) {
      if (!is_reachable(gta, ctor)) {
        continue;
      }
      IRCode* code = ctor->get_code();
      auto& cfg = code->cfg();
      auto lta = gta.get_local_analysis(ctor);
      auto env = lta->get_exit_state_at(cfg.exit_block());
      set_ifields_in_partition(cls, env, field_partition);
    }
  }
}
void collect_dirty_return_blocks_constraints(
    std::vector<std::unordered_set<std::string>>& vec_dirty_constraints,
    std::vector<std::vector<cfg::Edge*>>& vec_vec_pred,
    int return_block_idx) {
  std::unordered_set<std::string> constraints;
  for (auto e : vec_vec_pred[return_block_idx]) {
    if (e->type() == cfg::EdgeType::EDGE_BRANCH) {
      // true branch
      constraints.insert(show((e->src()->get_last_insn())->insn) + ";true");
    } else if (e->type() == cfg::EdgeType::EDGE_GOTO) {
      // The false branch of an if statement, default of a switch, or
      // unconditional goto
      std::vector<cfg::Edge*> goto_block_predecessors(e->src()->preds());
      if (goto_block_predecessors.size() == 1) {
        constraints.insert(
            show((goto_block_predecessors[0]->src()->get_last_insn())->insn) +
            ";false");
      } else if (goto_block_predecessors.size() == 0) {
        constraints.insert(show((e->src()->get_last_insn())->insn) +
                           ";gotonopredecessorsfalse");
      } else {
        constraints.insert(
            show((goto_block_predecessors[0]->src()->get_last_insn())->insn) +
            ";multiple predecessors false");
      }
    } else if (e->type() == cfg::EdgeType::EDGE_THROW) {
      constraints.insert(";exception");
    } else {
      not_reached_log("only deal with edge types branch goto and throw\n");
    }
  }
  std::cout
      << "finish collection for this method's one type of return blocks\n";
  vec_dirty_constraints.push_back(constraints);
  return;
}
void process_constraints(
    std::vector<std::unordered_set<std::string>>& vec_constraints) {
  int infeasible_equation = 0;
  for (int idx = 0; idx < vec_constraints.size(); idx++) {
    std::cout << "constraints set" << idx << "->";
    int unsat_clause = 0;
    for (auto c : vec_constraints[idx]) {
      std::cout << c << "*";
      if (c.find("IF_NEZ") != std::string::npos &&
          c.find("false") != std::string::npos) {
        unsat_clause++;
      } else if (c.find("IF_EQZ") != std::string::npos &&
                 c.find("true") != std::string::npos) {
        unsat_clause++;
      }
    }
    if (unsat_clause == vec_constraints[idx].size()) {
      infeasible_equation++;
    }
    std::cout << "\n";
  }
  if (infeasible_equation == vec_constraints.size()) {
    // This return block is unreachable
    vec_constraints.clear();
    std::cout << "Return null block is infeasible\n";

  } else {
    std::cout << "Have feasible return null blocks\n";
  }
}
void WholeProgramState::collect(const Scope& scope,
                                const global::GlobalTypeAnalyzer& gta) {
  ConcurrentMap<const DexField*, std::vector<DexTypeDomain>> fields_tmp;
  ConcurrentMap<const DexMethod*, std::vector<DexTypeDomain>> methods_tmp;
  std::unordered_map<const DexMethod*, std::vector<std::vector<cfg::Edge*>>>
      predecessors_tmp;
  walk::parallel::methods(scope, [&](DexMethod* method) {
    std::vector<std::vector<cfg::Edge*>> return_block_predecessors;
    IRCode* code = method->get_code();
    if (code == nullptr) {
      return;
    }
    if (!is_reachable(gta, method)) {
      return;
    }
    auto& cfg = code->cfg();
    auto lta = gta.get_local_analysis(method);
    std::unordered_map<reg_t, std::string> parameters_mapping;
    for (cfg::Block* b : cfg.blocks()) {
      auto env = lta->get_entry_state_at(b);
      for (auto& mie : InstructionIterable(b)) {
        auto* insn = mie.insn;
        lta->analyze_instruction(insn, &env);
        collect_field_types(insn, env, &fields_tmp);
        if (opcode::is_a_load_param(insn->opcode())) {
          std::cout << "encounter parameter load" << show(insn) << std::endl;
        }
        if (collect_return_types(insn, env, method, &methods_tmp)) {
          // get a sequence of predecessors for this return value
          std::vector<cfg::Edge*> tmp_predecessors(b->preds());
          // get this return block's immediate dominator
          std::cout << "predecessor size is" << tmp_predecessors.size()
                    << std::endl;
          return_block_predecessors.push_back(tmp_predecessors);
        }
      }
    }
    predecessors_tmp.insert(std::make_pair(method, return_block_predecessors));
  });
  for (const auto& pair : fields_tmp) {
    for (auto& type : pair.second) {
      m_field_partition.update(pair.first, [&type](auto* current_type) {
        current_type->join_with(type);
      });
    }
  }
  for (const auto& pair : methods_tmp) {
    std::cout << "method name is\n"
              << SHOW(pair.first) << "size of return block is\n"
              << pair.second.size() << "previous return value is"
              << m_method_partition.get(pair.first) << std::endl;
    auto it = predecessors_tmp.find(pair.first);
    auto vec_vec_pred = it->second;
    always_assert(it != predecessors_tmp.end());
    int return_block_idx = 0;
    // iterate through each possible return blocks
    std::vector<std::unordered_set<std::string>> vec_null_return_constraints;
    std::vector<std::unordered_set<std::string>> vec_notnull_return_constraints;
    std::vector<std::unordered_set<std::string>>
        vec_nullable_return_constraints;

    for (auto& type : pair.second) {
      // iterate mthod's return blocks
      const auto prev_result = m_method_partition.get(pair.first);
      std::cout << "joined value" << type << std::endl;
      m_method_partition.update(pair.first, [&type](auto* current_type) {
        current_type->join_with(type);
      });
      std::cout << "after join result " << m_method_partition.get(pair.first)
                << std::endl;
      if (type.is_null()) {
        collect_dirty_return_blocks_constraints(vec_null_return_constraints,
                                                vec_vec_pred, return_block_idx);
      } else if (type.is_not_null()) {
        collect_dirty_return_blocks_constraints(vec_notnull_return_constraints,
                                                vec_vec_pred, return_block_idx);
      } else if (type.is_nullable()) {
        collect_dirty_return_blocks_constraints(vec_nullable_return_constraints,
                                                vec_vec_pred, return_block_idx);
      } else {
        not_reached_log("Type is bottom, which is unexpected\n");
      }
      if (prev_result.is_not_null() || prev_result.is_null()) {
        if (m_method_partition.get(pair.first).is_nullable()) {
          std::cout << "change from nullnotnull to nullable\n";
        }
      }
      return_block_idx++;
    }
    // int original_null_return_constraints_size =
    //     vec_null_return_constraints.size();
    // std::cout << "size of null return constraints"
    //           << vec_null_return_constraints.size() << std::endl;
    // // TODO: check whether null return constraints are feasible
    // process_constraints(vec_null_return_constraints);
    // std::cout << "size of nonnull return constraints"
    //           << vec_notnull_return_constraints.size() << std::endl;
    // std::cout << "size of nullable return constraints"
    //           << vec_nullable_return_constraints.size() << std::endl;
    // auto current_result = m_method_partition.get(pair.first);
    // std::cout << "current result is" << current_result << std::endl;
    // if (vec_nullable_return_constraints.size() == 0 &&
    //     original_null_return_constraints_size > 0 &&
    //     vec_null_return_constraints.size() == 0) {
    //   std::cout << "method" << SHOW(pair.first)
    //             << "achieve more precise result notnull\n";
    //   // SET the return value to nonnull
    //   DexTypeDomain finer_result(current_result.get_dex_type().get(),
    //                              Nullness::NOT_NULL);
    //   m_method_partition.set(pair.first, finer_result);
    //   const auto update_result = m_method_partition.get(pair.first);
    //   std::cout << "update value" << update_result << std::endl;
    // } else {
    //   std::cout << "no update value" << current_result << std::endl;
    // }
  }
  std::cout << "*****************************************\n";
}

void WholeProgramState::collect_field_types(
    const IRInstruction* insn,
    const DexTypeEnvironment& env,
    ConcurrentMap<const DexField*, std::vector<DexTypeDomain>>* field_tmp) {
  if (!opcode::is_an_sput(insn->opcode()) &&
      !opcode::is_an_iput(insn->opcode())) {
    return;
  }
  auto field = resolve_field(insn->get_field());
  if (!field || !type::is_object(field->get_type())) {
    return;
  }
  auto type = env.get(insn->src(0));
  if (traceEnabled(TYPE, 5)) {
    std::ostringstream ss;
    ss << type;
    TRACE(TYPE, 5, "collecting field %s -> %s", SHOW(field), ss.str().c_str());
  }
  field_tmp->update(field,
                    [type](const DexField*,
                           std::vector<DexTypeDomain>& s,
                           bool /* exists */) { s.emplace_back(type); });
}

bool WholeProgramState::collect_return_types(
    const IRInstruction* insn,
    const DexTypeEnvironment& env,
    const DexMethod* method,
    ConcurrentMap<const DexMethod*, std::vector<DexTypeDomain>>* method_tmp) {
  auto op = insn->opcode();
  if (!opcode::is_a_return(op)) {
    return false;
  }
  if (!returns_reference(method)) {
    // We must set the binding to Top here to record the fact that this method
    // does indeed return -- even though `void` is not actually a return type,
    // this tells us that the code following any invoke of this method is
    // reachable.
    method_tmp->update(
        method,
        [](const DexMethod*, std::vector<DexTypeDomain>& s, bool /* exists */) {
          s.emplace_back(DexTypeDomain::top());
        });
    return true;
  }
  auto type = env.get(insn->src(0));
  std::cout << "debug " << SHOW(method) << "collect_return_types" << type
            << std::endl;
  method_tmp->update(method,
                     [type](const DexMethod*,
                            std::vector<DexTypeDomain>& s,
                            bool /* exists */) { s.emplace_back(type); });
  return true;
}

bool WholeProgramState::is_reachable(const global::GlobalTypeAnalyzer& gta,
                                     const DexMethod* method) const {
  return !m_known_methods.count(method) || gta.is_reachable(method);
}

std::string WholeProgramState::print_field_partition_diff(
    const WholeProgramState& other) const {
  std::ostringstream ss;
  if (m_field_partition.is_top()) {
    ss << "[wps] diff this < is top" << std::endl;
    return ss.str();
  }
  if (other.m_field_partition.is_top()) {
    ss << "[wps] diff other > is top" << std::endl;
    return ss.str();
  }
  const auto& this_field_bindings = m_field_partition.bindings();
  const auto& other_field_bindings = other.m_field_partition.bindings();
  for (auto& pair : this_field_bindings) {
    auto field = pair.first;
    if (!other_field_bindings.count(field)) {
      ss << "[wps] diff " << field << " < " << pair.second << std::endl;
    } else {
      const auto& this_type = pair.second;
      const auto& other_type = other_field_bindings.at(field);
      if (!this_type.equals(other_type)) {
        ss << "[wps] diff " << field << " < " << this_type << " > "
           << other_type << std::endl;
      }
    }
  }
  for (auto& pair : other_field_bindings) {
    auto field = pair.first;
    if (!this_field_bindings.count(field)) {
      ss << "[wps] diff " << field << " > " << pair.second << std::endl;
    }
  }

  return ss.str();
}

std::string WholeProgramState::print_method_partition_diff(
    const WholeProgramState& other) const {
  std::ostringstream ss;
  if (m_method_partition.is_top()) {
    ss << "[wps] diff this < is top" << std::endl;
    return ss.str();
  }
  if (other.m_method_partition.is_top()) {
    ss << "[wps] diff other > is top" << std::endl;
    return ss.str();
  }
  const auto& this_method_bindings = m_method_partition.bindings();
  const auto& other_method_bindings = other.m_method_partition.bindings();
  for (auto& pair : this_method_bindings) {
    auto method = pair.first;
    if (!other_method_bindings.count(method)) {
      ss << "[wps] diff " << method << " < " << pair.second << std::endl;
    } else {
      const auto& this_type = pair.second;
      const auto& other_type = other_method_bindings.at(method);
      if (!this_type.equals(other_type)) {
        ss << "[wps] diff " << method << " < " << this_type << " > "
           << other_type << std::endl;
      }
    }
  }
  for (auto& pair : other_method_bindings) {
    auto method = pair.first;
    if (!this_method_bindings.count(method)) {
      ss << "[wps] diff " << method << " > " << pair.second << std::endl;
    }
  }

  return ss.str();
}

bool WholeProgramAwareAnalyzer::analyze_iget(
    const WholeProgramState* whole_program_state,
    const IRInstruction* insn,
    DexTypeEnvironment* env) {
  return analyze_gets_helper(whole_program_state, insn, env);
}

bool WholeProgramAwareAnalyzer::analyze_sget(
    const WholeProgramState* whole_program_state,
    const IRInstruction* insn,
    DexTypeEnvironment* env) {
  return analyze_gets_helper(whole_program_state, insn, env);
}

bool WholeProgramAwareAnalyzer::analyze_invoke(
    const WholeProgramState* whole_program_state,
    const IRInstruction* insn,
    DexTypeEnvironment* env) {
  if (whole_program_state == nullptr) {
    return false;
  }
  auto known_type = whole_program_state->get_type_for_method_with_known_type(
      insn->get_method());
  if (known_type) {
    std::cout << "find known type" << SHOW(known_type) << std::endl;
    env->set(RESULT_REGISTER, *known_type);
    return true;
  }

  auto method = resolve_method(insn->get_method(), opcode_to_search(insn));
  if (method == nullptr) {
    return false;
  }
  if (!returns_reference(method)) {
    // Reset RESULT_REGISTER
    env->set(RESULT_REGISTER, DexTypeDomain::top());
    return false;
  }
  auto type = whole_program_state->get_return_type(method);
  env->set(RESULT_REGISTER, type);
  return true;
}

} // namespace type_analyzer
