; ModuleID = 'evidence\benchmark-stencil-radius-20261024-b\R8.original.ll'
source_filename = "evidence\\benchmark-stencil-radius-20261024-b\\R8.original.ll"
target triple = "nvptx64-nvidia-cuda"

@p20.tile.p20_stencil = internal addrspace(3) global [144 x i32] undef, align 4

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
  %1 = add i64 %lane.index, 8
  %center.slot = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %1
  store i32 %center, ptr addrspace(3) %center.slot, align 4
  %2 = icmp ult i32 %tid, 8
  br i1 %2, label %left.halo, label %check.right

left.halo:                                        ; preds = %entry
  %3 = sub i64 %global.index, 8
  %4 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %3
  %left.halo.value = load i32, ptr addrspace(1) %4, align 4
  %5 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %lane.index
  store i32 %left.halo.value, ptr addrspace(3) %5, align 4
  br label %check.right

check.right:                                      ; preds = %left.halo, %entry
  %6 = icmp uge i32 %tid, 120
  br i1 %6, label %right.halo, label %tile.ready

right.halo:                                       ; preds = %check.right
  %7 = add i64 %global.index, 8
  %8 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %7
  %right.halo.value = load i32, ptr addrspace(1) %8, align 4
  %9 = add i64 %lane.index, 16
  %10 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %9
  store i32 %right.halo.value, ptr addrspace(3) %10, align 4
  br label %tile.ready

tile.ready:                                       ; preds = %right.halo, %check.right
  call void @llvm.nvvm.barrier.cta.sync.aligned.all(i32 0)
  %11 = add i64 %lane.index, 0
  %12 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %11
  %cached.neighbor = load i32, ptr addrspace(3) %12, align 4
  %stencil.sum = add i32 %center, %cached.neighbor
  %13 = add i64 %lane.index, 1
  %14 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %13
  %cached.neighbor1 = load i32, ptr addrspace(3) %14, align 4
  %stencil.sum2 = add i32 %stencil.sum, %cached.neighbor1
  %15 = add i64 %lane.index, 2
  %16 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %15
  %cached.neighbor3 = load i32, ptr addrspace(3) %16, align 4
  %stencil.sum4 = add i32 %stencil.sum2, %cached.neighbor3
  %17 = add i64 %lane.index, 3
  %18 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %17
  %cached.neighbor5 = load i32, ptr addrspace(3) %18, align 4
  %stencil.sum6 = add i32 %stencil.sum4, %cached.neighbor5
  %19 = add i64 %lane.index, 4
  %20 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %19
  %cached.neighbor7 = load i32, ptr addrspace(3) %20, align 4
  %stencil.sum8 = add i32 %stencil.sum6, %cached.neighbor7
  %21 = add i64 %lane.index, 5
  %22 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %21
  %cached.neighbor9 = load i32, ptr addrspace(3) %22, align 4
  %stencil.sum10 = add i32 %stencil.sum8, %cached.neighbor9
  %23 = add i64 %lane.index, 6
  %24 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %23
  %cached.neighbor11 = load i32, ptr addrspace(3) %24, align 4
  %stencil.sum12 = add i32 %stencil.sum10, %cached.neighbor11
  %25 = add i64 %lane.index, 7
  %26 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %25
  %cached.neighbor13 = load i32, ptr addrspace(3) %26, align 4
  %stencil.sum14 = add i32 %stencil.sum12, %cached.neighbor13
  %27 = add i64 %lane.index, 9
  %28 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %27
  %cached.neighbor15 = load i32, ptr addrspace(3) %28, align 4
  %stencil.sum16 = add i32 %stencil.sum14, %cached.neighbor15
  %29 = add i64 %lane.index, 10
  %30 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %29
  %cached.neighbor17 = load i32, ptr addrspace(3) %30, align 4
  %stencil.sum18 = add i32 %stencil.sum16, %cached.neighbor17
  %31 = add i64 %lane.index, 11
  %32 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %31
  %cached.neighbor19 = load i32, ptr addrspace(3) %32, align 4
  %stencil.sum20 = add i32 %stencil.sum18, %cached.neighbor19
  %33 = add i64 %lane.index, 12
  %34 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %33
  %cached.neighbor21 = load i32, ptr addrspace(3) %34, align 4
  %stencil.sum22 = add i32 %stencil.sum20, %cached.neighbor21
  %35 = add i64 %lane.index, 13
  %36 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %35
  %cached.neighbor23 = load i32, ptr addrspace(3) %36, align 4
  %stencil.sum24 = add i32 %stencil.sum22, %cached.neighbor23
  %37 = add i64 %lane.index, 14
  %38 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %37
  %cached.neighbor25 = load i32, ptr addrspace(3) %38, align 4
  %stencil.sum26 = add i32 %stencil.sum24, %cached.neighbor25
  %39 = add i64 %lane.index, 15
  %40 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %39
  %cached.neighbor27 = load i32, ptr addrspace(3) %40, align 4
  %stencil.sum28 = add i32 %stencil.sum26, %cached.neighbor27
  %41 = add i64 %lane.index, 16
  %42 = getelementptr inbounds [144 x i32], ptr addrspace(3) @p20.tile.p20_stencil, i64 0, i64 %41
  %cached.neighbor29 = load i32, ptr addrspace(3) %42, align 4
  %stencil.sum30 = add i32 %stencil.sum28, %cached.neighbor29
  %43 = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %global.index
  store i32 %stencil.sum30, ptr addrspace(1) %43, align 4
  ret void
}

; Function Attrs: convergent nocallback nounwind
declare void @llvm.nvvm.barrier.cta.sync.aligned.all(i32) #2

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-17point;128-threads;multiple-of-128-elements;index-fits-u32" }
attributes #2 = { convergent nocallback nounwind }
