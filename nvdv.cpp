#include <signal.h>
#include <windows.h>
#include "nvapi/nvapi.h"
#include "CLI11.hpp"
#include "nvdv.hpp"

static constexpr int ABORT_SIGNALS[11]{
  NVAPI_ERROR,
  SIGABRT,
  SIGABRT_COMPAT,
  SIGBREAK,
  SIGFPE,
  SIGILL,
  SIGINT,
  SIGSEGV,
  SIGTERM,
  WM_CLOSE,
  WM_QUIT,
};

#pragma warning(suppress: 4702)  // unreachable
static NvAPI_Status reject(const char* reason) {
  std::cerr << "Error: " << reason << std::endl;
  throw std::runtime_error(reason);
  return NVAPI_ERROR;
}

/** @returns number of detected connected displays */
static std::size_t get_display_count() {
  static int count = NULL;
  if (!count) EnumDisplayMonitors(NULL, NULL, [](HMONITOR__*, HDC__*, tagRECT*, long long) { return ++count; }, NULL);
  if (!count) return reject("Unable to display count");
  return count;
}

/** @returns 1-based index of primary display */
static std::size_t get_primary_display() {
  DISPLAY_DEVICE dd{};
  dd.cb = sizeof(dd);
  for (DWORD i = 0; EnumDisplayDevices(NULL, i, &dd, NULL); ++i) {
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) return i + 1;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
  }

  return reject("Unable to get primary display");
}

struct NVAPI {
  typedef NvAPI_Status (*NvAPI_Initialize_t)();
  static constexpr std::intptr_t INITIALIZE = 0x0150e828;

  typedef std::intptr_t (*NvAPI_QueryInterface_t)(...);
  NvAPI_QueryInterface_t ${nullptr};

  const HMODULE instance{nullptr};
  ~NVAPI() { instance&& FreeLibrary(instance); }
  NVAPI() : instance(LoadLibrary("nvapi64.dll")) {
    if (!instance) reject("Failed to load nvapi64.dll");
    $ = (NvAPI_QueryInterface_t)GetProcAddress(instance, "nvapi_QueryInterface");
    if (!$) reject("Failed to load `nvapi_QueryInterface`");
    static const NvAPI_Initialize_t& init{(NvAPI_Initialize_t)($)(NVAPI::INITIALIZE)};
    if (!init || (*init)() != NVAPI_OK) reject("Failed to initialize NvAPI");
  }
};

struct DVC {
 private:
  // clang-format off
  struct DVC_INFO { const NvU32 _{sizeof(DVC_INFO) | 0x10000}; const NvU32 cur; const NvU32 min; const NvU32 max; };  // clang-format on

  typedef NvAPI_Status (*NvAPI_EnumNvidiaDisplayHandle_t)(std::size_t display, NvDisplayHandle* handle);
  static constexpr std::intptr_t ENUM_NVIDIA_DISPLAY_HANDLE = 0x9abdd40d;

  typedef NvAPI_Status (*NvAPI_GetDVCInfo_t)(NvDisplayHandle handle, std::size_t display, DVC_INFO* info);
  static constexpr std::intptr_t GET_DVC_INFO = 0x4085de45;  // undocumented

  typedef NvAPI_Status (*NvAPI_SetDVCLevel_t)(NvDisplayHandle handle, std::size_t display, NvU32 value);
  static constexpr std::intptr_t SET_DVC_LEVEL = 0x172409b4;  // undocumented

  NvDisplayHandle handle{nullptr};
  NvAPI_SetDVCLevel_t nvapi_SetDVCLevel{nullptr};

 public:
  DVC_INFO info{};
  const std::size_t display;
  DVC(const NVAPI& nvapi, const std::size_t n) : display(n) {
    static const NvAPI_GetDVCInfo_t& get_dvc_info{(NvAPI_GetDVCInfo_t)(*nvapi.$)(DVC::GET_DVC_INFO)};
    static const NvAPI_EnumNvidiaDisplayHandle_t& enum_display_handle{
      (NvAPI_EnumNvidiaDisplayHandle_t)(*nvapi.$)(DVC::ENUM_NVIDIA_DISPLAY_HANDLE)
    };

    if (!get_dvc_info) reject("Failed to load `nvapi_GetDVCInfo`");
    if (!enum_display_handle) reject("Failed to load `nvapi_EnumNvidiaDisplayHandle`");
    if ((*enum_display_handle)(display - 1, &handle) != NVAPI_OK) reject("Failed to get display handle");
    if ((*get_dvc_info)(handle, NULL, &info) != NVAPI_OK) reject("Failed to get DVC info");
    if (!(nvapi_SetDVCLevel = (NvAPI_SetDVCLevel_t)(*nvapi.$)(DVC::SET_DVC_LEVEL))) {
      reject("Failed to load `nvapi_SetDVCLevel`");
    }
  }

  NvU32 raw_to_percent(const NvU32 value) const noexcept {
    const double total = static_cast<double>(info.max - info.min);
    const double decimal = static_cast<double>(value - info.min) / total;
    return static_cast<NvU32>(std::round(decimal * 100.0));
  }

