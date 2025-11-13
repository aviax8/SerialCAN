@echo off

set VCVARS="True"
set TRIAL="True"
set LIBDB="True"
set CONTROLCAN="True"

rem parse arguments: [NOVARS] [NOTRIAL] [NODEBUG] [NOCONTROLCAN]
:LOOP
if "%1" == "NOVARS" set VCVARS="False"
if "%1" == "NOTRIAL" set TRIAL="False"
if "%1" == "NODEBUG" set LIBDB="False"
if "%1" == "NOCONTROLCAN" set CONTROLCAN="False"
SHIFT
if not "%1" == "" goto LOOP

rem set MSBuild environment variables
if %VCVARS% == "True" (
   pushd
   call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" x86
   popd
)
pushd

rem generate a pseudo build number
call build_no.bat

set BIN=.\Binaries
if not exist %BIN% mkdir %BIN%
set BIN=%BIN%\x86
if not exist %BIN% mkdir %BIN%

rem build the library 'ControlCAN'
if %CONTROLCAN% == "True" (
  call msbuild.exe .\Libraries\ControlCAN\ControlCAN.vcxproj /t:Clean;Build /p:"Configuration=Release_dll";"Platform=Win32"
  if errorlevel 1 goto end

  call msbuild.exe .\Libraries\ControlCAN\ControlCAN.vcxproj /t:Clean;Build /p:"Configuration=Release_lib";"Platform=Win32"
  if errorlevel 1 goto end

  echo Copying ControlCAN...
  copy /Y .\Libraries\ControlCAN\Release_dll\ControlCAN.dll %BIN%
)

rem end of the job
:end
popd
if %VCVARS% == "True" (
   pause
)

pause
