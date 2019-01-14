//===--- QCC.h - QNX qcc ToolChain Implementation  --------------*- C++ -*-===//
//
// Copyright (c) QNX Software Systems
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_QCC_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_QCC_H

#include "Gnu.h"

namespace clang {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY QCC : public Generic_ELF {
public:
  QCC(const Driver &D, const llvm::Triple &Triple,
        const llvm::opt::ArgList &Args);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;

  static bool isExeQCC(std::string ClangExecutable);
  static std::string getTriple(ArrayRef<const char *> Args);
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_QCC_H
