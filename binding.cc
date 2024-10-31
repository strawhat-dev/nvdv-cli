#include <Windows.h>
#include <node.h>

using namespace v8;

typedef struct {
  unsigned int version;
  int currentDV;
  int minDV;
  int maxDV;
} NV_DISPLAY_DVC_INFO;

typedef int (*nvapi_Initialize_t)();
typedef int* (*nvapi_QueryInterface_t)(unsigned int offset);
typedef int (*nvapi_EnumPhysicalGPUs_t)(int** handles, int* count);
typedef int (*nvapi_EnumNvidiaDisplayHandle_t)(int thisEnum, int* handle);
typedef int (*nvapi_GetDVCInfo_t)(int displayHandle, int outputId, NV_DISPLAY_DVC_INFO* DVCInfo);
typedef int (*nvapi_SetDVCLevel_t)(int handle, int outputId, int level);

nvapi_Initialize_t nvapi_Initialize = NULL;
nvapi_QueryInterface_t nvapi_QueryInterface = NULL;
nvapi_EnumPhysicalGPUs_t nvapi_EnumPhysicalGPUs = NULL;
nvapi_EnumNvidiaDisplayHandle_t nvapi_EnumNvidiaDisplayHandle = NULL;
nvapi_GetDVCInfo_t nvapi_GetDVCInfo = NULL;
nvapi_SetDVCLevel_t nvapi_SetDVCLevel = NULL;

int handle = 0;

auto reject = [](Isolate*& iso, const char* reason) {
  iso->ThrowException(Exception::TypeError(String::NewFromUtf8(iso, reason).ToLocalChecked()));
};

auto defineNumberPropertyCallback(Isolate*& iso, const Local<Object>& obj, const Local<Context>& ctx) {
  return [&iso, &obj, &ctx](const char* key, const int& value) {
    obj->Set(ctx, String::NewFromUtf8(iso, key).ToLocalChecked(), Number::New(iso, value)).FromJust();
  };
}

NV_DISPLAY_DVC_INFO getDVCInfo() {
  NV_DISPLAY_DVC_INFO info = {};
  Isolate* iso = Isolate::GetCurrent();
  info.version = sizeof(NV_DISPLAY_DVC_INFO) | 0x10000;
  nvapi_GetDVCInfo = (nvapi_GetDVCInfo_t)(*nvapi_QueryInterface)(0x4085DE45);
  if (nvapi_GetDVCInfo == NULL) {
    reject(iso, "Could load `nvapi_GetDVCInfo_t`");
    return info;
  }

  int status = (*nvapi_GetDVCInfo)(handle, 0, &info);
  if (status != 0) reject(iso, "Could not get digital vibrance");
  return info;
}

void getDigitalVibrance(const FunctionCallbackInfo<Value>& args) {
  Isolate* iso = args.GetIsolate();
  Local<Object> obj = Object::New(iso);
  Local<Context> ctx = iso->GetCurrentContext();
  auto defineNumberProperty = defineNumberPropertyCallback(iso, obj, ctx);
  NV_DISPLAY_DVC_INFO info = getDVCInfo();
  defineNumberProperty("version", info.version);
  defineNumberProperty("current", info.currentDV);
  defineNumberProperty("min", info.minDV);
  defineNumberProperty("max", info.maxDV);
  args.GetReturnValue().Set(obj);
}

void setDigitalVibrance(const FunctionCallbackInfo<Value>& args) {
  Isolate* iso = args.GetIsolate();
  nvapi_SetDVCLevel = (nvapi_SetDVCLevel_t)(*nvapi_QueryInterface)(0x172409B4);
  if (nvapi_SetDVCLevel == NULL) return reject(iso, "Could not load `nvapi_SetDVCLevel`");
  if (args.Length() != 1 || !args[0]->IsNumber()) return reject(iso, "Invalid arguments");
  int arg = args[0].As<Number>()->Value();
  NV_DISPLAY_DVC_INFO info = getDVCInfo();
  if (arg < info.minDV || arg > info.maxDV) return reject(iso, "Value out of range");
  int status = arg == info.currentDV ? 0 : (*nvapi_SetDVCLevel)(handle, 0, arg);
  if (status != 0) return reject(iso, "Could set digital vibrance");
  args.GetReturnValue().Set(Number::New(iso, status));
}

void toggleDigitalVibrance(const FunctionCallbackInfo<Value>& args) {
  Isolate* iso = args.GetIsolate();
  nvapi_SetDVCLevel = (nvapi_SetDVCLevel_t)(*nvapi_QueryInterface)(0x172409B4);
  if (nvapi_SetDVCLevel == NULL) return reject(iso, "Could not load `nvapi_SetDVCLevel`");
  NV_DISPLAY_DVC_INFO info = getDVCInfo();
  int status = (*nvapi_SetDVCLevel)(handle, 0, info.currentDV > info.minDV ? info.minDV : info.maxDV);
  if (status != 0) return reject(iso, "Could toggle digital vibrance");
  args.GetReturnValue().Set(Number::New(iso, status));
}

int init_nvapi() {
  int status = 1;
  if (handle > 0) return handle;
  Isolate* iso = Isolate::GetCurrent();
  HMODULE nvapi = LoadLibrary("nvapi64.dll");
  if (nvapi == NULL) {
    reject(iso, "Could not load nvapi64.dll");
    return status;
  }

  nvapi_QueryInterface = (nvapi_QueryInterface_t)GetProcAddress(nvapi, "nvapi_QueryInterface");
  if (nvapi_QueryInterface == NULL) {
    reject(iso, "Could not load NvAPI_QueryInterface");
    return status;
  }

  nvapi_Initialize = (nvapi_Initialize_t)(*nvapi_QueryInterface)(0x0150E828);
  status = (*nvapi_Initialize)();
  if (status != 0) {
    reject(iso, "Could not initialize nvapi");
    return status;
  }

  nvapi_EnumNvidiaDisplayHandle = (nvapi_EnumNvidiaDisplayHandle_t)(*nvapi_QueryInterface)(0x9ABDD40D);
  status = (*nvapi_EnumNvidiaDisplayHandle)(0, &handle);
  if (status != 0) reject(iso, "Could not get primary display handle");
  return status;
}

void init(Local<Object> exports) {
  if (init_nvapi() != 0) return;
  NODE_SET_METHOD(exports, "getDigitalVibrance", getDigitalVibrance);
  NODE_SET_METHOD(exports, "setDigitalVibrance", setDigitalVibrance);
  NODE_SET_METHOD(exports, "toggleDigitalVibrance", toggleDigitalVibrance);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, init)
