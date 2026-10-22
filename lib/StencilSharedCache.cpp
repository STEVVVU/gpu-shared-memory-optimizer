#include "StencilSharedCache.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

using namespace llvm;

namespace {
constexpr StringLiteral Contract =
    "one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32";

bool isCallNamed(Value *V, StringRef Name) {
  auto *CB = dyn_cast<CallBase>(V);
  Function *Callee = CB ? CB->getCalledFunction() : nullptr;
  return Callee && Callee->getName() == Name;
}

bool isConstant(Value *V, int64_t N) {
  auto *CI = dyn_cast<ConstantInt>(V);
  return CI && CI->getSExtValue() == N;
}

bool isIndexOffset(Value *V, Value *Base, int Offset) {
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (!BO)
    return false;
  if (Offset == -1)
    return BO->getOpcode() == Instruction::Sub &&
           BO->getOperand(0) == Base && isConstant(BO->getOperand(1), 1);
  return BO->getOpcode() == Instruction::Add &&
         ((BO->getOperand(0) == Base && isConstant(BO->getOperand(1), Offset)) ||
          (BO->getOperand(1) == Base && isConstant(BO->getOperand(0), Offset)));
}

bool collectLoads(Value *V, SmallVectorImpl<LoadInst *> &Found) {
  if (auto *LI = dyn_cast<LoadInst>(V)) {
    Found.push_back(LI);
    return true;
  }
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (!BO || BO->getOpcode() != Instruction::Add || BO->hasNoSignedWrap() ||
      BO->hasNoUnsignedWrap())
    return false;
  return collectLoads(BO->getOperand(0), Found) &&
         collectLoads(BO->getOperand(1), Found);
}

bool isSupported(Function &F, Argument *&Input, Argument *&Output,
                 Value *&GlobalIndex, Value *&Lane,
                 SmallVectorImpl<LoadInst *> &Loads) {
  if (F.isDeclaration() || F.size() != 1 || F.arg_size() != 2 ||
      F.getCallingConv() != CallingConv::PTX_Kernel ||
      F.getFnAttribute("nvvm.reqntid").getValueAsString() != "128,1,1" ||
      F.getFnAttribute("p20.stencil-contract").getValueAsString() != Contract ||
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

  Value *Tid = nullptr;
  Value *Cta = nullptr;
  StoreInst *OutputStore = nullptr;
  for (Instruction &I : F.front()) {
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      if (isCallNamed(CB, "llvm.nvvm.read.ptx.sreg.tid.x"))
        Tid = CB;
      else if (isCallNamed(CB, "llvm.nvvm.read.ptx.sreg.ctaid.x"))
        Cta = CB;
      else
        return false;
    } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (LI->getType()->isIntegerTy(32) &&
          LI->getPointerAddressSpace() == 1 && !LI->isVolatile() &&
          !LI->isAtomic())
        Loads.push_back(LI);
      else
        return false;
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (OutputStore || SI->getPointerAddressSpace() != 1 ||
          SI->isVolatile() || SI->isAtomic())
        return false;
      OutputStore = SI;
    } else if (isa<GetElementPtrInst>(I) || isa<ZExtInst>(I) ||
               isa<BinaryOperator>(I) || isa<ReturnInst>(I)) {
      continue;
    } else {
      return false;
    }
  }
  if (!Tid || !Cta || Loads.size() != 3 || !OutputStore ||
      !OutputStore->getValueOperand()->getType()->isIntegerTy(32))
    return false;

  // The three source loads must address idx-1, idx, idx+1 from one affine
  // block/lane index, and the output must be the sum of exactly those loads.
  SmallVector<LoadInst *, 3> SumLoads;
  if (!collectLoads(OutputStore->getValueOperand(), SumLoads) ||
      SumLoads.size() != 3 ||
      !llvm::all_of(Loads, [&](LoadInst *LI) {
        return llvm::count(SumLoads, LI) == 1;
      }))
    return false;

