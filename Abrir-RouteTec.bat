@echo off
setlocal
set "ROUTETEC_ROOT=%~dp0"
set "ROUTETEC_EXE=%ROUTETEC_ROOT%out\viewer\Release\terrain-options\RouteTec.exe"
if not exist "%ROUTETEC_EXE%" (
  echo No se encontro RouteTec compilado. Use el instalador en entrega\RouteTec.
  pause
  exit /b 1
)
if exist "%ROUTETEC_ROOT%out\viewer-deps\Library\bin" set "PATH=%ROUTETEC_ROOT%out\viewer-deps\Library\bin;%PATH%"
if exist "%ROUTETEC_ROOT%out\viewer-deps\Library\lib\qt6\plugins" set "QT_PLUGIN_PATH=%ROUTETEC_ROOT%out\viewer-deps\Library\lib\qt6\plugins"
start "" "%ROUTETEC_EXE%" %*
endlocal
