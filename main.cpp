#include <cstdlib>
//
#include <nvapi.h>
#include <windows.h>

#include "CLI11.hpp"

struct DVC_INFO {
  NvU32 _;
  NvU32 val;
  NvU32 min;
  NvU32 max;
};

typedef NvAPI_Status (*NvAPI_Initialize_t)();
typedef const NvU32* (*NvAPI_QueryInterface_t)(NvU32 offset);
typedef NvAPI_Status (*NvAPI_EnumNvidiaDisplayHandle_t)(NvU32 display, NvDisplayHandle* handle);
typedef NvAPI_Status (*NvAPI_SetDVCLevel_t)(NvDisplayHandle handle, NvU32 display, NvU32 value);
typedef NvAPI_Status (*NvAPI_GetDVCInfo_t)(NvDisplayHandle handle, NvU32 display, DVC_INFO* ref);

static void reject(const char* reason) {
  std::cerr << "Error: " << reason << std::endl;
  throw std::runtime_error(reason);
}

struct NVAPI {
  static constexpr NvU32 INITIALIZE{0x0150e828};
  static constexpr NvU32 GET_DVC_INFO{0x4085DE45};
  static constexpr NvU32 SET_DVC_LEVEL{0x172409B4};
  static constexpr NvU32 ENUM_NVIDIA_DISPLAY_HANDLE{0x9abdd40d};

  const HMODULE library{nullptr};
  NvAPI_QueryInterface_t ${nullptr};
  ~NVAPI() { FreeLibrary(library); }
  NVAPI() : library(LoadLibrary("nvapi64.dll")) {
    if (!library) reject("Failed to load nvapi64.dll");
    $ = (NvAPI_QueryInterface_t)GetProcAddress(library, "nvapi_QueryInterface");
    if (!$) reject("Failed to load `nvapi_QueryInterface`");
  }
};

std::unique_ptr<NVAPI> nvapi(new NVAPI());

struct DVC {
  DVC_INFO info{};
  std::optional<NvU32> display;
  NvDisplayHandle handle{nullptr};
  std::function<void()> execute{nullptr};
};

static DVC dvc{};

struct Options {
  bool raw{false};
  NvU32 valueToSet;
  NvU32* valueToGet{&dvc.info.val};
};

static Options opts{};

static NvU32 raw_to_percent(const NvU32& val) {
  static const auto& [_, __, min, max]{dvc.info};
  static const double total{static_cast<double>(max - min)};
  static const double x{static_cast<double>(val - min) / total};
  return static_cast<NvU32>(x * 100.0);
}

static NvU32 percent_to_raw(const NvU32& val) {
  static const auto& [_, __, min, max]{dvc.info};
  static const double total{static_cast<double>(max - min)};
  static const double x{static_cast<double>(val) / 100.0};
  return static_cast<NvU32>((x * total) + min);
}

static NvU32 get_primay_display() {
  DISPLAY_DEVICE dd{};
  dd.cb = sizeof(DISPLAY_DEVICE);
  for (NvU32 i = 0; EnumDisplayDevices(NULL, i, &dd, 0); ++i) {
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) return i;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
  }

  reject("Unable to get primary display");

  return NULL;
}

static void handle_get() {
  NvU32 value{*opts.valueToGet};
  if (!opts.raw) value = raw_to_percent(value);
  printf("%lu\n", value);
}

static void handle_set() {
  const auto& [raw, valueToSet, _]{opts};
  const auto& [__, val, min, max]{dvc.info};
  const NvU32 dv{raw ? valueToSet : percent_to_raw(valueToSet)};
  if (dv < min || dv > max) reject("Value out of range");
  if (dv != val) {
    static const NvAPI_SetDVCLevel_t nvapi_SetDVCLevel{(NvAPI_SetDVCLevel_t)(*nvapi->$)(NVAPI::SET_DVC_LEVEL)};
    if (!nvapi_SetDVCLevel) reject("Failed to load `nvapi_SetDVCLevel`");
    const NvAPI_Status status{(*nvapi_SetDVCLevel)(dvc.handle, 0, dv)};
    if (status != NVAPI_OK) reject("Failed to set the digital vibrance");
  }
}

