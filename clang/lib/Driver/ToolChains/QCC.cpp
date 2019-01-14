//===--- QCC.cpp - QNX qcc Toolchain Implementation -----------------------===//
//
// Copyright (c) QNX Software Systems
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "QCC.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang::driver::toolchains;

QCC::QCC(const Driver &D, const llvm::Triple &Triple, const llvm::opt::ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
}

bool QCC::isExeQCC(std::string ClangExecutable) {
  return llvm::sys::path::stem(ClangExecutable) == "qcc";
}

std::string QCC::getTriple(llvm::ArrayRef<const char *> Args) {
  for (const char *Arg: Args) {
    llvm::StringRef argStr(Arg);
    if (argStr.startswith_lower("-V")) {
      return llvm::StringSwitch<std::string>(argStr)
        .Case("-Vgcc_ntoarmv7le", "arm-unknown-nto-qnx7.0.0eabi")
        .Default("i586-pc-nto-qnx7.0.0");
    }
  }
  return "";
}

void
QCC::AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<1024> P(getDriver().ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P.str());
 }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;
  
  Optional<std::string> qnxTarget = llvm::sys::Process::GetEnv("QNX_TARGET");
  if (qnxTarget) {
    SmallString<1024> usrInclude(*qnxTarget);
    llvm::sys::path::append(usrInclude, "/usr/include");
    addSystemInclude(DriverArgs, CC1Args, usrInclude);
    llvm::sys::path::append(usrInclude, "/c++/v1");
    addSystemInclude(DriverArgs, CC1Args, usrInclude);
  }
}
