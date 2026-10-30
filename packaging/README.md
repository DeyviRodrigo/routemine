# Distribución de RouteTec para Windows

El paquete de usuario final incluye Qt, VTK, sus dependencias y las bibliotecas
redistribuibles de Visual C++ en la misma carpeta que `RouteTec.exe`. No instala
Python, conda, compiladores ni herramientas de desarrollo. Funciona sin conexión.

## Crear el paquete

Compile primero el target `routemine_viewer` en Release. Su nombre de archivo
público es `RouteTec.exe`. Para preparar el compilador de instaladores:

```powershell
& packaging/Get-NSIS.ps1
```

La herramienta NSIS 3.11 queda en `out/tooling/nsis`, sin instalar servicios ni
modificar el entorno del sistema. El ZIP tiene SHA-256 fijado en el script.

```powershell
& packaging/Build-Installer.ps1 -Viewer out/viewer/Release/terrain-options/RouteTec.exe
```

Para incluir además un proyecto portátil de carretera en espiral:

```powershell
& packaging/Build-Installer.ps1 `
  -Viewer out/viewer/Release/terrain-options/RouteTec.exe `
  -ExampleProject out/release-qa/ui-spiral/project.json `
  -ExampleTerrain out/engine-final/spiral-fixture/Cerro_40_por_ciento.obj
```

El constructor usa el Python del entorno de compilación únicamente para
inspeccionar las tablas de importación PE, incluidas las importaciones
diferidas. Copia recursivamente solo las DLL necesarias, comprueba que no haya
dependencias sin resolver y conserva las licencias y recetas exactas de los
paquetes propietarios de cada binario. No copia el entorno completo.

Las salidas son `entrega/RouteTec/RouteTec-Setup-1.0.0.exe`, el ZIP portátil y
`Distribucion.json`, con tamaños y hashes. El staging se conserva en `out/package-*`
para inspección. No se incluye ningún directorio de trabajo personal.

## Comprobar instalación y desinstalación

```powershell
& packaging/Verify-Installer.ps1 -Installer entrega/RouteTec/RouteTec-Setup-1.0.0.exe
```

La prueba requiere que todavía no haya una instalación registrada de RouteTec
para el usuario. Instala por CLI en una carpeta nueva de QA, comprueba los hashes,
ejecuta la prueba gráfica completa con `PATH` limitado a Windows y sin las
variables de Qt del desarrollador, y desinstala por CLI. Un archivo de proyecto
creado por la prueba permanece en la carpeta para comprobar su conservación.
El registro JSON de evidencia y las capturas se conservan en `out/installer-qa-*`.

La instalación normal es por usuario en `%LOCALAPPDATA%\Programs\RouteTec` y
no necesita elevación. Crea accesos del usuario en Inicio y escritorio. La
desinstalación elimina únicamente los archivos incluidos en el manifiesto del
paquete; los proyectos deben guardarse en Documentos u otra carpeta personal.
No se desinstalan bibliotecas compartidas del sistema.

## Requisitos del usuario

Windows 10 versión 1809 o posterior o Windows 11, arquitectura x64 y controlador
gráfico compatible con OpenGL 3.2. Se recomiendan 16 GB de RAM para topografías
grandes y 500 MB de espacio para instalar y conservar el instalador. Estas
recomendaciones de memoria son orientativas, no un límite medido de todas las
topografías. No se requieren runtimes de .NET o Java.

## Componentes y avisos

`deploy_runtime.py` prepara el runtime y manifiesto; `RouteTec.nsi` define el
instalador en español; `RouteTec.rc` aporta el icono y versión nativos;
`generate_icon.py` reproduce el icono con la biblioteca estándar de Python;
`LEEME.txt` y `TERCEROS.txt` se distribuyen con el producto.

Referencias técnicas: [plataformas Qt 6.10](https://doc.qt.io/qt-6.10/supported-platforms.html),
[despliegue Windows de Qt](https://doc.qt.io/qt-6/windows-deployment.html),
[redistribución Visual C++](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files),
[licencia NSIS](https://nsis.sourceforge.io/Docs/AppendixI.html).
