; ModuleID = 'C:\Users\steve\Projects\gpu-shared-memory-optimizer\test\Inputs\P20PrivateSlot.ll'
source_filename = "C:\\Users\\steve\\Projects\\gpu-shared-memory-optimizer\\test\\Inputs\\P20PrivateSlot.ll"
target triple = "nvptx64-nvidia-cuda"

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 1024) i32 @llvm.nvvm.read.ptx.sreg.tid.x() #0

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare noundef range(i32 0, 2147483647) i32 @llvm.nvvm.read.ptx.sreg.ctaid.x() #0

define ptx_kernel void @p20_private_slot(ptr addrspace(1) %input, ptr addrspace(1) %output) #1 {
entry:
  %tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %bid = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %base = mul i32 %bid, 128
  %idx = add i32 %base, %tid
  %idx64 = zext i32 %idx to i64
  %lane64 = zext i32 %tid to i64
  %in.ptr = getelementptr inbounds i32, ptr addrspace(1) %input, i64 %idx64
  %value = load i32, ptr addrspace(1) %in.ptr, align 4
  call void @llvm.nvvm.barrier.cta.sync.aligned.all(i32 0)
  %out.ptr = getelementptr inbounds i32, ptr addrspace(1) %output, i64 %idx64
  store i32 %value, ptr addrspace(1) %out.ptr, align 4
  ret void
}

; Function Attrs: convergent nocallback nounwind
declare void @llvm.nvvm.barrier.cta.sync.aligned.all(i32) #2

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { "nvvm.kernel" "nvvm.reqntid"="128,1,1" }
attributes #2 = { convergent nocallback nounwind }