  auto getInputIndex = [&](LoadInst *LI) -> Value * {
    auto *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand());
    if (!GEP || GEP->getPointerOperand() != Input || GEP->getNumIndices() != 1)
      return nullptr;
    if (!GEP->getSourceElementType()->isIntegerTy(32))
      return nullptr;
    return GEP->idx_end()[-1];
  };
  Value *Base = nullptr;
  for (LoadInst *LI : Loads) {
    Value *Index = getInputIndex(LI);
    if (!Index)
      return false;
    auto *Z = dyn_cast<ZExtInst>(Index);
    if (Z && !isIndexOffset(Index, nullptr, 0)) {
      auto *BO = dyn_cast<BinaryOperator>(Z->getOperand(0));
      if (BO && BO->getOpcode() == Instruction::Add) {
        for (Value *Operand : {BO->getOperand(0), BO->getOperand(1)}) {
          if (Operand != Tid && Base == nullptr) {
            auto *Mul = dyn_cast<BinaryOperator>(Operand);
            if (Mul && Mul->getOpcode() == Instruction::Mul &&
                ((Mul->getOperand(0) == Cta && isConstant(Mul->getOperand(1), 128)) ||
                 (Mul->getOperand(1) == Cta && isConstant(Mul->getOperand(0), 128))))
              Base = Index;
          }
        }
      }
    }
  }
  if (!Base)
    return false;
  unsigned Direct = 0, Left = 0, Right = 0;
  for (LoadInst *LI : Loads) {
    Value *Index = getInputIndex(LI);
    if (Index == Base)
      ++Direct;
    else if (isIndexOffset(Index, Base, -1))
      ++Left;
    else if (isIndexOffset(Index, Base, 1))
      ++Right;
    else
      return false;
  }
  if (Direct != 1 || Left != 1 || Right != 1)
    return false;
  auto *OutGEP = dyn_cast<GetElementPtrInst>(OutputStore->getPointerOperand());
  if (!OutGEP || OutGEP->getPointerOperand() != Output ||
      OutGEP->getNumIndices() != 1 ||
      !OutGEP->getSourceElementType()->isIntegerTy(32) ||
      OutGEP->idx_end()[-1] != Base)
    return false;
  GlobalIndex = Base;
  Lane = Tid;
  return true;
}
} // namespace

PreservedAnalyses StencilSharedCachePass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (!F.hasFnAttribute("p20.stencil-contract"))
      continue;
    Argument *Input = nullptr, *Output = nullptr;
    Value *OldIndex = nullptr, *Lane = nullptr;
    SmallVector<LoadInst *, 3> OldLoads;
    if (!isSupported(F, Input, Output, OldIndex, Lane, OldLoads)) {
      errs() << "p20-stencil-cache: rejected unsupported shape in "
             << F.getName() << "\n";
      continue;
    }
    auto *I32 = Type::getInt32Ty(M.getContext());
    auto *I64 = Type::getInt64Ty(M.getContext());
    auto *TileTy = ArrayType::get(I32, 130);
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
        {B.getInt64(0), B.CreateAdd(Lane64, B.getInt64(1))}, "center.slot");
    B.CreateStore(Center, CenterSlot);
    B.CreateCondBr(B.CreateICmpEQ(Tid, B.getInt32(0)), LoadLeft, CheckRight);

    B.SetInsertPoint(LoadLeft);
    Value *LeftIndex64 = B.CreateSub(Base64, B.getInt64(1));
    Value *LeftPtr = B.CreateInBoundsGEP(I32, Input, LeftIndex64);
    Value *Left = B.CreateLoad(I32, LeftPtr, "left.halo.value");
    B.CreateStore(Left, B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), B.getInt64(0)}));
    B.CreateBr(CheckRight);

    B.SetInsertPoint(CheckRight);
    B.CreateCondBr(B.CreateICmpEQ(Tid, B.getInt32(127)), LoadRight, Join);

    B.SetInsertPoint(LoadRight);
    Value *RightIndex64 = B.CreateAdd(Base64, B.getInt64(1));
    Value *RightPtr = B.CreateInBoundsGEP(I32, Input, RightIndex64);
    Value *Right = B.CreateLoad(I32, RightPtr, "right.halo.value");
    B.CreateStore(Right, B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), B.getInt64(129)}));
    B.CreateBr(Join);

    B.SetInsertPoint(Join);
    B.CreateCall(BarrierFn, {B.getInt32(0)});
    Value *LeftSlot = B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), Lane64});
    Value *RightSlot = B.CreateInBoundsGEP(TileTy, Tile,
        {B.getInt64(0), B.CreateAdd(Lane64, B.getInt64(2))});
    Value *LV = B.CreateLoad(I32, LeftSlot, "cached.left");
    Value *RV = B.CreateLoad(I32, RightSlot, "cached.right");
    Value *Sum = B.CreateAdd(B.CreateAdd(LV, Center), RV, "stencil.sum");
    B.CreateStore(Sum, B.CreateInBoundsGEP(I32, Output, Base64));
    B.CreateRetVoid();
    errs() << "p20-stencil-cache: cached a 128-lane stencil tile in "
           << F.getName() << "\n";
    Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
