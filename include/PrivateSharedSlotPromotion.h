#ifndef LLVM_TUTOR_PRIVATE_SHARED_SLOT_PROMOTION_H
#define LLVM_TUTOR_PRIVATE_SHARED_SLOT_PROMOTION_H

#include "llvm/IR/PassManager.h"

// Promotes one proven thread-private NVPTX shared slot to SSA. The pass is
// deliberately conservative: see the feature map for its accepted shape.
class PrivateSharedSlotPromotionPass
    : public llvm::PassInfoMixin<PrivateSharedSlotPromotionPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};

#endif
