#include <cuda.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Mode {
  const char *Name;
  unsigned Radius;
  const char *Kernel;
};
constexpr Mode Modes[] = {{"copy", 0, "p20_private_slot"},
                          {"stencil", 1, "p20_stencil"},
                          {"stencil-r4", 4, "p20_stencil"},
                          {"stencil-r8", 8, "p20_stencil"},
                          {"stencil-r16", 16, "p20_stencil"}};

std::vector<uint32_t> oracle(const std::vector<uint32_t> &Input,
                             unsigned Count, unsigned Radius) {
  if (Input.size() != Count + 2 * Radius)
    throw std::runtime_error("CPU oracle input/halo size mismatch");
  std::vector<uint32_t> Expected(Count);
  for (unsigned I = 0; I < Count; ++I)
    for (unsigned D = 0; D <= 2 * Radius; ++D)
      Expected[I] += Input[I + D];
  return Expected;
}

std::vector<uint32_t> makeInput(unsigned Count, unsigned Radius, unsigned Pattern) {
  std::vector<uint32_t> Input(Count + 2 * Radius);
  uint32_t State = 0x7193a5cdu;
  for (size_t I = 0; I < Input.size(); ++I) {
    if (Pattern == 0)
      Input[I] = static_cast<uint32_t>(static_cast<int>((I * 7919u) % 200003u) - 100001);
    else if (Pattern == 1) {
      State ^= State << 13;
      State ^= State >> 17;
      State ^= State << 5;
      Input[I] = State;
    } else
      Input[I] = I % 2 ? 0xffffffffu : 0x80000000u;
  }
  if (Pattern == 2)
    for (unsigned I = 0; I < Radius; ++I) {
      Input[I] = 0xa1000000u + I * 17;
      Input[Count + Radius + I] = 0xb2000000u + I * 31;
    }
  return Input;
}

void selfTest() {
  if (oracle({0xffffffffu, 2, 0x80000000u}, 1, 1) !=
          std::vector<uint32_t>{0x80000001u} ||
      oracle({7, 9}, 2, 0) != std::vector<uint32_t>{7, 9})
    throw std::runtime_error("CPU modulo-i32 oracle self-check failed");
  for (const Mode &M : Modes) {
    for (unsigned Pattern = 0; Pattern < 3; ++Pattern) {
      auto Input = makeInput(128, M.Radius, Pattern);
      auto Expected = oracle(Input, 128, M.Radius);
      if (Expected.size() != 128 || (M.Radius && Pattern == 2 &&
          Input.front() == Input.back()))
        throw std::runtime_error("CPU fixture self-check failed");
    }
  }
  std::cout << "PASS fixed modes, uint32 wrapping oracle and distinct halos\n";
}

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

std::vector<uint32_t> runCase(CUfunction Kernel, CUdeviceptr DeviceBase,
                         CUdeviceptr Input,
                         CUdeviceptr Output, const std::vector<uint32_t> &Host,
                         unsigned OutputCount, unsigned Blocks) {
  check(cuMemcpyHtoD(DeviceBase, Host.data(), Host.size() * sizeof(uint32_t)),
        "copy input to GPU");
  check(cuMemsetD32(Output, 0xdeadbeef, OutputCount), "poison correctness output");
  void *Args[] = {&Input, &Output};
  check(cuLaunchKernel(Kernel, Blocks, 1, 1, 128, 1, 1, 0, nullptr, Args,
                       nullptr), "launch correctness kernel");
  check(cuCtxSynchronize(), "wait for correctness kernel");
  std::vector<uint32_t> Actual(OutputCount);
  check(cuMemcpyDtoH(Actual.data(), Output, Actual.size() * sizeof(uint32_t)),
        "copy output from GPU");
  return Actual;
}

double transaction(CUfunction Kernel, const std::vector<uint32_t> &Input,
                   unsigned Radius, unsigned Blocks,
                   std::vector<uint32_t> &Actual) {
  DeviceBuffer DIn, DOut;
  auto Start = std::chrono::steady_clock::now();
  check(cuMemAlloc(&DIn.Value, Input.size() * sizeof(uint32_t)), "transaction allocate input");
  check(cuMemAlloc(&DOut.Value, Actual.size() * sizeof(uint32_t)), "transaction allocate output");
  check(cuMemcpyHtoD(DIn.Value, Input.data(), Input.size() * sizeof(uint32_t)), "transaction H2D");
  check(cuMemsetD32(DOut.Value, 0xdeadbeef, Actual.size()), "transaction poison output");
  CUdeviceptr KernelInput = DIn.Value + Radius * sizeof(uint32_t);
  void *Args[] = {&KernelInput, &DOut.Value};
  check(cuLaunchKernel(Kernel, Blocks, 1, 1, 128, 1, 1, 0, nullptr, Args, nullptr), "transaction launch");
  check(cuCtxSynchronize(), "transaction synchronize");
  check(cuMemcpyDtoH(Actual.data(), DOut.Value, Actual.size() * sizeof(uint32_t)), "transaction D2H");
  check(cuMemFree(DOut.Value), "transaction free output");
  DOut.Value = 0;
  check(cuMemFree(DIn.Value), "transaction free input");
  DIn.Value = 0;
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Start).count();
}
} // namespace

