//===--- Xtensa.h - Declare Xtensa target feature support -------*- C++ -*-===//
//
// Copyright (c) QNX Software Systems
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Xtensa TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_XTENSA_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_XTENSA_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"
#include "ARM.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY XtensaTargetInfo : public ARMleTargetInfo {
public:
  XtensaTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
    : ARMleTargetInfo(Triple, Opts) {}

  void
  getTargetDefines(const LangOptions &Opts,
                   MacroBuilder &Builder) const override;
};

} // targets
} // clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_XTENSA_H
