#include "StencilSharedCache.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

using namespace llvm;

namespace {
unsigned contractRadius(StringRef Contract) {
  if (Contract == "one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32")
    return 1;
  if (Contract == "one-dimensional-padded-i32-9point;128-threads;multiple-of-128-elements;index-fits-u32")
    return 4;
  if (Contract == "one-dimensional-padded-i32-17point;128-threads;multiple-of-128-elements;index-fits-u32")
    return 8;
  if (Contract == "one-dimensional-padded-i32-33point;128-threads;multiple-of-128-elements;index-fits-u32")
    return 16;
  return 0;
}

bool isCallNamed(Value *V, StringRef Name) {
  auto *CI = dyn_cast<CallInst>(V);
  Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee && Callee->getName() == Name && CI->arg_empty() &&
         !CI->hasOperandBundles() && CI->getType()->isIntegerTy(32);
}

bool isConstant(Value *V, int64_t N) {
  auto *CI = dyn_cast<ConstantInt>(V);
  return CI && CI->getSExtValue() == N;
}

bool isIndexOffset(Value *V, Value *Base, int Offset) {
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (!BO || !BO->getType()->isIntegerTy(64) || BO->hasPoisonGeneratingFlags())
    return false;
  if (Offset < 0 && BO->getOpcode() == Instruction::Sub)
    return BO->getOperand(0) == Base && isConstant(BO->getOperand(1), -Offset);
  return BO->getOpcode() == Instruction::Add &&
         ((BO->getOperand(0) == Base && isConstant(BO->getOperand(1), Offset)) ||
          (BO->getOperand(1) == Base && isConstant(BO->getOperand(0), Offset)));
}

bool collectLoads(Value *V, SmallVectorImpl<LoadInst *> &Found,
                  std::set<Instruction *> &Matched) {
  if (auto *LI = dyn_cast<LoadInst>(V)) {
    Found.push_back(LI);
    Matched.insert(LI);
    return true;
  }
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (!BO || BO->getOpcode() != Instruction::Add ||
      !BO->getType()->isIntegerTy(32) || BO->hasPoisonGeneratingFlags())
    return false;
  Matched.insert(BO);
  return collectLoads(BO->getOperand(0), Found, Matched) &&
         collectLoads(BO->getOperand(1), Found, Matched);
}

bool isSupported(Function &F, Argument *&Input, Argument *&Output,
                 unsigned Radius) {
  if (!Radius || F.isDeclaration() || F.size() != 1 || F.arg_size() != 2 ||
      F.isVarArg() || !F.getReturnType()->isVoidTy() ||
      F.getCallingConv() != CallingConv::PTX_Kernel ||
      F.hasFnAttribute(Attribute::NoSync) ||
      F.getMemoryEffects() != MemoryEffects::unknown() ||
      F.getFnAttribute("nvvm.reqntid").getValueAsString() != "128,1,1" ||
      !F.getArg(0)->getType()->isPointerTy() ||
      !F.getArg(1)->getType()->isPointerTy() ||
      cast<PointerType>(F.getArg(0)->getType())->getAddressSpace() != 1 ||
      cast<PointerType>(F.getArg(1)->getType())->getAddressSpace() != 1 ||
      !F.hasParamAttribute(0, Attribute::NoAlias) ||
      !F.hasParamAttribute(0, Attribute::ReadOnly) ||
      !F.hasParamAttribute(1, Attribute::NoAlias) ||
      !F.hasParamAttribute(1, Attribute::WriteOnly))
    return false;
  Input = F.getArg(0);
  Output = F.getArg(1);

  Value *Tid = nullptr, *Cta = nullptr;
  StoreInst *OutputStore = nullptr;
  SmallVector<LoadInst *, 33> Loads;
  std::set<Instruction *> Matched;
  for (Instruction &I : F.front()) {
    if (isa<CallBase>(I)) {
      if (!Tid && isCallNamed(&I, "llvm.nvvm.read.ptx.sreg.tid.x"))
        Tid = &I;
      else if (!Cta && isCallNamed(&I, "llvm.nvvm.read.ptx.sreg.ctaid.x"))
        Cta = &I;
      else
        return false;
      Matched.insert(&I);
    } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->getType()->isIntegerTy(32) || LI->getPointerAddressSpace() != 1 ||
          LI->isVolatile() || LI->isAtomic() || LI->getAlign() < Align(4))
        return false;
      Loads.push_back(LI);
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (OutputStore || SI->getPointerAddressSpace() != 1 ||
          SI->isVolatile() || SI->isAtomic() || SI->getAlign() < Align(4))
        return false;
      OutputStore = SI;
      Matched.insert(SI);
    } else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
      if (RI->getReturnValue())
        return false;
      Matched.insert(RI);
    } else if (!isa<GetElementPtrInst>(I) && !isa<ZExtInst>(I) &&
               !isa<BinaryOperator>(I)) {
      return false;
    }
  }
  if (!Tid || !Cta || Loads.size() != 2 * Radius + 1 || !OutputStore ||
      !OutputStore->getValueOperand()->getType()->isIntegerTy(32))
    return false;

  // Anchor every source offset on the output's actual block-plus-lane index.
  auto *OutGEP = dyn_cast<GetElementPtrInst>(OutputStore->getPointerOperand());
  if (!OutGEP || OutGEP->getPointerOperand() != Output ||
      OutGEP->getNumIndices() != 1 ||
      !OutGEP->getSourceElementType()->isIntegerTy(32))
    return false;
  Value *Base = OutGEP->idx_end()[-1];
  auto *Z = dyn_cast<ZExtInst>(Base);
  auto *Index = Z ? dyn_cast<BinaryOperator>(Z->getOperand(0)) : nullptr;
  if (!Z || !Z->getType()->isIntegerTy(64) || Z->hasPoisonGeneratingFlags() ||
      !Index || !Index->getType()->isIntegerTy(32) ||
      Index->getOpcode() != Instruction::Add || Index->hasPoisonGeneratingFlags())
    return false;
  Value *BlockBase = nullptr;
  if (Index->getOperand(0) == Tid)
    BlockBase = Index->getOperand(1);
  else if (Index->getOperand(1) == Tid)
    BlockBase = Index->getOperand(0);
  auto *Mul = dyn_cast_or_null<BinaryOperator>(BlockBase);
  if (!Mul || Mul->getOpcode() != Instruction::Mul || Mul->hasPoisonGeneratingFlags() ||
      !((Mul->getOperand(0) == Cta && isConstant(Mul->getOperand(1), 128)) ||
        (Mul->getOperand(1) == Cta && isConstant(Mul->getOperand(0), 128))))
    return false;
  Matched.insert({OutGEP, Z, Index, Mul});

  SmallVector<LoadInst *, 33> SumLoads;
  if (!collectLoads(OutputStore->getValueOperand(), SumLoads, Matched) ||
      SumLoads.size() != Loads.size() ||
      !llvm::all_of(Loads, [&](LoadInst *LI) {
        return llvm::count(SumLoads, LI) == 1;
      }))
    return false;
  std::set<int> Offsets;
  for (LoadInst *LI : Loads) {
    auto *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand());
    if (!GEP || GEP->getPointerOperand() != Input || GEP->getNumIndices() != 1 ||
        !GEP->getSourceElementType()->isIntegerTy(32))
      return false;
    Value *SourceIndex = GEP->idx_end()[-1];
    int Offset = -static_cast<int>(Radius);
    for (; Offset <= static_cast<int>(Radius); ++Offset)
      if ((Offset == 0 && SourceIndex == Base) ||
          (Offset != 0 && isIndexOffset(SourceIndex, Base, Offset)))
        break;
    if (Offset > static_cast<int>(Radius) || !Offsets.insert(Offset).second)
      return false;
    Matched.insert(GEP);
    if (SourceIndex != Base)
      Matched.insert(cast<Instruction>(SourceIndex));
  }
  return llvm::all_of(F.front(), [&](Instruction &I) { return Matched.count(&I); });
}
} // namespace

