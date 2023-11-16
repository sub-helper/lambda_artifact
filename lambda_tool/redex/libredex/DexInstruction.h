/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

#include "Debug.h"
#include "DexDefs.h"
#include "DexOpcode.h"
#include "Gatherable.h"
#include "IROpcode.h"

#define MAX_ARG_COUNT (4)

class DexIdx;
class DexOutputIdx;
class DexString;

class DexInstruction : public Gatherable {
 protected:
  enum {
    REF_NONE,
    REF_STRING,
    REF_TYPE,
    REF_FIELD,
    REF_METHOD,
    REF_CALLSITE,
    REF_METHODHANDLE,
  } m_ref_type{REF_NONE};

 private:
  uint16_t m_opcode = OPCODE_NOP;
  uint16_t m_arg[MAX_ARG_COUNT] = {};

 protected:
  uint16_t m_count = 0;

  // use clone() instead
  DexInstruction(const DexInstruction&) = default;

  // Ref-less opcodes, largest size is 5 insns.
  // If the constructor is called with a non-numeric
  // count, we'll have to add a assert here.
  // Holds formats:
  // 10x 11x 11n 12x 22x 21s 21h 31i 32x 51l
  DexInstruction(const uint16_t* opcodes, int count) {
    always_assert_log(count <= MAX_ARG_COUNT,
                      "arg count %d exceeded the limit of %d",
                      count,
                      MAX_ARG_COUNT);
    m_opcode = *opcodes++;
    m_count = count;
    for (int i = 0; i < count; i++) {
      m_arg[i] = opcodes[i];
    }
  }

 public:
  explicit DexInstruction(DexOpcode op)
      : m_opcode(op), m_count(count_from_opcode()) {}

  DexInstruction(DexOpcode opcode, uint16_t arg) : DexInstruction(opcode) {
    redex_assert(m_count == 1);
    m_arg[0] = arg;
  }

 protected:
  void encode_args(uint16_t*& insns) const {
    for (int i = 0; i < m_count; i++) {
      *insns++ = m_arg[i];
    }
  }

  void encode_opcode(uint16_t*& insns) const { *insns++ = m_opcode; }

 public:
  static DexInstruction* make_instruction(DexIdx* idx,
                                          const uint16_t** insns_ptr);
  /* Creates the right subclass of DexInstruction for the given opcode */
  static DexInstruction* make_instruction(DexOpcode);
  virtual void encode(DexOutputIdx* dodx, uint16_t*& insns) const;
  virtual uint16_t size() const;
  virtual DexInstruction* clone() const { return new DexInstruction(*this); }
  bool operator==(const DexInstruction&) const;

  bool has_string() const { return m_ref_type == REF_STRING; }
  bool has_type() const { return m_ref_type == REF_TYPE; }
  bool has_field() const { return m_ref_type == REF_FIELD; }
  bool has_method() const { return m_ref_type == REF_METHOD; }
  bool has_callsite() const { return m_ref_type == REF_CALLSITE; }
  bool has_methodhandle() const { return m_ref_type == REF_METHODHANDLE; }

  bool has_range() const { return dex_opcode::has_range(opcode()); }
  bool has_literal() const { return dex_opcode::has_literal(opcode()); }
  bool has_offset() const { return dex_opcode::has_offset(opcode()); }

  /*
   * Number of registers used.
   */
  bool has_dest() const;
  unsigned srcs_size() const;

  /*
   * Accessors for logical parts of the instruction.
   */
  DexOpcode opcode() const;
  uint16_t dest() const;
  uint16_t src(int i) const;
  uint16_t arg_word_count() const;
  uint16_t range_base() const;
  uint16_t range_size() const;
  int64_t get_literal() const;
  int32_t offset() const;

