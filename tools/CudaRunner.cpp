#include <cuda.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(CUresult Result, const char *What) {
  if (Result == CUDA_SUCCESS)
    return;
  const char *Name = nullptr;
  const char *Text = nullptr;
  cuGetErrorName(Result, &Name);
  cuGetErrorString(Result, &Text);
  throw std::runtime_error(std::string(What) + ": " +
                           (Name ? Name : "CUDA error") + " (" +
                           (Text ? Text : "unknown") + ")");
}

struct Context {
  CUdevice Device = 0;
  bool Retained = false;
  ~Context() { if (Retained) cuDevicePrimaryCtxRelease(Device); }
};
struct Module {
  CUmodule Value = nullptr;
  ~Module() { if (Value) cuModuleUnload(Value); }
};
struct DeviceBuffer {
  CUdeviceptr Value = 0;
  ~DeviceBuffer() { if (Value) cuMemFree(Value); }
};
struct Event {
  CUevent Value = nullptr;
  ~Event() { if (Value) cuEventDestroy(Value); }
};

std::string readText(const char *Path) {
  std::ifstream In(Path, std::ios::binary);
  if (!In)
    throw std::runtime_error(std::string("cannot read PTX: ") + Path);
  return {std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>()};
}

float measure(CUfunction Kernel, CUdeviceptr Input, CUdeviceptr Output,
              unsigned Blocks) {
  Event Start, Stop;
  check(cuEventCreate(&Start.Value, CU_EVENT_DEFAULT), "create start event");
  check(cuEventCreate(&Stop.Value, CU_EVENT_DEFAULT), "create stop event");
  void *Args[] = {&Input, &Output};
  check(cuEventRecord(Start.Value, nullptr), "record start event");
  for (unsigned I = 0; I < 64; ++I)
    check(cuLaunchKernel(Kernel, Blocks, 1, 1, 128, 1, 1, 0, nullptr, Args,
                         nullptr), "launch timed kernel");
  check(cuEventRecord(Stop.Value, nullptr), "record stop event");
  check(cuEventSynchronize(Stop.Value), "wait for timed kernel");
  float Milliseconds = 0;
  check(cuEventElapsedTime(&Milliseconds, Start.Value, Stop.Value),
        "read event duration");
  return Milliseconds / 64.0f;
}

std::vector<int> runCase(CUfunction Kernel, CUdeviceptr DeviceBase,
                         CUdeviceptr Input,
                         CUdeviceptr Output, const std::vector<int> &Host,
                         unsigned OutputCount, unsigned Blocks) {
  check(cuMemcpyHtoD(DeviceBase, Host.data(), Host.size() * sizeof(int)),
        "copy input to GPU");
  void *Args[] = {&Input, &Output};
  check(cuLaunchKernel(Kernel, Blocks, 1, 1, 128, 1, 1, 0, nullptr, Args,
                       nullptr), "launch correctness kernel");
  check(cuCtxSynchronize(), "wait for correctness kernel");
  std::vector<int> Actual(OutputCount);
  check(cuMemcpyDtoH(Actual.data(), Output, Actual.size() * sizeof(int)),
        "copy output from GPU");
  return Actual;
}
} // namespace

int main(int Argc, char **Argv) {
  if (Argc != 4 || (std::string(Argv[3]) != "copy" &&
                    std::string(Argv[3]) != "stencil")) {
    std::cerr << "usage: cuda-runner <original.ptx> <promoted.ptx> <copy|stencil>\n";
    return 2;
  }
  try {
    check(cuInit(0), "initialize CUDA");
    Context Ctx;
    check(cuDeviceGet(&Ctx.Device, 0), "select CUDA device");
    CUcontext Primary;
    check(cuDevicePrimaryCtxRetain(&Primary, Ctx.Device),
          "get primary context handle");
    Ctx.Retained = true;
    check(cuCtxSetCurrent(Primary), "set current CUDA context");
    const std::string OriginalText = readText(Argv[1]);
    const std::string PromotedText = readText(Argv[2]);
    Module Original, Promoted;
    check(cuModuleLoadData(&Original.Value, OriginalText.c_str()),
          "load original PTX");
    check(cuModuleLoadData(&Promoted.Value, PromotedText.c_str()),
          "load promoted PTX");
    CUfunction OriginalKernel, PromotedKernel;
    const bool Stencil = std::string(Argv[3]) == "stencil";
    const char *KernelName = Stencil ? "p20_stencil" : "p20_private_slot";
    check(cuModuleGetFunction(&OriginalKernel, Original.Value, KernelName),
          "find original kernel");
    check(cuModuleGetFunction(&PromotedKernel, Promoted.Value, KernelName),
          "find promoted kernel");

    for (unsigned Blocks : {1u, 3u, 17u}) {
      const unsigned Count = Blocks * 128;
      std::vector<int> Input(Count + (Stencil ? 2 : 0));
      for (size_t I = 0; I < Input.size(); ++I)
        Input[I] = static_cast<int>((I * 7919u) % 200003u) - 100001;
      std::vector<int> Expected(Count);
      if (Stencil) {
        for (unsigned I = 0; I < Count; ++I)
          Expected[I] = Input[I] + Input[I + 1] + Input[I + 2];
      } else {
        std::copy_n(Input.begin(), Count, Expected.begin());
      }
      DeviceBuffer DIn, DOut;
      check(cuMemAlloc(&DIn.Value, Input.size() * sizeof(int)),
            "allocate input");
      check(cuMemAlloc(&DOut.Value, Count * sizeof(int)),
            "allocate output");
      CUdeviceptr KernelInput = DIn.Value + (Stencil ? sizeof(int) : 0);

      auto OriginalResult = runCase(OriginalKernel, DIn.Value, KernelInput, DOut.Value,
                                    Input, Count, Blocks);
      std::cout << "blocks=" << Blocks << " original execution complete\n";
      auto PromotedResult = runCase(PromotedKernel, DIn.Value, KernelInput, DOut.Value,
                                    Input, Count, Blocks);
      std::cout << "blocks=" << Blocks << " promoted execution complete\n";
      if (OriginalResult != Expected || PromotedResult != Expected ||
          OriginalResult != PromotedResult)
        throw std::runtime_error("GPU output differs from independent CPU oracle");

      // Warm both variants, then alternate batches to limit drift bias.
      (void)runCase(OriginalKernel, DIn.Value, KernelInput, DOut.Value, Input, Count, Blocks);
      (void)runCase(PromotedKernel, DIn.Value, KernelInput, DOut.Value, Input, Count, Blocks);
      float OriginalMs = 0, PromotedMs = 0;
      for (unsigned Round = 0; Round < 5; ++Round) {
        if (Round % 2 == 0) {
          OriginalMs += measure(OriginalKernel, KernelInput, DOut.Value, Blocks);
          PromotedMs += measure(PromotedKernel, KernelInput, DOut.Value, Blocks);
        } else {
          PromotedMs += measure(PromotedKernel, KernelInput, DOut.Value, Blocks);
          OriginalMs += measure(OriginalKernel, KernelInput, DOut.Value, Blocks);
        }
      }
      std::cout << "mode=" << Argv[3] << " blocks=" << Blocks << " elements=" << Count
                << " oracle=PASS original_us=" << (OriginalMs * 200.0f)
                << " promoted_us=" << (PromotedMs * 200.0f) << '\n';
    }
  } catch (const std::exception &E) {
    std::cerr << "cuda-runner: " << E.what() << '\n';
    return 1;
  }
  return 0;
}