PreservedAnalyses StencilSharedCachePass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (!F.hasFnAttribute("p20.stencil-contract"))
      continue;
    Argument *Input = nullptr, *Output = nullptr;
    unsigned Radius = contractRadius(F.getFnAttribute("p20.stencil-contract").getValueAsString());
    if (!isSupported(F, Input, Output, Radius)) {
      errs() << "p20-stencil-cache: rejected unsupported shape in "
             << F.getName() << "\n";
      continue;
    }
    auto *I32 = Type::getInt32Ty(M.getContext());
    auto *I64 = Type::getInt64Ty(M.getContext());
    auto *TileTy = ArrayType::get(I32, 128 + 2 * Radius);
    std::string TileName = ("p20.tile." + F.getName()).str();
    if (M.getNamedValue(TileName)) {
      errs() << "p20-stencil-cache: rejected duplicate tile symbol " << TileName
             << "\n";
      continue;
    }
    auto *Tile = new GlobalVariable(M, TileTy, false,
        GlobalValue::InternalLinkage, UndefValue::get(TileTy), TileName, nullptr,
        GlobalVariable::NotThreadLocal, 3);
    Tile->setAlignment(Align(4));
    auto *TidFn = M.getFunction("llvm.nvvm.read.ptx.sreg.tid.x");
    auto *CtaFn = M.getFunction("llvm.nvvm.read.ptx.sreg.ctaid.x");
    if (!TidFn || !CtaFn) {
      Tile->eraseFromParent();
      continue;
    }
    FunctionType *BarrierTy = FunctionType::get(Type::getVoidTy(M.getContext()),
                                                {I32}, false);
    Function *BarrierFn = cast<Function>(M.getOrInsertFunction(
        "llvm.nvvm.barrier.cta.sync.aligned.all", BarrierTy).getCallee());

    F.deleteBody();
    BasicBlock *Entry = BasicBlock::Create(M.getContext(), "entry", &F);
    BasicBlock *LoadLeft = BasicBlock::Create(M.getContext(), "left.halo", &F);
    BasicBlock *CheckRight = BasicBlock::Create(M.getContext(), "check.right", &F);
    BasicBlock *LoadRight = BasicBlock::Create(M.getContext(), "right.halo", &F);
    BasicBlock *Join = BasicBlock::Create(M.getContext(), "tile.ready", &F);
    IRBuilder<> B(Entry);
    Value *Tid = B.CreateCall(TidFn, {}, "tid");
    Value *Cta = B.CreateCall(CtaFn, {}, "cta");
    Value *Base32 = B.CreateAdd(B.CreateMul(Cta, B.getInt32(128)), Tid, "base32");
    Value *Base64 = B.CreateZExt(Base32, I64, "global.index");
    Value *Lane64 = B.CreateZExt(Tid, I64, "lane.index");
    Value *CenterPtr = B.CreateInBoundsGEP(I32, Input, Base64, "center.ptr");
    Value *Center = B.CreateLoad(I32, CenterPtr, "center");
    Value *CenterSlot = B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), B.CreateAdd(Lane64, B.getInt64(Radius))}, "center.slot");
    B.CreateStore(Center, CenterSlot);
    B.CreateCondBr(B.CreateICmpULT(Tid, B.getInt32(Radius)), LoadLeft, CheckRight);

    B.SetInsertPoint(LoadLeft);
    Value *LeftIndex64 = B.CreateSub(Base64, B.getInt64(Radius));
    Value *LeftPtr = B.CreateInBoundsGEP(I32, Input, LeftIndex64);
    Value *Left = B.CreateLoad(I32, LeftPtr, "left.halo.value");
    B.CreateStore(Left, B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), Lane64}));
    B.CreateBr(CheckRight);

    B.SetInsertPoint(CheckRight);
    B.CreateCondBr(B.CreateICmpUGE(Tid, B.getInt32(128 - Radius)), LoadRight, Join);

    B.SetInsertPoint(LoadRight);
    Value *RightIndex64 = B.CreateAdd(Base64, B.getInt64(Radius));
    Value *RightPtr = B.CreateInBoundsGEP(I32, Input, RightIndex64);
    Value *Right = B.CreateLoad(I32, RightPtr, "right.halo.value");
    B.CreateStore(Right, B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), B.CreateAdd(Lane64, B.getInt64(2 * Radius))}));
    B.CreateBr(Join);

    B.SetInsertPoint(Join);
    B.CreateCall(BarrierFn, {B.getInt32(0)});
    Value *Sum = Center;
    for (int D = -static_cast<int>(Radius); D <= static_cast<int>(Radius); ++D) {
      if (D == 0)
        continue;
      Value *Slot = B.CreateInBoundsGEP(TileTy, Tile,
          {B.getInt64(0), B.CreateAdd(Lane64, B.getInt64(static_cast<int>(Radius) + D))});
      Sum = B.CreateAdd(Sum, B.CreateLoad(I32, Slot, "cached.neighbor"), "stencil.sum");
    }
    B.CreateStore(Sum, B.CreateInBoundsGEP(I32, Output, Base64));
    B.CreateRetVoid();
    errs() << "p20-stencil-cache: cached a 128-lane stencil tile radius=" << Radius
           << " in " << F.getName() << "\n";
    Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
