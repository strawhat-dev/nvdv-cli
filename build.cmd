@echo off

cd "%~dp0" && if exist nvdv.exe del /f nvdv.exe

if not exist nvapi\amd64\ git.exe submodule update --init --remote

call vcvars64.bat && cl.exe /std:c++latest /MD /O2 /W4 /WX ^
  /EHsc /I nvapi nvdv.cpp user32.lib shell32.lib ^
  /link /LIBPATH:nvapi\amd64 && del /f nvdv.obj
