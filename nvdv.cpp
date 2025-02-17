#include <windows.h>
#include "CLI11.hpp"
#include "version.h"
#include "nvapi.h"

#pragma warning(suppress: 4702)  // unreachable
static NvAPI_Status reject(const char* reason) {
  std::cerr << "Error: " << reason << std::endl;
  throw std::runtime_error(reason);
  return NVAPI_ERROR;
}

struct NVAPI {
  static constexpr NvU32 INITIALIZE{0x0150e828};
  static constexpr NvU32 ENUM_NVIDIA_DISPLAY_HANDLE{0x9abdd40d};
  static constexpr NvU32 GET_DVC_INFO{0x4085de45};   // undocumented
  static constexpr NvU32 SET_DVC_LEVEL{0x172409b4};  // undocumented
  typedef long long (*NvAPI_QueryInterface_t)(...);
  NvAPI_QueryInterface_t ${nullptr};
  const HMODULE instance{nullptr};
  ~NVAPI() { FreeLibrary(instance); }
  NVAPI() : instance(LoadLibrary("nvapi64.dll")) {
    if (!instance) reject("Failed to load nvapi64.dll");
    $ = (NvAPI_QueryInterface_t)GetProcAddress(instance, "nvapi_QueryInterface");
    if (!$) reject("Failed to load `nvapi_QueryInterface`");
  }
};

struct DVC_INFO {
  const NvU32 _{sizeof(DVC_INFO) | 0x10000};
  const NvU32 cur{NULL};
  const NvU32 min{NULL};
  const NvU32 max{NULL};
};

struct DVC {
  DVC_INFO info{};
  std::optional<NvU32> display;     // 1-based index (same as UI)
  NvDisplayHandle handle{nullptr};  // handle for 0-based indexed display
};

typedef NvAPI_Status (*NvAPI_Initialize_t)();
typedef NvAPI_Status (*NvAPI_EnumNvidiaDisplayHandle_t)(NvU32 display, NvDisplayHandle* handle);
typedef NvAPI_Status (*NvAPI_GetDVCInfo_t)(NvDisplayHandle handle, NvU32 display, DVC_INFO* info);
typedef NvAPI_Status (*NvAPI_SetDVCLevel_t)(NvDisplayHandle handle, NvU32 display, NvU32 value);

// app context
namespace nvdv {
  static const std::unique_ptr<NVAPI>& nvapi{std::make_unique<NVAPI>()};
  static std::function<NvAPI_Status()> run_command{nullptr};
  static NvU32 value_to_set{NULL};
  static bool raw{false};
  static DVC dvc{};
}  // namespace nvdv

/** @returns 1-based index of primary display */
static NvU32 get_primary_display() {
  DISPLAY_DEVICE dd;
  dd.cb = sizeof(DISPLAY_DEVICE);
  for (NvU32 i = 0; EnumDisplayDevices(NULL, i, &dd, 0); ++i) {
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) return i + 1;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
  }

  return reject("Unable to get primary display");
}

static NvU32 raw_to_percent(const NvU32& value) noexcept {
  const auto& [_, __, min, max]{nvdv::dvc.info};
  const double& total{static_cast<double>(max - min)};
  const double& decimal{static_cast<double>(value - min) / total};
  return static_cast<NvU32>(std::round(decimal * 100.0));
}

static NvU32 percent_to_raw(const NvU32& value) noexcept {
  const auto& [_, __, min, max]{nvdv::dvc.info};
  const double& total{static_cast<double>(max - min)};
  const double& decimal{static_cast<double>(value) / 100.0};
  return static_cast<NvU32>(std::round((decimal * total) + min));
}

static NvAPI_Status handle_set() {
  const auto& [__, cur, min, max]{nvdv::dvc.info};
  const NvU32& dv{nvdv::raw ? nvdv::value_to_set : percent_to_raw(nvdv::value_to_set)};
  if (dv < min || dv > max) return reject("Value out of range");
  else if (dv != cur) {
    static const NvAPI_SetDVCLevel_t& nvapi_SetDVCLevel{(NvAPI_SetDVCLevel_t)(*nvdv::nvapi->$)(NVAPI::SET_DVC_LEVEL)};
    if (!nvapi_SetDVCLevel) return reject("Failed to load `nvapi_SetDVCLevel`");
    const NvAPI_Status& status{(*nvapi_SetDVCLevel)(nvdv::dvc.handle, 0, dv)};
    if (status != NVAPI_OK) return reject("Failed to set the digital vibrance");
  }

  return NVAPI_OK;
}

