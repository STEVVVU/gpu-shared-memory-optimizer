#include <cuda.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
  const bool Benchmark = Argc == 5 && std::string(Argv[4]) == "--benchmark";
  const bool Smoke = Argc == 5 && std::string(Argv[4]) == "--smoke";
  if ((Argc != 4 && !Benchmark && !Smoke) || (std::string(Argv[3]) != "copy" &&
                    std::string(Argv[3]) != "stencil")) {
    std::cerr << "usage: cuda-runner <original.ptx> <promoted.ptx> <copy|stencil> [--benchmark|--smoke]\n";
    return 2;
  }
  try {
    check(cuInit(0), "initialize CUDA");
    Context Ctx;
    check(cuDeviceGet(&Ctx.Device, 0), "select CUDA device");
    char DeviceName[256];
    check(cuDeviceGetName(DeviceName, sizeof(DeviceName), Ctx.Device), "read device name");
    int DriverVersion = 0, Multiprocessors = 0, MaxThreads = 0;
    check(cuDriverGetVersion(&DriverVersion), "read driver version");
    check(cuDeviceGetAttribute(&Multiprocessors, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, Ctx.Device), "read SM count");
    check(cuDeviceGetAttribute(&MaxThreads, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR, Ctx.Device), "read SM thread limit");
    std::cout << "device=" << DeviceName << " driver=" << DriverVersion << " sms=" << Multiprocessors << '\n';
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

    const std::vector<unsigned> Sizes = Benchmark ? std::vector<unsigned>{128, 4096, 32768, 131072} : std::vector<unsigned>{1, 3, 17};
    for (unsigned Blocks : Sizes) {
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
      std::cout << "allocation_bytes=" << (Input.size() + Count) * sizeof(int) << '\n';

      auto OriginalResult = runCase(OriginalKernel, DIn.Value, KernelInput, DOut.Value,
                                    Input, Count, Blocks);
      std::cout << "blocks=" << Blocks << " original execution complete\n";
      auto PromotedResult = runCase(PromotedKernel, DIn.Value, KernelInput, DOut.Value,
                                    Input, Count, Blocks);
      std::cout << "blocks=" << Blocks << " promoted execution complete\n";
      if (OriginalResult != Expected || PromotedResult != Expected ||
          OriginalResult != PromotedResult)
        throw std::runtime_error("GPU output differs from independent CPU oracle");
      if (Smoke) {
        std::cout << "mode=" << Argv[3] << " elements=" << Count << " oracle=PASS timing=disabled\n";
        continue;
      }

      // Warm both variants, then alternate batches to limit drift bias.
      (void)runCase(OriginalKernel, DIn.Value, KernelInput, DOut.Value, Input, Count, Blocks);
      (void)runCase(PromotedKernel, DIn.Value, KernelInput, DOut.Value, Input, Count, Blocks);
      for (auto Entry : {std::make_pair("original", OriginalKernel), std::make_pair("promoted", PromotedKernel)}) {
        int ActiveBlocks = 0, Registers = 0, SharedBytes = 0;
        check(cuOccupancyMaxActiveBlocksPerMultiprocessor(&ActiveBlocks, Entry.second, 128, 0), "read theoretical occupancy");
        check(cuFuncGetAttribute(&Registers, CU_FUNC_ATTRIBUTE_NUM_REGS, Entry.second), "read registers");
        check(cuFuncGetAttribute(&SharedBytes, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, Entry.second), "read static shared memory");
        std::cout << "resources side=" << Entry.first << " registers_per_thread=" << Registers << " shared_bytes_per_cta=" << SharedBytes
                  << " theoretical_occupancy=" << double(ActiveBlocks * 128) / MaxThreads << '\n';
      }
      std::vector<float> Samples[2];
      for (unsigned Round = 0; Round < 7; ++Round) {
        for (unsigned Position = 0; Position < 2; ++Position) {
          unsigned Side = (Round + Position) % 2;
          CUfunction Kernel = Side ? PromotedKernel : OriginalKernel;
          check(cuMemsetD32(DOut.Value, 0xdeadbeef, Count), "poison timed output");
          auto StartWall = std::chrono::steady_clock::now();
          float Ms = measure(Kernel, KernelInput, DOut.Value, Blocks);
          double WallUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - StartWall).count() / 64;
          std::vector<int> Actual(Count);
          check(cuMemcpyDtoH(Actual.data(), DOut.Value, Count * sizeof(int)), "read timed output");
          if (Actual != Expected) throw std::runtime_error("timed output differs from independent CPU oracle");
          Samples[Side].push_back(Ms * 1000);
          std::cout << "sample mode=" << Argv[3] << " blocks=" << Blocks << " elements=" << Count << " repeat=" << Round
                    << " side=" << (Side ? "promoted" : "original") << " event_us=" << Ms * 1000 << " host_batch_us_per_call=" << WallUs
                    << " logical_effective_GBps=" << (double(Count) * 2 * sizeof(int)) / (Ms * 1e6)
                    << " finished=64 failed=0 oracle=PASS\n";
        }
      }
      for (unsigned Side = 0; Side < 2; ++Side) {
        auto Values = Samples[Side];
        std::sort(Values.begin(), Values.end());
        std::cout << "summary mode=" << Argv[3] << " elements=" << Count << " side=" << (Side ? "promoted" : "original")
                  << " repeats=7 median_us=" << Values[3] << " min_us=" << Values.front() << " max_us=" << Values.back()
                  << " p95_us=" << Values[6] << " bandwidth_scope=logical_input_plus_output_not_measured_DRAM\n";
      }
    }
  } catch (const std::exception &E) {
    std::cerr << "cuda-runner: " << E.what() << '\n';
    return 1;
  }
  return 0;
}
