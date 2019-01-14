//===--- Xtensa.cpp - Implement Xtensa target feature support -------------===//
//
// Copyright (c) QNX Software Systems
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Xtensa TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Xtensa.h"
#include "clang/Basic/Builtins.h"

using namespace clang;
using namespace clang::targets;

void
XtensaTargetInfo::getTargetDefines(const LangOptions &Opts, MacroBuilder &Builder) const {
  Builder.defineMacro("__IEEE_LITTLE_ENDIAN");
}