  NvU32 percent_to_raw(const NvU32 value) const noexcept {
    const double total = static_cast<double>(info.max - info.min);
    const double decimal = static_cast<double>(value) / 100.0;
    return static_cast<NvU32>(std::round((decimal * total) + info.min));
  }

  NvAPI_Status set_raw(const NvU32 value) const {
    if (value < info.min || value > info.max) return reject("Value out of range");
    else if (value != info.cur && (*nvapi_SetDVCLevel)(handle, NULL, value) != NVAPI_OK) {
      return reject("Failed to set the digital vibrance");
    }

    return NVAPI_OK;
  }

  NvAPI_Status set(const NvU32 percentage) const {
    const NvU32 value = percent_to_raw(percentage);
    return set_raw(value);
  }
};

// app context
namespace nvdv {
  static HANDLE handle{nullptr};
  static const std::size_t display_count = get_display_count();
  static const std::size_t primary_display = get_primary_display();
  static const std::unique_ptr<NVAPI>& nvapi{std::make_unique<NVAPI>()};
  static std::function<NvAPI_Status(const DVC&)> run_command{nullptr};
  static std::vector<std::size_t> displays{nvdv::primary_display};
  static std::vector<DVC> controllers;
  static NvU32 value_to_set = NULL;
  static bool raw = false;
  static bool all = false;
}  // namespace nvdv

static NvAPI_Status init_dvc() {
  if (nvdv::all) {
    std::size_t n = 0;
    nvdv::displays.clear();
    nvdv::displays.resize(nvdv::display_count);
    std::generate(nvdv::displays.begin(), nvdv::displays.end(), [&] { return ++n; });
  }

  for (const std::size_t n : nvdv::displays) {
    if (n < 1 || n > nvdv::display_count) return reject("Invalid display number provided");
    nvdv::controllers.emplace_back(*nvdv::nvapi.get(), n);
  }

  return nvdv::controllers.empty() ? reject("Unable to initialize dvc(s) for display(s)") : NVAPI_OK;
}

static void cleanup() {
  ReleaseMutex(nvdv::handle);
  CloseHandle(nvdv::handle);
}

static void ensure_single_instance() {
  nvdv::handle = CreateMutex(NULL, TRUE, APP_NAME);
  if (!nvdv::handle) reject("Unable to create mutex for nvdv handle");
  if (GetLastError() == ERROR_ALREADY_EXISTS) CloseHandle(nvdv::handle), std::exit(0);
  for (const int sig : ABORT_SIGNALS) signal(sig, [](const int code) { cleanup(), std::exit(code); });
  std::atexit(cleanup);
}

int wmain(int argc, wchar_t* argv[]) {
  ensure_single_instance();
  static CLI::App app{APP_NAME};
  app.set_version_flag("-v,--version", APP_VERSION);
  app.add_option("-d,--display", nvdv::displays, "Specify other display number (handles only primary by default)");
  app.add_flag("-a,--all", nvdv::all, "Handle all available displays (overrides `--display`)");
  static const std::function<NvAPI_Status(const DVC&)>& handle_set{[](const DVC& dvc) {
    return nvdv::raw ? dvc.set_raw(nvdv::value_to_set) : dvc.set(nvdv::value_to_set);
  }};

  app.add_subcommand("info", "output current digital vibrance control info")->callback([] {
    nvdv::run_command = [](const DVC& dvc) {
      const auto& [_, cur, min, max]{dvc.info};
      printf("Display %zu%c\n", dvc.display, dvc.display == nvdv::primary_display ? '*' : '\0');
      printf("Current DV: %lu%c (%lu%%)\n", cur, cur < 10 ? ' ' : '\0', dvc.raw_to_percent(cur));
      printf("Minimum DV: %lu  (0%%)\n", min);
      printf("Maximum DV: %lu (100%%)\n\n", max);
      return NVAPI_OK;
    };
  });

  app.add_subcommand("toggle", "toggle current digital vibrance (between min and max)")->callback([] {
    nvdv::run_command = [](const DVC& dvc) {
      nvdv::value_to_set = dvc.info.cur > dvc.info.min ? dvc.info.min : dvc.info.max;
      return nvdv::raw = true, handle_set(dvc);
    };
  });

  app.add_subcommand("disable", "disable current digital vibrance (set to min)")->callback([] {
    nvdv::run_command = [](const DVC& dvc) {
      nvdv::value_to_set = dvc.info.min;
      return nvdv::raw = true, handle_set(dvc);
    };
  });

  app.add_subcommand("enable", "enable current digital vibrance (set to max)")->callback([] {
    nvdv::run_command = [](const DVC& dvc) {
      nvdv::value_to_set = dvc.info.max;
      return nvdv::raw = true, handle_set(dvc);
    };
  });

  static CLI::App* set{app.add_subcommand("set", "set current digital vibrance level")};
  set->add_flag("-r,--raw", nvdv::raw, "use raw values instead of percentage based scale");
  set->add_option("value", nvdv::value_to_set, "value in range [0, 100] (unless `--raw` is given)")->required();
  set->callback([] { nvdv::run_command = handle_set; });

  app.require_subcommand(1);
  CLI11_PARSE(app, argc, argv);
  if (init_dvc() == NVAPI_OK) std::for_each(nvdv::controllers.begin(), nvdv::controllers.end(), nvdv::run_command);
  return 0;
}