  /*
   * Setters for logical parts of the instruction.
   */
  DexInstruction* set_opcode(DexOpcode);
  DexInstruction* set_dest(uint16_t vreg);
  DexInstruction* set_src(int i, uint16_t vreg);
  DexInstruction* set_srcs(const std::vector<uint16_t>& vregs);
  DexInstruction* set_arg_word_count(uint16_t count);
  DexInstruction* set_range_base(uint16_t base);
  DexInstruction* set_range_size(uint16_t size);
  DexInstruction* set_literal(int64_t literal);
  DexInstruction* set_offset(int32_t offset);

  /*
   * The number of shorts needed to encode the args.
   */
  uint16_t count() const { return m_count; }

  friend std::string show(const DexInstruction* insn);
  friend std::string show_deobfuscated(const DexInstruction* insn);

 private:
  unsigned count_from_opcode() const;
};

class DexOpcodeString : public DexInstruction {
 private:
  DexString* m_string;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_strings(std::vector<DexString*>& lstring) const override;
  DexOpcodeString* clone() const override { return new DexOpcodeString(*this); }

  DexOpcodeString(DexOpcode opcode, DexString* str) : DexInstruction(opcode) {
    m_string = str;
    m_ref_type = REF_STRING;
  }

  DexString* get_string() const { return m_string; }

  bool jumbo() const { return opcode() == DOPCODE_CONST_STRING_JUMBO; }

  void set_string(DexString* str) { m_string = str; }
};

class DexOpcodeType : public DexInstruction {
 private:
  DexType* m_type;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_types(std::vector<DexType*>& ltype) const override;
  DexOpcodeType* clone() const override { return new DexOpcodeType(*this); }

  DexOpcodeType(DexOpcode opcode, DexType* type) : DexInstruction(opcode) {
    m_type = type;
    m_ref_type = REF_TYPE;
  }

  DexOpcodeType(DexOpcode opcode, DexType* type, uint16_t arg)
      : DexInstruction(opcode, arg) {
    m_type = type;
    m_ref_type = REF_TYPE;
  }

  DexType* get_type() const { return m_type; }

  void set_type(DexType* type) { m_type = type; }
};

class DexOpcodeField : public DexInstruction {
 private:
  DexFieldRef* m_field;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_fields(std::vector<DexFieldRef*>& lfield) const override;
  DexOpcodeField* clone() const override { return new DexOpcodeField(*this); }

  DexOpcodeField(DexOpcode opcode, DexFieldRef* field)
      : DexInstruction(opcode) {
    m_field = field;
    m_ref_type = REF_FIELD;
  }

  DexFieldRef* get_field() const { return m_field; }
  void set_field(DexFieldRef* field) { m_field = field; }
};

class DexOpcodeMethod : public DexInstruction {
 private:
  DexMethodRef* m_method;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_methods(std::vector<DexMethodRef*>& lmethod) const override;
  DexOpcodeMethod* clone() const override { return new DexOpcodeMethod(*this); }

  DexOpcodeMethod(DexOpcode opcode, DexMethodRef* meth, uint16_t arg = 0)
      : DexInstruction(opcode, arg) {
    m_method = meth;
    m_ref_type = REF_METHOD;
  }

  DexMethodRef* get_method() const { return m_method; }

  void set_method(DexMethodRef* method) { m_method = method; }
};

class DexOpcodeCallSite : public DexInstruction {
 private:
  DexCallSite* m_callsite;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_callsites(std::vector<DexCallSite*>& lcallsite) const override;
  void gather_strings(std::vector<DexString*>& lstring) const override;
  void gather_methodhandles(
      std::vector<DexMethodHandle*>& lmethodhandle) const override;
  void gather_methods(std::vector<DexMethodRef*>& lmethod) const override;
  void gather_fields(std::vector<DexFieldRef*>& lfield) const override;
  DexOpcodeCallSite* clone() const override {
    return new DexOpcodeCallSite(*this);
  }

  DexOpcodeCallSite(DexOpcode opcode, DexCallSite* callsite, uint16_t arg = 0)
      : DexInstruction(opcode, arg) {
    m_callsite = callsite;
    m_ref_type = REF_CALLSITE;
  }

  DexCallSite* get_callsite() const { return m_callsite; }

