target triple = "nvptx64-nvidia-cuda"

declare i32 @llvm.nvvm.read.ptx.sreg.tid.x()
declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
declare void @llvm.nvvm.barrier0()

define ptx_kernel void @p20_stencil(ptr addrspace(1) noalias readonly %input, ptr addrspace(1) noalias writeonly %output) #0 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %bid = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %block.base = mul i32 %bid, 128
  %index = add i32 %block.base, %tid
  %index64 = zext i32 %index to i64
  %left.index = sub i64 %index64, 1
  %right.index = add i64 %index64, 1
  %left.ptr = getelementptr inbounds i8, ptr addrspace(1) %input, i64 %left.index
  %center.ptr = getelementptr inbounds i8, ptr addrspace(1) %input, i64 %index64
  %right.ptr = getelementptr inbounds i8, ptr addrspace(1) %input, i64 %right.index
  %left = load i32, ptr addrspace(1) %left.ptr, align 1
  %center = load i32, ptr addrspace(1) %center.ptr, align 1
  %right = load i32, ptr addrspace(1) %right.ptr, align 1
  %partial = add i32 %left, %center
  %sum = add i32 %partial, %right
  %output.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %index64
  store i32 %sum, ptr addrspace(1) %output.ptr, align 4
  ret void
}

attributes #0 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" "p20.stencil-contract"="one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32" }
