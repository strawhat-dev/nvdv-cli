#include <cstdlib>
//
#include <nvapi.h>
#include <windows.h>

#include "CLI11.hpp"

enum NVAPI : NvU32 {
  Initialize = 0x0150e828,
  EnumNvidiaDisplayHandle = 0x9abdd40d,
  GetDVCInfo = 0x4085DE45,
  SetDVCLevel = 0x172409B4,
};

struct DVC_INFO {
  NvU32 version;
  NvU32 current;
  NvU32 min;
  NvU32 max;
};

struct Subcommand {
  const char* id;
  const char* description;
  const std::function<void()> callback;
};

typedef const NvAPI_Status (*NvAPI_Initialize_t)();
typedef const NvU32* (*NvAPI_QueryInterface_t)(NvU32 offset);
typedef const NvAPI_Status (*NvAPI_EnumNvidiaDisplayHandle_t)(NvU32 display, NvDisplayHandle* handle);
typedef const NvAPI_Status (*NvAPI_SetDVCLevel_t)(NvDisplayHandle handle, NvU32 display, NvU32 value);
typedef const NvAPI_Status (*NvAPI_GetDVCInfo_t)(NvDisplayHandle handle, NvU32 display, DVC_INFO* ref);

static NvAPI_Initialize_t nvapi_Initialize;
static NvAPI_QueryInterface_t nvapi_QueryInterface;
static NvAPI_EnumNvidiaDisplayHandle_t nvapi_EnumNvidiaDisplayHandle;
static NvAPI_SetDVCLevel_t nvapi_SetDVCLevel;
static NvAPI_GetDVCInfo_t nvapi_GetDVCInfo;

static DVC_INFO info = {};
static NvU32 display = -1;
static HMODULE nvapi = nullptr;
static NvDisplayHandle handle = nullptr;
static std::function<void()> run_command = nullptr;
static NvU32* valueToGet = &info.current;
static NvU32 valueToSet;
static bool raw = false;

static void cleanup() {
  if (nvapi) FreeLibrary(nvapi);
}

static void reject(const char* reason) {
  cleanup();
  std::cerr << "Error: " << reason << std::endl;
  throw std::runtime_error(reason);
}

const NvU32 raw_to_percent(const NvU32 dv) {
  const NvU32 total = info.max - info.min;
  const NvU32 value = dv - info.min;
  return (value / total) * 100;
}

const NvU32 precent_to_raw(const NvU32 dv) {
  const NvU32 total = info.max - info.min;
  const double value = static_cast<double>(dv) / 100.0;
  return (value * total) + info.min;
}

const NvU32 get_primay_display() {
  DISPLAY_DEVICE dd;
  for (NvU32 i = 0; EnumDisplayDevices(NULL, i, &dd, 0); ++i) {
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) return i;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
  }

  return NULL;
}

static void handle_get() {
  NvU32 value = *valueToGet;
  if (!raw) value = raw_to_percent(value);
  printf("%lu\n", value);
}

static void handle_set() {
  const NvU32 value = raw ? valueToSet : precent_to_raw(valueToSet);
  if (value < info.min || value > info.max) reject("Value out of range");
  if (value != info.current) {
    nvapi_SetDVCLevel = (NvAPI_SetDVCLevel_t)(*nvapi_QueryInterface)(NVAPI::SetDVCLevel);
    if (!nvapi_SetDVCLevel) reject("Failed to load `nvapi_SetDVCLevel`");
    const NvAPI_Status status = (*nvapi_SetDVCLevel)(handle, 0, value);
    if (status != NVAPI_OK) reject("Failed to set the digital vibrance");
  }
}

static void handle_disable() {
  raw = true;
  valueToSet = info.min;
  handle_set();
}

static void handle_enable() {
  raw = true;
  valueToSet = info.max;
  handle_set();
}

static void handle_toggle() {
  raw = true;
  valueToSet = info.current == info.min ? info.max : info.min;
  handle_set();
}

static void init_nvapi() {
  nvapi = LoadLibrary("nvapi64.dll");
  if (!nvapi) reject("Failed to load nvapi64.dll");

  nvapi_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(nvapi, "nvapi_QueryInterface");
  if (!nvapi_QueryInterface) reject("Failed to load `nvapi_QueryInterface`");

  nvapi_Initialize = (NvAPI_Initialize_t)(*nvapi_QueryInterface)(NVAPI::Initialize);
  if (!nvapi_Initialize) reject("Failed to load `nvapi_Initialize`");

  NvAPI_Status status = (*nvapi_Initialize)();
  if (status != NVAPI_OK) reject("Failed to initialize NvAPI");

  nvapi_EnumNvidiaDisplayHandle =
    (NvAPI_EnumNvidiaDisplayHandle_t)(*nvapi_QueryInterface)(NVAPI::EnumNvidiaDisplayHandle);
  if (!nvapi_EnumNvidiaDisplayHandle) reject("Failed to load `nvapi_EnumNvidiaDisplayHandle`");

  if (display == -1) display = get_primay_display();
  status = (*nvapi_EnumNvidiaDisplayHandle)(display, &handle);
  if (status != NVAPI_OK) reject("Failed to get display handle");

  nvapi_GetDVCInfo = (NvAPI_GetDVCInfo_t)(*nvapi_QueryInterface)(NVAPI::GetDVCInfo);
  if (!nvapi_GetDVCInfo) reject("Failed to load `nvapi_GetDVCInfo`");

  info.version = sizeof(DVC_INFO) | 0x10000;
  status = (*nvapi_GetDVCInfo)(handle, 0, &info);
  if (status != NVAPI_OK) reject("Failed to get DVC info");
}

const std::vector<Subcommand> subcommands{
  {"set", "Set the digital vibrance", []() { run_command = handle_set; }},
  {"get", "Get the current digital vibrance", []() { run_command = handle_get; }},
  {"enable", "Enable digital vibrance (alias for `set [MAX]`)", []() { run_command = handle_enable; }},
  {"disable", "Disable digital vibrance (alias for `set [MIN]`)", []() { run_command = handle_disable; }},
  {"toggle", "Toggle digital vibrance (alias for `set [MIN|MAX]`)", []() { run_command = handle_toggle; }},
};

// clang-format off
const std::vector<Subcommand> get_commands{
  {"min", "Get the MIN digital vibrance", []() { valueToGet = &info.min; }},
  {"max", "Get the MAX digital vibrance", []() { valueToGet = &info.max; }},
  {"primary", "Get the index of the primary display", []() {
    static NvU32 primary = get_primay_display();
    valueToGet = &primary;
    raw = true;
  }},  // clang-format on
};

int main(int argc, char** argv) {
  std::atexit(cleanup);
  CLI::App app{"NVIDIA Digital Vibrance CLI"};
  app.add_flag("-r,--raw", raw, "Use raw values instead of percentage based scale");
  app.add_option("-d,--display", display, "Specify display other than the primary (zero-based index)");
  for (const auto& [id, description, callback] : subcommands) app.add_subcommand(id, description)->callback(callback);

  const auto get = app.get_subcommand("get");
  for (const auto& [id, description, callback] : get_commands) get->add_subcommand(id, description)->callback(callback);
  app.get_subcommand("set")
    ->add_option("value", valueToSet, "Value in range [0, 100] or ([MIN, MAX] if raw flag is enabled)")
    ->required();

  app.require_subcommand(1);
  CLI11_PARSE(app, argc, app.ensure_utf8(argv));
  init_nvapi(), run_command();
  return 0;
}
