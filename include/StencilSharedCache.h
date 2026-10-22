#ifndef LLVM_TUTOR_STENCIL_SHARED_CACHE_H
#define LLVM_TUTOR_STENCIL_SHARED_CACHE_H

#include "llvm/IR/PassManager.h"

// Caches a validated, one-dimensional three-point stencil tile in CTA shared
// memory. The accepted launch/padding contract is documented in the feature map.
class StencilSharedCachePass
    : public llvm::PassInfoMixin<StencilSharedCachePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};

#endif