int main(int Argc, char **Argv) {
  if (Argc == 2 && std::string(Argv[1]) == "--self-test") {
    try { selfTest(); } catch (const std::exception &E) {
      std::cerr << E.what() << '\n';
      return 1;
    }
    return 0;
  }
  const bool Benchmark = Argc == 5 && std::string(Argv[4]) == "--benchmark";
  const bool Smoke = Argc == 5 && std::string(Argv[4]) == "--smoke";
  const Mode *Selected = nullptr;
  if (Argc >= 4)
    for (const Mode &M : Modes)
      if (std::string(Argv[3]) == M.Name)
        Selected = &M;
  if ((Argc != 4 && !Benchmark && !Smoke) || !Selected) {
    std::cerr << "usage: cuda-runner <original.ptx> <promoted.ptx> <copy|stencil|stencil-r4|stencil-r8|stencil-r16> [--benchmark|--smoke]\n";
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
    const unsigned Radius = Selected->Radius;
    const char *KernelName = Selected->Kernel;
    check(cuModuleGetFunction(&OriginalKernel, Original.Value, KernelName),
          "find original kernel");
    check(cuModuleGetFunction(&PromotedKernel, Promoted.Value, KernelName),
          "find promoted kernel");

    const std::vector<unsigned> Sizes = Benchmark ? std::vector<unsigned>{128, 4096, 32768, 131072} : std::vector<unsigned>{1, 3, 17};
    for (unsigned Blocks : Sizes) {
      const unsigned Count = Blocks * 128;
      std::vector<uint32_t> Input = makeInput(Count, Radius, 0);
      std::vector<uint32_t> Expected = oracle(Input, Count, Radius);
      DeviceBuffer DIn, DOut;
      check(cuMemAlloc(&DIn.Value, Input.size() * sizeof(uint32_t)),
            "allocate input");
      check(cuMemAlloc(&DOut.Value, Count * sizeof(uint32_t)),
            "allocate output");
      CUdeviceptr KernelInput = DIn.Value + Radius * sizeof(uint32_t);
      std::cout << "allocation_bytes=" << (Input.size() + Count) * sizeof(uint32_t) << '\n';

      if (!Benchmark) {
        for (unsigned Pattern = 0; Pattern < 3; ++Pattern) {
          auto TestInput = makeInput(Count, Radius, Pattern);
          auto TestExpected = oracle(TestInput, Count, Radius);
          for (auto Entry : {std::make_pair("original", OriginalKernel), std::make_pair("promoted", PromotedKernel)}) {
            if (runCase(Entry.second, DIn.Value, KernelInput, DOut.Value, TestInput, Count, Blocks) != TestExpected)
              throw std::runtime_error("GPU edge-case output differs from independent uint32 CPU oracle");
            std::cout << "correctness mode=" << Selected->Name << " blocks=" << Blocks << " pattern=" << Pattern
                      << " side=" << Entry.first << " poisoned=1 oracle=PASS\n";
          }
        }
      }

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
      std::vector<uint32_t> WarmOutput(Count);
      for (CUfunction Kernel : {OriginalKernel, PromotedKernel}) {
        (void)transaction(Kernel, Input, Radius, Blocks, WarmOutput);
        if (WarmOutput != Expected) throw std::runtime_error("warm transaction oracle mismatch");
      }
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
          std::vector<uint32_t> Actual(Count);
          check(cuMemcpyDtoH(Actual.data(), DOut.Value, Count * sizeof(uint32_t)), "read timed output");
          if (Actual != Expected) throw std::runtime_error("timed output differs from independent CPU oracle");
          Samples[Side].push_back(Ms * 1000);
          std::cout << "sample mode=" << Argv[3] << " blocks=" << Blocks << " elements=" << Count << " repeat=" << Round
                    << " side=" << (Side ? "promoted" : "original") << " event_us=" << Ms * 1000 << " host_batch_us_per_call=" << WallUs
                    << " logical_effective_GBps=" << (double(Count) * 2 * sizeof(uint32_t)) / (Ms * 1e6)
                    << " finished=64 failed=0 oracle=PASS\n";
          double TransactionUs = transaction(Kernel, Input, Radius, Blocks, Actual);
          if (Actual != Expected) throw std::runtime_error("transaction output differs from independent CPU oracle");
          std::cout << "transaction mode=" << Selected->Name << " elements=" << Count << " repeat=" << Round
                    << " side=" << (Side ? "promoted" : "original") << " host_transaction_us=" << TransactionUs
                    << " finished=1 failed=0 oracle=PASS\n";
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
