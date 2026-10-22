#include "PrivateSharedSlotPromotion.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
bool isLaneId(Value *V) {
  if (auto *Cast = dyn_cast<ZExtInst>(V))
    V = Cast->getOperand(0);
  auto *Call = dyn_cast<CallBase>(V);
  Function *Callee = Call ? Call->getCalledFunction() : nullptr;
  return Callee && Callee->getName() == "llvm.nvvm.read.ptx.sreg.tid.x";
}

bool hasOnlyOneBlock(const Function &F) {
  return F.size() == 1;
}

bool dominatesInBlock(const Instruction *A, const Instruction *B) {
  if (A->getParent() != B->getParent())
    return false;
  for (const Instruction &I : *A->getParent()) {
    if (&I == A)
      return true;
    if (&I == B)
      return false;
  }
  return false;
}
} // namespace

PreservedAnalyses
PrivateSharedSlotPromotionPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() != 3 || !GV.hasInitializer())
      continue;
    auto *SharedArrayTy = dyn_cast<ArrayType>(GV.getValueType());
    if (!SharedArrayTy || SharedArrayTy->getNumElements() != 128 ||
        !SharedArrayTy->getElementType()->isIntegerTy(32))
      continue;
    auto *ArrayTy = dyn_cast<ArrayType>(GV.getValueType());
    if (!ArrayTy || ArrayTy->getNumElements() != 128 ||
        !ArrayTy->getElementType()->isIntegerTy(32))
      continue;

    GetElementPtrInst *Slot = nullptr;
    for (User *U : GV.users()) {
      auto *GEP = dyn_cast<GetElementPtrInst>(U);
      if (!GEP || Slot) {
        Slot = nullptr;
        break;
      }
      Slot = GEP;
    }
    if (!Slot || Slot->getNumIndices() != 2 ||
        Slot->getSourceElementType() != GV.getValueType() ||
        !isa<ConstantInt>(Slot->getOperand(1)) ||
        !cast<ConstantInt>(Slot->getOperand(1))->isZero() ||
        !isLaneId(Slot->getOperand(2))) {
      errs() << "p20-private-slot: rejected " << GV.getName()
             << " because it is not a single lane-indexed shared slot\n";
      continue;
    }

    StoreInst *Store = nullptr;
    LoadInst *Load = nullptr;
    bool ExtraUse = false;
    for (User *U : Slot->users()) {
      if (auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() != Slot || Store) {
          ExtraUse = true;
          break;
        }
        Store = SI;
      } else if (auto *LI = dyn_cast<LoadInst>(U)) {
        if (LI->getPointerOperand() != Slot || Load) {
          ExtraUse = true;
          break;
        }
        Load = LI;
      } else {
        ExtraUse = true;
        break;
      }
    }
    if (ExtraUse || !Store || !Load || Store->isVolatile() ||
        Load->isVolatile() || Store->isAtomic() || Load->isAtomic() ||
        Store->getValueOperand()->getType() != Load->getType() ||
        !isLaneId(Slot->getOperand(2)) ||
        !hasOnlyOneBlock(*Store->getFunction()) ||
        Store->getFunction()->getCallingConv() != CallingConv::PTX_Kernel ||
        Store->getFunction() != Load->getFunction() ||
        Store->getFunction()->getFnAttribute("nvvm.reqntid").getValueAsString() !=
            "128,1,1" ||
        !dominatesInBlock(Store, Load))
      continue;

    // Any calls between the store and load could publish or mutate the slot.
    bool InterveningEffect = false;
    for (Instruction *I = Store->getNextNode(); I && I != Load;
         I = I->getNextNode()) {
      if (auto *CB = dyn_cast<CallBase>(I)) {
        Function *Callee = CB->getCalledFunction();
        if (!Callee ||
            !Callee->getName().starts_with("llvm.nvvm.barrier.cta.sync.")) {
          InterveningEffect = true;
          break;
        }
      } else if (I->mayWriteToMemory()) {
        InterveningEffect = true;
        break;
      }
    }
    if (InterveningEffect)
      continue;

    const std::string FunctionName = Store->getFunction()->getName().str();
    Load->replaceAllUsesWith(Store->getValueOperand());
    Load->eraseFromParent();
    Store->eraseFromParent();
    Slot->eraseFromParent();
    if (GV.use_empty())
      GV.eraseFromParent();
    errs() << "p20-private-slot: promoted one thread-private shared slot in "
           << FunctionName << "\n";
    Changed = true;
    break;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
