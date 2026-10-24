; ModuleID = 'evidence\benchmark-stencil-radius-20261024-b\R1.original.ll'
source_filename = "evidence\\benchmark-stencil-radius-20261024-b\\R1.original.ll"
target triple = "nvptx64-nvidia-cuda"

@p20.tile.p20_stencil = internal addrspace(3) global [130 x i32] undef, align 4

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 1024) i32 @llvm.nvvm.read.ptx.sreg.tid.x() #0

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 2147483647) i32 @llvm.nvvm.read.ptx.sreg.ctaid.x() #0

define ptx_kernel void @p20_stencil(ptr addrspace(1) noalias readonly %input, ptr addrspace(1) noalias writeonly %output) #1 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %cta = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %0 = mul i32 %cta, 128
  %base32 = add i32 %0, %tid
  %global.index = zext i32 %base32 to i64
  %lane.index = zext i32 %tid to i64
  %center.ptr = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %global.index
  %center = load i32, ptr addrspace(1) %center.ptr, align 4
  %1 = add i64 %lane.index, 1
  %center.slot = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %1
  store i32 %center, ptr addrspace(3) %center.slot, align 4
  %2 = icmp ult i32 %tid, 1
  br i1 %2, label %left.halo, label %check.right

left.halo:                                        ; preds = %entry
  %3 = sub i64 %global.index, 1
  %4 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %3
  %left.halo.value = load i32, ptr addrspace(1) %4, align 4
  %5 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %lane.index
  store i32 %left.halo.value, ptr addrspace(3) %5, align 4
  br label %check.right

check.right:                                      ; preds = %left.halo, %entry
  %6 = icmp uge i32 %tid, 127
  br i1 %6, label %right.halo, label %tile.ready

right.halo:                                       ; preds = %check.right
  %7 = add i64 %global.index, 1
  %8 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %7
  %right.halo.value = load i32, ptr addrspace(1) %8, align 4
  %9 = add i64 %lane.index, 2
  %10 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %9
  store i32 %right.halo.value, ptr addrspace(3) %10, align 4
  br label %tile.ready

tile.ready:                                       ; preds = %right.halo, %check.right
  call void @llvm.nvvm.barrier.cta.sync.aligned.all(i32 0)
  %11 = add i64 %lane.index, 0
  %12 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %11
  %cached.neighbor = load i32, ptr addrspace(3) %12, align 4
  %stencil.sum = add i32 %center, %cached.neighbor
  %13 = add i64 %lane.index, 2
  %14 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %13
  %cached.neighbor1 = load i32, ptr addrspace(3) %14, align 4
  %stencil.sum2 = add i32 %stencil.sum, %cached.neighbor1
  %15 = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %global.index
  store i32 %stencil.sum2, ptr addrspace(1) %15, align 4
  ret void
}

; Function Attrs: convergent nocallback nounwind
declare void @llvm.nvvm.barrier.cta.sync.aligned.all(i32) #2

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32" }
attributes #2 = { convergent nocallback nounwind }
