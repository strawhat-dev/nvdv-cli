@echo off

cd "%~dp0" && rd /s /q out && md out

if not exist nvapi\amd64\ git.exe submodule update --init --remote

call vcvars64.bat && cl.exe /std:c++latest /MD /O2 /W4 ^
  /EHsc /I nvapi main.cpp user32.lib shell32.lib ^
  /link /LIBPATH:nvapi\amd64 /OUT:out\nvdv.exe ^
  && del /f main.obj