  void set_callsite(DexCallSite* callsite) { m_callsite = callsite; }
};

class DexOpcodeMethodHandle : public DexInstruction {
 private:
  DexMethodHandle* m_methodhandle;

 public:
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  void gather_methodhandles(
      std::vector<DexMethodHandle*>& lmethodhandle) const override;
  void gather_methods(std::vector<DexMethodRef*>& lmethod) const override;
  void gather_fields(std::vector<DexFieldRef*>& lfield) const override;
  DexOpcodeMethodHandle* clone() const override {
    return new DexOpcodeMethodHandle(*this);
  }

  DexOpcodeMethodHandle(DexOpcode opcode,
                        DexMethodHandle* methodhandle,
                        uint16_t arg = 0)
      : DexInstruction(opcode, arg) {
    m_methodhandle = methodhandle;
    m_ref_type = REF_METHODHANDLE;
  }

  DexMethodHandle* get_methodhandle() const { return m_methodhandle; }

  void set_methodhandle(DexMethodHandle* methodhandle) {
    m_methodhandle = methodhandle;
  }
};

class DexOpcodeData : public DexInstruction {
 private:
  uint16_t m_data_count;
  uint16_t* m_data;

 public:
  // This size refers to the whole instruction, not just the data portion
  uint16_t size() const override;
  void encode(DexOutputIdx* dodx, uint16_t*& insns) const override;
  DexOpcodeData* clone() const override { return new DexOpcodeData(*this); }

  DexOpcodeData(const uint16_t* opcodes, int count)
      : DexInstruction(opcodes, 0),
        m_data_count(count),
        m_data(new uint16_t[count]) {
    opcodes++;
    memcpy(m_data, opcodes, count * sizeof(uint16_t));
  }

  explicit DexOpcodeData(const std::vector<uint16_t>& opcodes)
      : DexInstruction(&opcodes[0], 0),
        m_data_count(opcodes.size() - 1),
        m_data(new uint16_t[opcodes.size() - 1]) {
    const uint16_t* data = opcodes.data() + 1;
    memcpy(m_data, data, (opcodes.size() - 1) * sizeof(uint16_t));
  }

  DexOpcodeData(const DexOpcodeData& op)
      : DexInstruction(op),
        m_data_count(op.m_data_count),
        m_data(new uint16_t[m_data_count]) {
    memcpy(m_data, op.m_data, m_data_count * sizeof(uint16_t));
  }

  DexOpcodeData& operator=(DexOpcodeData op) {
    DexInstruction::operator=(op);
    std::swap(m_data, op.m_data);
    return *this;
  }

  ~DexOpcodeData() override { delete[] m_data; }

  const uint16_t* data() const { return m_data; }
  // This size refers to just the length of the data array
  uint16_t data_size() const { return m_data_count; }
};

// helper function to create fill-array-data-payload according to
// https://source.android.com/devices/tech/dalvik/dalvik-bytecode#fill-array
template <typename IntType>
DexOpcodeData* encode_fill_array_data_payload(const std::vector<IntType>& vec) {
  static_assert(std::is_integral<IntType>::value,
                "fill-array-data-payload can only contain integral values.");
  int width = sizeof(IntType);
  size_t total_copy_size = vec.size() * width;
  // one "code unit" is a 2 byte word
  int total_used_code_units =
      (total_copy_size + 1 /* for rounding up int division */) / 2 + 4;
  std::vector<uint16_t> data(total_used_code_units);
  uint16_t* ptr = data.data();
  ptr[0] = FOPCODE_FILLED_ARRAY; // header
  ptr[1] = width;
  *(uint32_t*)(ptr + 2) = vec.size();
  uint8_t* data_bytes = (uint8_t*)(ptr + 4);
  memcpy(data_bytes, (void*)vec.data(), total_copy_size);
  return new DexOpcodeData(data);
}

/**
 * Return a copy of the instruction passed in.
 */
DexInstruction* copy_insn(DexInstruction* insn);
