#include <windows.h>
#include "nvapi.h"
#include "CLI11.hpp"

struct DVC_INFO {
  const NvU32 _{sizeof(DVC_INFO) | 0x10000};
  const NvU32 cur{NULL};
  const NvU32 min{NULL};
  const NvU32 max{NULL};
};

struct DVC {
  DVC_INFO info{};
  std::optional<NvU32> display;
  NvDisplayHandle handle{nullptr};
};

struct Context {
  DVC dvc{};
  bool raw{false};
  NvU32 value_to_set{NULL};
  std::function<NvU32()> run_command{nullptr};
};

static NvAPI_Status reject(const char* reason) {
  std::cerr << "Error: " << reason << std::endl;
  throw std::runtime_error(reason);
  return NVAPI_ERROR;
}

typedef NvAPI_Status (*NvAPI_Initialize_t)();
typedef long long(WINAPI* NvAPI_QueryInterface_t)(...);
typedef NvAPI_Status (*NvAPI_EnumNvidiaDisplayHandle_t)(NvU32 display, NvDisplayHandle* handle);
typedef NvAPI_Status (*NvAPI_GetDVCInfo_t)(NvDisplayHandle handle, NvU32 display, DVC_INFO* info);
typedef NvAPI_Status (*NvAPI_SetDVCLevel_t)(NvDisplayHandle handle, NvU32 display, NvU32 value);

struct NVAPI {
  const HMODULE instance{nullptr};
  NvAPI_QueryInterface_t ptr{nullptr};
  static constexpr NvU32 INITIALIZE{0x0150e828};
  static constexpr NvU32 GET_DVC_INFO{0x4085DE45};
  static constexpr NvU32 SET_DVC_LEVEL{0x172409B4};
  static constexpr NvU32 ENUM_NVIDIA_DISPLAY_HANDLE{0x9abdd40d};

  ~NVAPI() { FreeLibrary(instance); }
  NVAPI() : instance(LoadLibrary("nvapi64.dll")) {
    if (!instance) reject("Failed to load nvapi64.dll");
    ptr = (NvAPI_QueryInterface_t)GetProcAddress(instance, "nvapi_QueryInterface");
    if (!ptr) reject("Failed to load `nvapi_QueryInterface`");
  }
};

static Context app{};

static const std::unique_ptr<NVAPI>& nvapi{std::make_unique<NVAPI>()};

static NvU32 raw_to_percent(const NvU32& value) noexcept {
  const auto& [_, __, min, max]{app.dvc.info};
  const double& total{static_cast<double>(max - min)};
  const double& decimal{static_cast<double>(value - min) / total};
  return static_cast<NvU32>(std::round(decimal * 100.0));
}

static NvU32 percent_to_raw(const NvU32& value) noexcept {
  const auto& [_, __, min, max]{app.dvc.info};
  const double& total{static_cast<double>(max - min)};
  const double& decimal{static_cast<double>(value) / 100.0};
  return static_cast<NvU32>(std::round((decimal * total) + min));
}

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

static NvAPI_Status handle_info() {
  const auto& [info, display, _]{app.dvc};
  const auto& [__, cur, min, max]{info};
  const char& pad{cur < 10 ? ' ' : '\0'};
  const char& indicator{display == get_primary_display() ? '*' : '\0'};

  printf("Display %lu%c\n", display.value(), indicator);
  printf("Current DV: %lu%c (%lu%%)\n", cur, pad, raw_to_percent(cur));
  printf("Minimum DV: %lu  (0%%)\n", min);
  printf("Maximum DV: %lu (100%%)\n", max);

  return NVAPI_OK;
}

