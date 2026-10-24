; ModuleID = 'C:\Users\steve\.codex\orchestrate\resume-strength30-20261024\p20-function-attributes-red.ll'
source_filename = "C:\\Users\\steve\\.codex\\orchestrate\\resume-strength30-20261024\\p20-function-attributes-red.ll"
target triple = "nvptx64-nvidia-cuda"

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 1024) i32 @llvm.nvvm.read.ptx.sreg.tid.x() #0

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 2147483647) i32 @llvm.nvvm.read.ptx.sreg.ctaid.x() #0

; Function Attrs: nosync memory(argmem: readwrite)
define ptx_kernel void @p20_stencil(ptr addrspace(1) noalias readonly %input, ptr addrspace(1) noalias writeonly %output) #1 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %bid = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %block.base = mul i32 %bid, 128
  %index = add i32 %block.base, %tid
  %index64 = zext i32 %index to i64
  %offset.m4 = sub i64 %index64, 4
  %ptr.m4 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m4
  %value.m4 = load i32, ptr addrspace(1) %ptr.m4, align 4
  %offset.m3 = sub i64 %index64, 3
  %ptr.m3 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m3
  %value.m3 = load i32, ptr addrspace(1) %ptr.m3, align 4
  %offset.m2 = sub i64 %index64, 2
  %ptr.m2 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m2
  %value.m2 = load i32, ptr addrspace(1) %ptr.m2, align 4
  %offset.m1 = sub i64 %index64, 1
  %ptr.m1 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m1
  %value.m1 = load i32, ptr addrspace(1) %ptr.m1, align 4
  %ptr.p0 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %index64
  %value.p0 = load i32, ptr addrspace(1) %ptr.p0, align 4
  %offset.p1 = add i64 %index64, 1
  %ptr.p1 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p1
  %value.p1 = load i32, ptr addrspace(1) %ptr.p1, align 4
  %offset.p2 = add i64 %index64, 2
  %ptr.p2 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p2
  %value.p2 = load i32, ptr addrspace(1) %ptr.p2, align 4
  %offset.p3 = add i64 %index64, 3
  %ptr.p3 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p3
  %value.p3 = load i32, ptr addrspace(1) %ptr.p3, align 4
  %offset.p4 = add i64 %index64, 4
  %ptr.p4 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p4
  %value.p4 = load i32, ptr addrspace(1) %ptr.p4, align 4
  %sum.1 = add i32 %value.m4, %value.m3
  %sum.2 = add i32 %sum.1, %value.m2
  %sum.3 = add i32 %sum.2, %value.m1
  %sum.4 = add i32 %sum.3, %value.p0
  %sum.5 = add i32 %sum.4, %value.p1
  %sum.6 = add i32 %sum.5, %value.p2
  %sum.7 = add i32 %sum.6, %value.p3
  %sum.8 = add i32 %sum.7, %value.p4
  %output.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %index64
  store i32 %sum.8, ptr addrspace(1) %output.ptr, align 4
  ret void
}

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { nosync memory(argmem: readwrite) "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-9point;128-threads;multiple-of-128-elements;index-fits-u32" }
