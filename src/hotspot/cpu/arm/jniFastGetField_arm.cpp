/*
 * Copyright (c) 2008, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "assembler_arm.inline.hpp"
#include "memory/resourceArea.hpp"
#include "prims/jniFastGetField.hpp"
#include "prims/jvm_misc.hpp"
#include "runtime/safepoint.hpp"

#define __ masm->

#define BUFFER_SIZE  96

address JNI_FastGetField::generate_fast_get_int_field0(BasicType type) {
  const char* name = NULL;
  address slow_case_addr = NULL;
  switch (type) {
    case T_BOOLEAN:
      name = "jni_fast_GetBooleanField";
      slow_case_addr = jni_GetBooleanField_addr();
      break;
    case T_BYTE:
      name = "jni_fast_GetByteField";
      slow_case_addr = jni_GetByteField_addr();
      break;
    case T_CHAR:
      name = "jni_fast_GetCharField";
      slow_case_addr = jni_GetCharField_addr();
      break;
    case T_SHORT:
      name = "jni_fast_GetShortField";
      slow_case_addr = jni_GetShortField_addr();
      break;
    case T_INT:
      name = "jni_fast_GetIntField";
      slow_case_addr = jni_GetIntField_addr();
      break;
    case T_LONG:
      name = "jni_fast_GetLongField";
      slow_case_addr = jni_GetLongField_addr();
      break;
    case T_FLOAT:
      name = "jni_fast_GetFloatField";
      slow_case_addr = jni_GetFloatField_addr();
      break;
    case T_DOUBLE:
      name = "jni_fast_GetDoubleField";
      slow_case_addr = jni_GetDoubleField_addr();
      break;
    default:
      ShouldNotReachHere();
  }

  // R0 - jni env
  // R1 - object handle
  // R2 - jfieldID

  const Register Rsafepoint_counter_addr = R3;
  const Register Robj = R1;
  const Register Rres = R0;
  const Register Rres_hi = R1;
  const Register Rsafept_cnt = Rtemp;
  const Register Rsafept_cnt2 = Rsafepoint_counter_addr;
  const Register Rtmp1 = R3; // same as Rsafepoint_counter_addr
  const Register Rtmp2 = R2; // same as jfieldID

  assert_different_registers(Rsafepoint_counter_addr, Rsafept_cnt, Robj, Rres, LR);
  assert_different_registers(Rsafept_cnt, R1, R2, Rtmp1, LR);
  assert_different_registers(Rsafepoint_counter_addr, Rsafept_cnt, Rres, Rres_hi, Rtmp2, LR);
  assert_different_registers(Rsafept_cnt2, Rsafept_cnt, Rres, Rres_hi, LR);

  address fast_entry;

  ResourceMark rm;
  BufferBlob* blob = BufferBlob::create(name, BUFFER_SIZE);
  CodeBuffer cbuf(blob);
  MacroAssembler* masm = new MacroAssembler(&cbuf);
  fast_entry = __ pc();

  // Safepoint check
  InlinedAddress safepoint_counter_addr(SafepointSynchronize::safepoint_counter_addr());
  Label slow_case;
  __ ldr_literal(Rsafepoint_counter_addr, safepoint_counter_addr);

  __ push(RegisterSet(R0, R3));  // save incoming arguments for slow case

  __ ldr_s32(Rsafept_cnt, Address(Rsafepoint_counter_addr));
  __ tbnz(Rsafept_cnt, 0, slow_case);

  __ bic(R1, R1, JNIHandles::weak_tag_mask);

  // Address dependency restricts memory access ordering. It's cheaper than explicit LoadLoad barrier
  __ andr(Rtmp1, Rsafept_cnt, (unsigned)1);
  __ ldr(Robj, Address(R1, Rtmp1));

  Address field_addr;
  if (type != T_BOOLEAN
      && type != T_INT
#ifndef __ABI_HARD__
      && type != T_FLOAT
#endif // !__ABI_HARD__
      ) {
    // Only ldr and ldrb support embedded shift, other loads do not
    __ add(Robj, Robj, AsmOperand(R2, lsr, 2));
    field_addr = Address(Robj);
  } else {
    field_addr = Address(Robj, R2, lsr, 2);
  }
  assert(count < LIST_CAPACITY, "LIST_CAPACITY too small");
  speculative_load_pclist[count] = __ pc();

  switch (type) {
    case T_BOOLEAN:
      __ ldrb(Rres, field_addr);
      break;
    case T_BYTE:
      __ ldrsb(Rres, field_addr);
      break;
    case T_CHAR:
      __ ldrh(Rres, field_addr);
      break;
    case T_SHORT:
      __ ldrsh(Rres, field_addr);
      break;
    case T_INT:
#ifndef __ABI_HARD__
    case T_FLOAT:
#endif
      __ ldr_s32(Rres, field_addr);
      break;
    case T_LONG:
#ifndef __ABI_HARD__
    case T_DOUBLE:
#endif
      // Safe to use ldrd since long and double fields are 8-byte aligned
      __ ldrd(Rres, field_addr);
      break;
#ifdef __ABI_HARD__
    case T_FLOAT:
      __ ldr_float(S0, field_addr);
      break;
    case T_DOUBLE:
      __ ldr_double(D0, field_addr);
      break;
#endif // __ABI_HARD__
    default:
      ShouldNotReachHere();
  }

  // Address dependency restricts memory access ordering. It's cheaper than explicit LoadLoad barrier
#ifdef __ABI_HARD__
  if (type == T_FLOAT || type == T_DOUBLE) {
    __ ldr_literal(Rsafepoint_counter_addr, safepoint_counter_addr);
    __ fmrrd(Rres, Rres_hi, D0);
    __ eor(Rtmp2, Rres, Rres);
    __ ldr_s32(Rsafept_cnt2, Address(Rsafepoint_counter_addr, Rtmp2));
  } else
#endif // __ABI_HARD__
  {
    __ ldr_literal(Rsafepoint_counter_addr, safepoint_counter_addr);
    __ eor(Rtmp2, Rres, Rres);
    __ ldr_s32(Rsafept_cnt2, Address(Rsafepoint_counter_addr, Rtmp2));
  }
  __ cmp(Rsafept_cnt2, Rsafept_cnt);
  // discards saved R0 R1 R2 R3
  __ add(SP, SP, 4 * wordSize, eq);
  __ bx(LR, eq);

  slowcase_entry_pclist[count++] = __ pc();

  __ bind(slow_case);
  __ pop(RegisterSet(R0, R3));
  // thumb mode switch handled by MacroAssembler::jump if needed
  __ jump(slow_case_addr, relocInfo::none, Rtemp);

  __ bind_literal(safepoint_counter_addr);

  __ flush();

  guarantee((__ pc() - fast_entry) <= BUFFER_SIZE, "BUFFER_SIZE too small");

  return fast_entry;
}

address JNI_FastGetField::generate_fast_get_float_field0(BasicType type) {
  ShouldNotReachHere();
  return NULL;
}

address JNI_FastGetField::generate_fast_get_boolean_field() {
  return generate_fast_get_int_field0(T_BOOLEAN);
}

address JNI_FastGetField::generate_fast_get_byte_field() {
  return generate_fast_get_int_field0(T_BYTE);
}

address JNI_FastGetField::generate_fast_get_char_field() {
  return generate_fast_get_int_field0(T_CHAR);
}

address JNI_FastGetField::generate_fast_get_short_field() {
  return generate_fast_get_int_field0(T_SHORT);
}

address JNI_FastGetField::generate_fast_get_int_field() {
  return generate_fast_get_int_field0(T_INT);
}

address JNI_FastGetField::generate_fast_get_long_field() {
  return generate_fast_get_int_field0(T_LONG);
}

address JNI_FastGetField::generate_fast_get_float_field() {
  return generate_fast_get_int_field0(T_FLOAT);
}

address JNI_FastGetField::generate_fast_get_double_field() {
  return generate_fast_get_int_field0(T_DOUBLE);
}
