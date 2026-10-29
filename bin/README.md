# Ejecutables RouteMine 0.1.0 CPU

Herramienta de consola preliminar, sin instalación de Python, Qt, VTK ni CUDA.
Descargue el repositorio completo (Code > Download ZIP) para conservar los ejemplos.
No incluye la aplicación gráfica de RouteTec ni importación DXF.

## Windows x64

Desde PowerShell, en la raíz del repositorio:

```powershell
.\bin\windows-x64\routemine_cli.exe --demo
.\bin\windows-x64\routemine_cli.exe --cost-demo
.\bin\windows-x64\routemine_cli.exe --plan-basic fixtures/ridge.obj fixtures/basic.request ruta.csv
```

El archivo `bin/windows-x64/demo.cmd` permite abrir una demostración con doble clic.
El ejecutable se compila con MinGW-w64 y runtime enlazado estáticamente.
No está firmado digitalmente. Compilación cruzada desde Linux. GitHub Actions
comprobó este binario en Windows Server 2022: demo y planificación básica aprobadas,
con CSV idéntico al generado por la compilación nativa MSVC. El código fuente
aprobó además las 28 pruebas CTest en Windows y Linux.
Evidencia: https://github.com/DeyviRodrigo/routemine/actions/runs/40802573184

## Linux x64

```sh
chmod +x bin/linux-x64/routemine_cli
bin/linux-x64/routemine_cli --demo
bin/linux-x64/routemine_cli --plan-basic fixtures/ridge.obj fixtures/basic.request ruta.csv
```

Compilado y probado con GCC 13 en Ubuntu 24.04 x64; usa bibliotecas dinámicas del
sistema (glibc y libstdc++). No se afirma compatibilidad con distribuciones antiguas.

## Resultados y límites

`ruta.csv` contiene X,Y,Z y `ruta.csv.project.conf` la configuración efectiva.
Los ejemplos y costos son sintéticos. Un código de salida 0 solo indica éxito de
la operación solicitada; no certifica factibilidad geométrica vehicular integral.
Los archivos de salida existentes se reemplazan.

`SHA256SUMS.txt` permite verificar integridad. Los binarios corresponden al código
base b2753d0e808373b99cd1c4719f0d247458eb25a9, sin cambios funcionales al núcleo.
