# RouteMine / RouteTec 3D — núcleo y primer generador CPU

Base nueva C++20 para Windows y Linux, derivada del análisis autorizado de
`RouteTec_3D_01.py`. No es una traducción completa del programa de 10259 líneas.
No se descargó ni ejecutó el Python original.

## Disponible

- Biblioteca CPU sin dependencias externas: XYZ `double`, origen visual local,
  distancias, pendientes, consulta baricéntrica sobre caras TIN originales.
- Validación de pendiente, cobertura continua por cruces de caras originales,
  corte/relleno independientes y separación en modo superficie.
- Distingue comprobaciones aprobadas de restricciones no evaluadas: nunca emite
  factibilidad integral mientras falten verificaciones.
- Importador OBJ triangulado estricto y generador XY/Z de corredores y rasantes
  por Dijkstra, con costo económico configurable.
- Configuración central básica/avanzada con perfiles sintéticos, overrides,
  visualización y snapshot por proyecto; CLI anterior conservada.
- CLI con ejemplos sintéticos UTM/económicos y 28 pruebas CTest.
- Evaluador económico configurable: longitud/ancho, corte suelo/roca, relleno,
  reutilización, importación, disposición y acarreo. Es comparación preliminar
  de candidatos suministrados o generados en una malla discreta.
- Backend CUDA opcional para pendientes por lote, escrito pero no compilado aquí.
- Integración opcional Qt 6/VTK con escena sintética, escrita pero no compilada aquí.

No hay todavía importación DXF, interfaz de producción,
exportación DXF/JSON, índices espaciales ni certificación de curvas/secciones.
Las consultas CPU recorren todas las caras: referencia de exactitud, no solución
de rendimiento para millones de triángulos. Se rechazan caras verticales o
degeneradas en XY y superficies solapadas de cotas contradictorias.

## Ejecutables

Véase [Distribución y ejecución](bin/README.md) para descargar y usar la CLI CPU.
Esta versión es una herramienta de consola preliminar; la aplicación gráfica
completa continúa pendiente.

## Compilar y comprobar

Desde una consola con CMake y compilador disponibles:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

En Windows Visual Studio: `build/Release/routemine_cli.exe --demo`.
En Linux con generador de una configuración: `build/routemine_cli --demo`.
Use `--cost-demo` para comparar candidatos fijos con precios ficticios.
Para generar un corredor nuevo desde OBJ y una solicitud configurable:

```sh
build/Release/routemine_cli.exe --plan fixtures/ridge.obj fixtures/ridge.request build/route.csv
```

Con solo coordenadas A/B y perfiles sintéticos predeterminados:

```sh
build/Release/routemine_cli.exe --plan-basic fixtures/ridge.obj fixtures/basic.request build/basic.csv
build/Release/routemine_cli.exe --show-config fixtures/ridge.obj fixtures/basic.request
```

Cada planificación muestra y guarda la configuración efectiva en
`salida.csv.project.conf`. No hay perfil vehicular real aprobado todavía.

El ejemplo es sintético. La CLI escribe el CSV indicado (reemplaza ese archivo
si ya existe), pero rechaza usar como salida alguno de los archivos de entrada.
El código de salida 0 significa corredor preliminar generado; 3 significa sin
camino en la malla elegida; 4 presupuesto agotado; 2 error de datos/configuración.
La validación Linux adicional se documenta en [Revisión del repositorio](docs/REVISION_REPOSITORIO.md).

Con Qt 6 y VTK compilado para Qt 6 ya instalados:

```sh
cmake -S . -B build-viewer -DROUTEMINE_BUILD_VIEWER=ON -DCMAKE_PREFIX_PATH="<Qt>;<VTK>"
cmake --build build-viewer --config Release
```

Con toolkit CUDA compatible con el compilador y controlador ya instalado:

```sh
cmake -S . -B build-cuda -DROUTEMINE_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build build-cuda --config Release
```

`75` corresponde únicamente a la RTX 2070 inspeccionada. No se descargan paquetes
durante CMake. Los backends se eligen explícitamente; `grades_cuda` propaga errores
de GPU y el programa predeterminado usa CPU. No hay pruebas GPU ejecutadas aún.

## Documentación

- [Parámetros ignorados y evidencias](docs/PARAMETROS_LEGACY.md).
- [Arquitectura y migración](docs/ARQUITECTURA.md).
- [Validación y entorno](docs/VALIDACION.md).
- [Modelo económico y caso sintético](docs/MODELO_ECONOMICO.md).
- [Generación CPU, OBJ, configuración y límites](docs/GENERADOR_CPU.md).
- [Modo básico, avanzado y persistencia](docs/CONFIGURACION.md).

El código está versionado en GitHub. El estado inicial conserva evidencia de pruebas Windows anteriores.