static NvAPI_Status handle_set() {
  const auto& [dvc, raw, value_to_set, _]{app};
  const auto& [__, cur, min, max]{dvc.info};
  const NvU32& dv{raw ? value_to_set : percent_to_raw(value_to_set)};
  if (dv < min || dv > max) return reject("Value out of range");
  else if (dv != cur) {
    static const NvAPI_SetDVCLevel_t& nvapi_SetDVCLevel{(NvAPI_SetDVCLevel_t)(*nvapi->ptr)(NVAPI::SET_DVC_LEVEL)};
    if (!nvapi_SetDVCLevel) return reject("Failed to load `nvapi_SetDVCLevel`");
    const NvAPI_Status& status{(*nvapi_SetDVCLevel)(dvc.handle, 0, dv)};
    if (status != NVAPI_OK) return reject("Failed to set the digital vibrance");
  }

  return NVAPI_OK;
}

static NvAPI_Status handle_enable() {
  app.value_to_set = app.dvc.info.max;
  app.raw = true;
  return handle_set();
}

static NvAPI_Status handle_disable() {
  app.value_to_set = app.dvc.info.min;
  app.raw = true;
  return handle_set();
}

static NvAPI_Status handle_toggle() {
  const auto& [_, cur, min, max]{app.dvc.info};
  const std::function<NvAPI_Status()>& toggle{cur > min ? handle_disable : handle_enable};
  return toggle();
}

static NvAPI_Status init_nvapi() {
  static const NvAPI_Initialize_t& init{(NvAPI_Initialize_t)(*nvapi->ptr)(NVAPI::INITIALIZE)};
  if (!init) return reject("Failed to load `nvapi_Initialize`");

  NvAPI_Status status = (*init)();
  if (status != NVAPI_OK) return reject("Failed to initialize NvAPI");

  static const NvAPI_EnumNvidiaDisplayHandle_t& enum_display_handle{
    (NvAPI_EnumNvidiaDisplayHandle_t)(*nvapi->ptr)(NVAPI::ENUM_NVIDIA_DISPLAY_HANDLE)
  };

  if (!app.dvc.display.has_value()) app.dvc.display.emplace(get_primary_display());
  if (!enum_display_handle) return reject("Failed to load `nvapi_EnumNvidiaDisplayHandle`");

  status = (*enum_display_handle)(app.dvc.display.value() - 1, &app.dvc.handle);
  if (status != NVAPI_OK) return reject("Failed to get display handle");

  static const NvAPI_GetDVCInfo_t& get_dvc_info{(NvAPI_GetDVCInfo_t)(*nvapi->ptr)(NVAPI::GET_DVC_INFO)};
  if (!get_dvc_info) return reject("Failed to load `nvapi_GetDVCInfo`");

  status = (*get_dvc_info)(app.dvc.handle, 0, &app.dvc.info);
  if (status != NVAPI_OK) return reject("Failed to get DVC info");

  return status;
}

int main(int argc, char** argv) {
  static CLI::App cli{"NVIDIA Digital Vibrance CLI"};
  cli.add_option("-d,--display", app.dvc.display, "specify a display number other than the primary to handle");

  static CLI::App* set{cli.add_subcommand("set", "set the current digital vibrance")};
  set->add_flag("-r,--raw", app.raw, "use raw values instead of percentage based scale");
  set->add_option("value", app.value_to_set, "value in range [0, 100] (or [min, max] if `-r` is provided)")->required();
  set->callback([]() { app.run_command = handle_set; });

  cli.add_subcommand("enable", "enable digital vibrance (set to max)")->callback([]() {
    app.run_command = handle_enable;
  });

  cli.add_subcommand("disable", "disable digital vibrance (set to min)")->callback([]() {
    app.run_command = handle_disable;
  });

  cli.add_subcommand("toggle", "toggle digital vibrance (between min and max)")->callback([]() {
    app.run_command = handle_toggle;
  });

  cli.add_subcommand("info", "output current digital vibrance control info")->callback([]() {
    app.run_command = handle_info;
  });

  cli.require_subcommand(1);
  CLI11_PARSE(cli, argc, cli.ensure_utf8(argv));
  return init_nvapi(), app.run_command();
}
