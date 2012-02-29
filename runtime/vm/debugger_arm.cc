// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_ARM)

#include "vm/debugger.h"

namespace dart {

RawInstance* ActivationFrame::GetLocalVarValue(intptr_t slot_index) {
  UNIMPLEMENTED();
  return NULL;
}


RawInstance* ActivationFrame::GetInstanceCallReceiver(
                 intptr_t num_actual_args) {
  UNIMPLEMENTED();
  return NULL;
}


void Breakpoint::PatchFunctionReturn() {
  UNIMPLEMENTED();
}


void Breakpoint::RestoreFunctionReturn() {
  UNIMPLEMENTED();
}

}  // namespace dart

#endif  // defined TARGET_ARCH_ARM
