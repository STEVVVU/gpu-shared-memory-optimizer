#include "PrivateSharedSlotPromotion.h"
#include "StencilSharedCache.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::OptionCategory Category("P20 shared-memory optimizer");
static cl::opt<std::string> Input(cl::Positional, cl::Required,
                                  cl::desc("input LLVM IR"),
                                  cl::cat(Category));
static cl::opt<std::string> Output("o", cl::Required,
                                   cl::desc("output LLVM IR"),
                                   cl::cat(Category));

int main(int Argc, char **Argv) {
  cl::HideUnrelatedOptions(Category);
  cl::ParseCommandLineOptions(Argc, Argv, "bounded NVPTX shared-slot promotion\n");
  llvm_shutdown_obj Shutdown;
  LLVMContext Context;
  SMDiagnostic Diagnostic;
  std::unique_ptr<Module> M = parseIRFile(Input, Diagnostic, Context);
  if (!M) {
    Diagnostic.print(Argv[0], errs());
    return 2;
  }
  if (verifyModule(*M, &errs()))
    return 3;

  ModuleAnalysisManager MAM;
  ModulePassManager MPM;
  MPM.addPass(PrivateSharedSlotPromotionPass());
  MPM.addPass(StencilSharedCachePass());
  PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  MPM.run(*M, MAM);
  if (verifyModule(*M, &errs()))
    return 4;

  std::error_code EC;
  raw_fd_ostream OS(Output, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "cannot open output: " << EC.message() << "\n";
    return 5;
  }
  M->print(OS, nullptr);
  return 0;
}
