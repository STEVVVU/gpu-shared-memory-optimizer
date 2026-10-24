target triple = "nvptx64-nvidia-cuda"

declare i32 @llvm.nvvm.read.ptx.sreg.tid.x()
declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()

define ptx_kernel void @p20_stencil(ptr addrspace(1) noalias readonly %input, ptr addrspace(1) noalias writeonly %output) #0 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %bid = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %block.base = mul i32 %bid, 128
  %index = add i32 %block.base, %tid
  %index64 = zext i32 %index to i64
  %offset.m8 = sub i64 %index64, 8
  %ptr.m8 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m8
  %value.m8 = load i32, ptr addrspace(1) %ptr.m8, align 4
  %offset.m7 = sub i64 %index64, 7
  %ptr.m7 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m7
  %value.m7 = load i32, ptr addrspace(1) %ptr.m7, align 4
  %offset.m6 = sub i64 %index64, 6
  %ptr.m6 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m6
  %value.m6 = load i32, ptr addrspace(1) %ptr.m6, align 4
  %offset.m5 = sub i64 %index64, 5
  %ptr.m5 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.m5
  %value.m5 = load i32, ptr addrspace(1) %ptr.m5, align 4
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
  %offset.p5 = add i64 %index64, 5
  %ptr.p5 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p5
  %value.p5 = load i32, ptr addrspace(1) %ptr.p5, align 4
  %offset.p6 = add i64 %index64, 6
  %ptr.p6 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p6
  %value.p6 = load i32, ptr addrspace(1) %ptr.p6, align 4
  %offset.p7 = add i64 %index64, 7
  %ptr.p7 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p7
  %value.p7 = load i32, ptr addrspace(1) %ptr.p7, align 4
  %offset.p8 = add i64 %index64, 8
  %ptr.p8 = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %offset.p8
  %value.p8 = load i32, ptr addrspace(1) %ptr.p8, align 4
  %sum.1 = add i32 %value.m8, %value.m7
  %sum.2 = add i32 %sum.1, %value.m6
  %sum.3 = add i32 %sum.2, %value.m5
  %sum.4 = add i32 %sum.3, %value.m4
  %sum.5 = add i32 %sum.4, %value.m3
  %sum.6 = add i32 %sum.5, %value.m2
  %sum.7 = add i32 %sum.6, %value.m1
  %sum.8 = add i32 %sum.7, %value.p0
  %sum.9 = add i32 %sum.8, %value.p1
  %sum.10 = add i32 %sum.9, %value.p2
  %sum.11 = add i32 %sum.10, %value.p3
  %sum.12 = add i32 %sum.11, %value.p4
  %sum.13 = add i32 %sum.12, %value.p5
  %sum.14 = add i32 %sum.13, %value.p6
  %sum.15 = add i32 %sum.14, %value.p7
  %sum.16 = add i32 %sum.15, %value.p8
  %output.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %index64
  store i32 %sum.16, ptr addrspace(1) %output.ptr, align 4
  ret void
}

attributes #0 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-17point;128-threads;multiple-of-128-elements;index-fits-u32" }
