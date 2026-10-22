target triple = "nvptx64-nvidia-cuda"

@slot = internal addrspace(3) global [128 x i32] undef, align 4

declare i32 @llvm.nvvm.read.ptx.sreg.tid.x()
declare void @llvm.nvvm.barrier0()

define ptx_kernel void @p20_private_slot(ptr addrspace(1) %input, ptr addrspace(1) %output) #0 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %idx64 = zext i32 %tid to i64
  %in.ptr = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %idx64
  %value = load i32, ptr addrspace(1) %in.ptr, align 4
  %slot.ptr = getelementptr inbounds [128 x i8], ptr addrspace(3) @slot, i64 0, i64 %idx64
  store i32 %value, ptr addrspace(3) %slot.ptr, align 1
  call void @llvm.nvvm.barrier0()
  %again = load i32, ptr addrspace(3) %slot.ptr, align 1
  %out.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %idx64
  store i32 %again, ptr addrspace(1) %out.ptr, align 4
  ret void
}

attributes #0 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" }
