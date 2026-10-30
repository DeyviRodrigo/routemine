@echo off
setlocal
if not exist "%~dp0out\ramp-qa-selected\project.json" (
  echo No se encontro el proyecto de prueba con los puntos seleccionados.
  exit /b 1
)
call "%~dp0Abrir-RouteMine.bat" --project "%~dp0out\ramp-qa-selected\project.json"
endlocal