static NvAPI_Status handle_enable() {
  nvdv::value_to_set = nvdv::dvc.info.max;
  nvdv::raw = true;
  return handle_set();
}

static NvAPI_Status handle_disable() {
  nvdv::value_to_set = nvdv::dvc.info.min;
  nvdv::raw = true;
  return handle_set();
}

static NvAPI_Status handle_toggle() {
  const auto& [_, cur, min, max]{nvdv::dvc.info};
  const std::function<NvAPI_Status()>& toggle{cur > min ? handle_disable : handle_enable};
  return toggle();
}

static NvAPI_Status handle_info() {
  const auto& [info, display, _]{nvdv::dvc};
  const auto& [__, cur, min, max]{info};
  printf("Display %lu%c\n", display.value(), display == get_primary_display() ? '*' : '\0');
  printf("Current DV: %lu%c (%lu%%)\n", cur, cur < 10 ? ' ' : '\0', raw_to_percent(cur));
  printf("Minimum DV: %lu  (0%%)\n", min);
  printf("Maximum DV: %lu (100%%)\n", max);
  return NVAPI_OK;
}

static NvAPI_Status init_nvapi() {
  static const NvAPI_Initialize_t& init{(NvAPI_Initialize_t)(*nvdv::nvapi->$)(NVAPI::INITIALIZE)};
  if (!init) return reject("Failed to load `nvapi_Initialize`");

  NvAPI_Status status = (*init)();
  if (status != NVAPI_OK) return reject("Failed to initialize NvAPI");

  static const NvAPI_EnumNvidiaDisplayHandle_t& enum_display_handle{
    (NvAPI_EnumNvidiaDisplayHandle_t)(*nvdv::nvapi->$)(NVAPI::ENUM_NVIDIA_DISPLAY_HANDLE)
  };

  if (!nvdv::dvc.display.has_value()) nvdv::dvc.display.emplace(get_primary_display());
  if (!enum_display_handle) return reject("Failed to load `nvapi_EnumNvidiaDisplayHandle`");

  status = (*enum_display_handle)(nvdv::dvc.display.value() - 1, &nvdv::dvc.handle);
  if (status != NVAPI_OK) return reject("Failed to get display handle");

  static const NvAPI_GetDVCInfo_t& get_dvc_info{(NvAPI_GetDVCInfo_t)(*nvdv::nvapi->$)(NVAPI::GET_DVC_INFO)};
  if (!get_dvc_info) return reject("Failed to load `nvapi_GetDVCInfo`");

  status = (*get_dvc_info)(nvdv::dvc.handle, 0, &nvdv::dvc.info);
  if (status != NVAPI_OK) return reject("Failed to get DVC info");

  return NVAPI_OK;
}

int main(int argc, char** argv) {
  static CLI::App app{"NVIDIA Digital Vibrance CLI"};
  app.set_version_flag("-v,--version", std::string(NVDV_VERSION));
  app.add_option("-d,--display", nvdv::dvc.display, "Specify a display number other than the primary to handle");

  static CLI::App* set{app.add_subcommand("set", "set the current digital vibrance")};
  set->add_flag("-r,--raw", nvdv::raw, "use raw values instead of percentage based scale");
  set->add_option("value", nvdv::value_to_set, "value in range [0, 100] (unless `--raw` is given)")->required();
  set->callback([]() { nvdv::run_command = handle_set; });

  app.add_subcommand("enable", "enable digital vibrance (set to max)")->callback([]() {
    nvdv::run_command = handle_enable;
  });

  app.add_subcommand("disable", "disable digital vibrance (set to min)")->callback([]() {
    nvdv::run_command = handle_disable;
  });

  app.add_subcommand("toggle", "toggle digital vibrance (between min and max)")->callback([]() {
    nvdv::run_command = handle_toggle;
  });

  app.add_subcommand("info", "output current digital vibrance control info")->callback([]() {
    nvdv::run_command = handle_info;
  });

  app.require_subcommand(1);
  CLI11_PARSE(app, argc, app.ensure_utf8(argv));
  return init_nvapi(), nvdv::run_command();
}
