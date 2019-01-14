//===--- GlobalCompilationDatabase.cpp ---------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// Some modifications Copyright (c) QNX Software and licensed same.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GlobalCompilationDatabase.h"
#include "Logger.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"

namespace clang {
namespace clangd {
namespace {

void adjustArguments(tooling::CompileCommand &Cmd,
                     llvm::StringRef ResourceDir) {
  // Strip plugin related command line arguments. Clangd does
  // not support plugins currently. Therefore it breaks if
  // compiler tries to load plugins.
  Cmd.CommandLine =
      tooling::getStripPluginsAdjuster()(Cmd.CommandLine, Cmd.Filename);
  // Inject the resource dir.
  // FIXME: Don't overwrite it if it's already there.
  if (!ResourceDir.empty())
    Cmd.CommandLine.push_back(("-resource-dir=" + ResourceDir).str());
}

std::string getStandardResourceDir() {
  static int Dummy; // Just an address in this process.
  return CompilerInvocation::GetResourcesPath("clangd", (void *)&Dummy);
}

} // namespace

static std::string getFallbackClangPath() {
  static int Dummy;
  std::string ClangdExecutable =
      llvm::sys::fs::getMainExecutable("clangd", (void *)&Dummy);
  SmallString<128> ClangPath;
  ClangPath = llvm::sys::path::parent_path(ClangdExecutable);
  llvm::sys::path::append(ClangPath, "clang");
  return ClangPath.str();
}

tooling::CompileCommand
GlobalCompilationDatabase::getFallbackCommand(PathRef File) const {
  std::vector<std::string> Argv = {getFallbackClangPath()};
  // Clang treats .h files as C by default, resulting in unhelpful diagnostics.
  // Parsing as Objective C++ is friendly to more cases.
  if (llvm::sys::path::extension(File) == ".h")
    Argv.push_back("-xobjective-c++-header");
  Argv.push_back(File);
  return tooling::CompileCommand(llvm::sys::path::parent_path(File),
                                 llvm::sys::path::filename(File),
                                 std::move(Argv),
                                 /*Output=*/"");
}

DirectoryBasedGlobalCompilationDatabase::
    DirectoryBasedGlobalCompilationDatabase(
        llvm::Optional<Path> CompileCommandsDir)
    : CompileCommandsDir(std::move(CompileCommandsDir)) {}

DirectoryBasedGlobalCompilationDatabase::
    ~DirectoryBasedGlobalCompilationDatabase() = default;

llvm::Optional<tooling::CompileCommand>
DirectoryBasedGlobalCompilationDatabase::getCompileCommand(
    PathRef File, ProjectInfo *Project) const {
  if (auto CDB = getCDBForFile(File, Project)) {
    auto Candidates = CDB->getCompileCommands(File);
    if (!Candidates.empty()) {
      return std::move(Candidates.front());
    }
  } else {
    log("Failed to find compilation database for {0}", File);
  }
  return None;
}

std::pair<tooling::CompilationDatabase *, /*Cached*/ bool>
DirectoryBasedGlobalCompilationDatabase::getCDBInDirLocked(PathRef Dir) const {
  // FIXME(ibiryukov): Invalidate cached compilation databases on changes
  auto CachedIt = CompilationDatabases.find(Dir);
  if (CachedIt != CompilationDatabases.end())
    return {CachedIt->second.get(), true};
  std::string Error = "";
  auto CDB = tooling::CompilationDatabase::loadFromDirectory(Dir, Error);
  auto Result = CDB.get();
  CompilationDatabases.insert(std::make_pair(Dir, std::move(CDB)));
  return {Result, false};
}

tooling::CompilationDatabase *
DirectoryBasedGlobalCompilationDatabase::getCDBForFile(
    PathRef File, ProjectInfo *Project) const {
  namespace path = llvm::sys::path;
  assert((path::is_absolute(File, path::Style::posix) ||
          path::is_absolute(File, path::Style::windows)) &&
         "path must be absolute");

  tooling::CompilationDatabase *CDB = nullptr;
  bool Cached = false;
  std::lock_guard<std::mutex> Lock(Mutex);
  if (CompileCommandsDir) {
    std::tie(CDB, Cached) = getCDBInDirLocked(*CompileCommandsDir);
    if (Project && CDB)
      Project->SourceRoot = *CompileCommandsDir;
  } else {
    for (auto Path = path::parent_path(File); !CDB && !Path.empty();
         Path = path::parent_path(Path)) {
      std::tie(CDB, Cached) = getCDBInDirLocked(Path);
      if (Project && CDB)
        Project->SourceRoot = Path;
    }
  }
  // FIXME: getAllFiles() may return relative paths, we need absolute paths.
  // Hopefully the fix is to change JSONCompilationDatabase and the interface.
  if (CDB && !Cached)
    OnCommandChanged.broadcast(CDB->getAllFiles());
  return CDB;
}

GCCDirectoryBasedGlobalCompilationDatabase::
  GCCDirectoryBasedGlobalCompilationDatabase(
    llvm::Optional<Path> CompileCommandsDir)
  : DirectoryBasedGlobalCompilationDatabase(CompileCommandsDir) {}

std::string
GCCDirectoryBasedGlobalCompilationDatabase::getTarget(std::vector<std::string> CommandLine) const {
  StringRef CompileCommand(CommandLine.front());
  std::string RealCommand(CompileCommand);
  llvm::Regex QCCRegex("^(.*)(qcc|QCC)(.exe)?");
  llvm::SmallVector<StringRef, 3> QCCMatches;
  if (QCCRegex.match(CompileCommand, &QCCMatches)) {
    auto VargFind = std::find_if(CommandLine.begin() + 1, CommandLine.end(), [](std::string &arg) {
      return llvm::StringRef(arg).startswith("-V");
    });
    if (VargFind != CommandLine.end()) {
      llvm::Regex Regex("^-V((.*),)?gcc_(.*)$");
      llvm::SmallVector<StringRef, 5> Matches;
      if (Regex.match(*VargFind, &Matches)) {
        StringRef prefix = Matches[3];
        prefix.consume_back("le");
        prefix.consume_back("_cpp");
        prefix.consume_back("_gpp");

        RealCommand = QCCMatches[1];
        RealCommand += prefix;
        if (!Matches[2].empty()) {
          RealCommand += "-";
          RealCommand += Matches[2];
        }
        RealCommand += "-gcc";
      }
    }
  }

  llvm::SmallString<256> out;
  std::error_code err = llvm::sys::fs::createTemporaryFile("clangd_target", "txt", out);
  if (err) {
    llvm::errs() << "target: Error creating temp file: " << err.message() << "\n";
    return "";
  }
  llvm::FileRemover outRemover(out);

  Optional<StringRef> Redirects[] = {
    llvm::None,
    llvm::None,
    StringRef(out)
  };

  std::vector<llvm::StringRef> args;
  args.push_back(RealCommand);
  args.push_back("-v");

  std::string ErrMsg;
  int rc = llvm::sys::ExecuteAndWait(RealCommand, args, None, Redirects, 0, 0, &ErrMsg);
  if (rc) {
    llvm::errs() << "target: execution failed (" << rc << ") " << ErrMsg << "\n";
    return "";
  }

  auto targetBuffer = llvm::MemoryBuffer::getFile(out);
  if (!targetBuffer) {
    llvm::errs() << "target: error opening " << out << ":" << targetBuffer.getError().message() << "\n";
    return "";
  }

  for (llvm::line_iterator line(*targetBuffer.get()); !line.is_at_eof(); line++) {
    StringRef target = *line;
    if (target.consume_front("Target: ")) {
      return target;
    }
  }

  return "";
}

// Adds the -target flag
llvm::Optional<tooling::CompileCommand>
GCCDirectoryBasedGlobalCompilationDatabase::getCompileCommand(PathRef File, ProjectInfo *) const {
  auto command = DirectoryBasedGlobalCompilationDatabase::getCompileCommand(File);
  if (command != llvm::None) {
    auto compileCommand = command->CommandLine.front();
    auto targetFind = TargetMap.find(compileCommand);
    std::string target;
    if (targetFind == TargetMap.end()) {
      target = TargetMap.try_emplace(compileCommand, getTarget(command->CommandLine)).first->getValue();
    } else {
      target = targetFind->second;
    }

    if (!target.empty()) {
      command->CommandLine.insert(command->CommandLine.begin() + 1, target);
      command->CommandLine.insert(command->CommandLine.begin() + 1, "-target");
      LastTarget = target;
    }
  }
  return command;
}

tooling::CompileCommand
GCCDirectoryBasedGlobalCompilationDatabase::getFallbackCommand(PathRef File) const {
  auto command = DirectoryBasedGlobalCompilationDatabase::getFallbackCommand(File);
  if (!LastTarget.empty()) {
    auto compileCommand = command.CommandLine.front();
    command.CommandLine.insert(command.CommandLine.begin() + 1, LastTarget);
    command.CommandLine.insert(command.CommandLine.begin() + 1, "-target");
  }
  return command;
}

OverlayCDB::OverlayCDB(const GlobalCompilationDatabase *Base,
                       std::vector<std::string> FallbackFlags,
                       llvm::Optional<std::string> ResourceDir)
    : Base(Base), ResourceDir(ResourceDir ? std::move(*ResourceDir)
                                          : getStandardResourceDir()),
      FallbackFlags(std::move(FallbackFlags)) {
  if (Base)
    BaseChanged = Base->watch([this](const std::vector<std::string> Changes) {
      OnCommandChanged.broadcast(Changes);
    });
}

llvm::Optional<tooling::CompileCommand>
OverlayCDB::getCompileCommand(PathRef File, ProjectInfo *Project) const {
  llvm::Optional<tooling::CompileCommand> Cmd;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Commands.find(File);
    if (It != Commands.end()) {
      if (Project)
        Project->SourceRoot = "";
      Cmd = It->second;
    }
  }
  if (!Cmd && Base)
    Cmd = Base->getCompileCommand(File, Project);
  if (!Cmd)
    return llvm::None;
  adjustArguments(*Cmd, ResourceDir);
  return Cmd;
}

tooling::CompileCommand OverlayCDB::getFallbackCommand(PathRef File) const {
  auto Cmd = Base ? Base->getFallbackCommand(File)
                  : GlobalCompilationDatabase::getFallbackCommand(File);
  std::lock_guard<std::mutex> Lock(Mutex);
  Cmd.CommandLine.insert(Cmd.CommandLine.end(), FallbackFlags.begin(),
                         FallbackFlags.end());
  return Cmd;
}

void OverlayCDB::setCompileCommand(
    PathRef File, llvm::Optional<tooling::CompileCommand> Cmd) {
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    if (Cmd)
      Commands[File] = std::move(*Cmd);
    else
      Commands.erase(File);
  }
  OnCommandChanged.broadcast({File});
}

} // namespace clangd
} // namespace clang