static void handle_enable() {
  opts.valueToSet = dvc.info.max;
  opts.raw = true;
  handle_set();
}

static void handle_disable() {
  opts.valueToSet = dvc.info.min;
  opts.raw = true;
  handle_set();
}

static void handle_toggle() {
  const auto& [_, val, min, max]{dvc.info};
  const auto& toggle{val == min ? handle_enable : handle_disable};
  toggle();
}

static void init_nvapi() {
  static const NvAPI_Initialize_t init{(NvAPI_Initialize_t)(*nvapi->$)(NVAPI::INITIALIZE)};
  if (!init) reject("Failed to load `nvapi_Initialize`");

  NvAPI_Status status{(*init)()};
  if (status != NVAPI_OK) reject("Failed to initialize NvAPI");

  static const NvAPI_EnumNvidiaDisplayHandle_t handle_display{
    (NvAPI_EnumNvidiaDisplayHandle_t)(*nvapi->$)(NVAPI::ENUM_NVIDIA_DISPLAY_HANDLE)
  };
  if (!handle_display) reject("Failed to load `nvapi_EnumNvidiaDisplayHandle`");

  if (!dvc.display.has_value()) dvc.display.emplace(get_primay_display());
  status = (*handle_display)(dvc.display.value(), &dvc.handle);
  if (status != NVAPI_OK) reject("Failed to get display handle");

  static const NvAPI_GetDVCInfo_t get_dvc_info{(NvAPI_GetDVCInfo_t)(*nvapi->$)(NVAPI::GET_DVC_INFO)};
  if (!get_dvc_info) reject("Failed to load `nvapi_GetDVCInfo`");

  dvc.info._ = sizeof(DVC_INFO) | 0x10000;
  status = (*get_dvc_info)(dvc.handle, 0, &dvc.info);
  if (status != NVAPI_OK) reject("Failed to get DVC info");
}

struct Subcommand {
  const char* id;
  const char* description;
  const std::function<void()> callback;
};

static const std::vector<Subcommand> subcommands{
  {"get", "Get the current digital vibrance", []() { dvc.execute = handle_get; }},
  {"set", "Set the current digital vibrance", []() { dvc.execute = handle_set; }},
  {"toggle", "Toggle digital vibrance", []() { dvc.execute = handle_toggle; }},
  {"enable", "Enable digital vibrance", []() { dvc.execute = handle_enable; }},
  {"disable", "Disable digital vibrance", []() { dvc.execute = handle_disable; }},
};

static const std::vector<Subcommand> getcommands{
  {"min", "Get the min digital vibrance", []() { opts.valueToGet = &dvc.info.min; }},
  {"max", "Get the max digital vibrance", []() { opts.valueToGet = &dvc.info.max; }},
  {"display",
   "Get the index of the primary display",
   []() { opts.raw = true, dvc.display.reset(), opts.valueToGet = &*dvc.display; }},
};

int main(int argc, char** argv) {
  static CLI::App app{"NVIDIA Digital Vibrance CLI"};
  app.add_flag("-r,--raw", opts.raw, "Use raw values instead of percentage based scale");
  app.add_option("-d,--display", dvc.display, "Specify a display other than the primary (zero-based index)");

  app.require_subcommand(1);
  for (const auto& [id, description, callback] : subcommands) app.add_subcommand(id, description)->callback(callback);

  static const auto& get = app.get_subcommand("get");
  for (const auto& [id, description, callback] : getcommands) get->add_subcommand(id, description)->callback(callback);

  app.get_subcommand("set")
    ->add_option("value", opts.valueToSet, "Value in range [0, 100] (or [MIN, MAX] if raw flag is enabled)")
    ->required();

  CLI11_PARSE(app, argc, app.ensure_utf8(argv));
  init_nvapi(), dvc.execute();
  return 0;
}
