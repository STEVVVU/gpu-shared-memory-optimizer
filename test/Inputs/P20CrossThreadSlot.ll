target triple = "nvptx64-nvidia-cuda"

@slot = internal addrspace(3) global [128 x i32] undef, align 4

declare i32 @llvm.nvvm.read.ptx.sreg.tid.x()
declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
declare void @llvm.nvvm.barrier0()

define ptx_kernel void @p20_cross_thread_slot(ptr addrspace(1) %input, ptr addrspace(1) %output) #0 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %bid = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %base = mul i32 %bid, 128
  %idx = add i32 %base, %tid
  %idx64 = zext i32 %idx to i64
  %lane64 = zext i32 %tid to i64
  %in.ptr = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %idx64
  %value = load i32, ptr addrspace(1) %in.ptr, align 4
  %slot.ptr = getelementptr inbounds [128 x i32], ptr addrspace(3) @slot, i64 0, i64 %lane64
  store i32 %value, ptr addrspace(3) %slot.ptr, align 4
  call void @llvm.nvvm.barrier0()
  %neighbor = xor i64 %lane64, 1
  %neighbor.ptr = getelementptr inbounds [128 x i32], ptr addrspace(3) @slot, i64 0, i64 %neighbor
  %again = load i32, ptr addrspace(3) %neighbor.ptr, align 4
  %out.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %idx64
  store i32 %again, ptr addrspace(1) %out.ptr, align 4
  ret void
}

attributes #0 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" }
