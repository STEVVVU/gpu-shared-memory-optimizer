; ModuleID = 'C:\Users\steve\Projects\gpu-shared-memory-optimizer\evidence\review-byte-gep-20261021\output-byte.ll'
source_filename = "C:\\Users\\steve\\Projects\\gpu-shared-memory-optimizer\\evidence\\review-byte-gep-20261021\\output-byte.ll"
target triple = "nvptx64-nvidia-cuda"

@p20.tile.p20_stencil = internal addrspace(3) global [130 x i32] undef, align 1

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
  %center = load i32, ptr addrspace(1) %center.ptr, align 1
  %1 = add i64 %lane.index, 1
  %center.slot = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %1
  store i32 %center, ptr addrspace(3) %center.slot, align 1
  %2 = icmp eq i32 %tid, 0
  br i1 %2, label %left.halo, label %check.right

left.halo:                                        ; preds = %entry
  %3 = sub i64 %global.index, 1
  %4 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %3
  %left.halo.value = load i32, ptr addrspace(1) %4, align 1
  store i32 %left.halo.value, ptr addrspace(3) @p20.tile.p20_stencil, align 1
  br label %check.right

check.right:                                      ; preds = %left.halo, %entry
  %5 = icmp eq i32 %tid, 127
  br i1 %5, label %right.halo, label %tile.ready

right.halo:                                       ; preds = %check.right
  %6 = add i64 %global.index, 1
  %7 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %6
  %right.halo.value = load i32, ptr addrspace(1) %7, align 1
  store i32 %right.halo.value, ptr addrspace(3) getelementptr inbounds ([130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 129), align 1
  br label %tile.ready

tile.ready:                                       ; preds = %right.halo, %check.right
  call void @llvm.nvvm.barrier.cta.sync.aligned.all(i32 0)
  %8 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %lane.index
  %9 = add i64 %lane.index, 1
  %10 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %9
  %11 = add i64 %lane.index, 2
  %12 = getelementptr inbounds [130 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %11
  %cached.left = load i32, ptr addrspace(3) %8, align 1
  %cached.center = load i32, ptr addrspace(3) %10, align 1
  %cached.right = load i32, ptr addrspace(3) %12, align 1
  %13 = add i32 %cached.left, %cached.center
  %stencil.sum = add i32 %13, %cached.right
  %14 = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %global.index
  store i32 %stencil.sum, ptr addrspace(1) %14, align 1
  ret void
}

; Function Attrs: convergent nocallback nounwind
declare void @llvm.nvvm.barrier.cta.sync.aligned.all(i32) #2

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32" }
attributes #2 = { convergent nocallback nounwind }
