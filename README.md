# RouteTec — planificación de carreteras en terreno 3D

Aplicación C++20 con interfaz Qt 6/VTK para explorar carreteras sobre una TIN,
incluyendo recorridos sobre superficie y alternativas con corte o relleno.
La búsqueda ensaya curvas tangentes, serpentines, horquillas y espirales,
intenta conectar A/B y conserva la mejor conexión encontrada mientras sigue
buscando. La CLI conserva también los generadores anteriores.

En este equipo, abra **`Abrir-RouteTec.bat`**. Cargue el DXF, registre A/B o
selecciónelos sobre la TIN y ajuste la pendiente máxima. La interfaz permite
buscar/detener, consultar resultados, exportar CSV/DXF y guardar/reabrir JSON.
Pulse **BUSCAR RUTAS POSIBLES**: la exploración continúa hasta detenerla, conserva
los sectores anteriores y dibuja también tramos que todavía no conectan A/B.

En **Configuración > Diseño** se seleccionan velocidad, orografía, altitud,
peralte de referencia y riesgo de hielo. La velocidad inicial es **20 km/h**,
identificada como exploración preliminar: no existe esa fila de tercera clase
en la Tabla 204.01 de DG-2018. El perfil DG2018 tercera clase exige una combinación
tabulada y aplica corrección de pendiente por altitud, radio calculado y mínimos
S/O de tangente. Las curvas contiguas del mismo sentido siguen permitidas.
El radio operativo inicial es 30 m; la recta solicitada, 35 m; la pendiente,
10%; y los límites independientes de corte y relleno, 2 m. Los valores efectivos
dependen del perfil seleccionado. La intervención continua inicial de 100 m es
configurable e independiente del límite de 2× pendiente para explorar.

La leyenda muestra **gris** para exploración sobre la TIN; **rojo/verde** para
exceso ascendente/descendente de pendiente, hasta 2× la máxima y en el sentido
del trazo; y **celeste** para alternativas que superan las comprobaciones
implementadas del eje. Las huellas de exploración y de recorridos verificados
continúan acumulándose, con memoria acotada, aunque se complete la lista de
geometrías guardables. Las comprobaciones de vehículo, sobreancho, visibilidad,
peralte construido y estabilidad siguen pendientes: no es un diseño aprobado
ni se garantiza enumerar todas las rutas o encontrar el óptimo global.

La pantalla principal contiene carga, extremos A/B, búsqueda y resultados.
Los parámetros están en **Configuración**. **Puntos intermedios**
se habilita desde ese menú y muestra cuatro botones en dos filas: Marcar P1,
Marcar P2, Optimizar y Limpiar. Sus parámetros se editan en el mismo diálogo.
La exploración se muestra desde **Vistas**; la información ampliada y los
diagnósticos localizados en español están en **Ayuda > Detalles del resultado**.

El empaquetado Windows se genera con `packaging/Build-Installer.ps1` en
`entrega/RouteTec`, directorio excluido de Git. Los paquetes incluyen los runtimes
y no requieren Python ni Visual Studio en el equipo de destino. Deben regenerarse
para distribuir los cambios de este código.
Véase [la guía de interfaz y sus límites](docs/INTERFAZ_ROUTETEC.md).
Para las propuestas y la compatibilidad con los atajos del modo anterior,
consulte [rutas y cortes](docs/RUTAS_Y_CORTES.md).

## Disponible

- Biblioteca CPU sin dependencias externas: XYZ `double`, origen visual local,
  distancias, pendientes, consulta baricéntrica sobre caras TIN originales.
- Validación de pendiente, cobertura continua por cruces de caras originales,
  corte/relleno independientes y separación en modo superficie.
- Distingue comprobaciones aprobadas de restricciones no evaluadas: nunca emite
  factibilidad integral mientras falten verificaciones.
- Importador OBJ triangulado estricto y generador XY/Z de corredores y rasantes
  por Dijkstra, con costo económico configurable.
- Importador DXF ASCII 3DFACE e índice espacial BVH sobre las caras originales.
  Se cargaron y renderizaron los dos DXF incluidos en RouteTec; las caras
  degeneradas en XY se omiten con conteo visible, sin rellenar huecos.
- Configuración central básica/avanzada con perfiles sintéticos, overrides,
  visualización y snapshot por proyecto; CLI anterior conservada.
- Ejemplos sintéticos UTM/económicos y 137 pruebas CTest verificadas en Windows
  con el visor habilitado, además de pruebas nativas de interfaz y DXF real.
- Evaluador económico configurable: longitud/ancho, corte suelo/roca, relleno,
  reutilización, importación, disposición y acarreo. Es comparación preliminar
  de candidatos suministrados o generados en una malla discreta.
- Backend CUDA opcional para pendientes por lote, escrito pero no compilado aquí.
- Interfaz opcional Qt 6/VTK compilada y probada en Windows, con TIN real,
  origen visual local, cotas, picking A/B, cámara, cálculo en segundo plano,
  invalidación de resultados al editar y exportaciones/proyectos atómicos.

Se comprueban curvas, tangencias, rectas y separación del eje; la conformidad
integral y la envolvente vehicular requieren comprobaciones adicionales.
La búsqueda discreta tiene presupuestos explícitos: cargar una topografía grande
no implica que cualquier ruta pueda calcularse con la precisión inicial.
No se admiten DXF binarios ni todos los tipos de entidad DXF; se rechazan cotas
contradictorias al consultar superficies solapadas.

## Ejecutables

Véase [Distribución y ejecución](bin/README.md) para usar los binarios históricos
de la CLI CPU 0.1.0. Esos archivos no incorporan los cambios actuales de RouteTec;
la aplicación gráfica y la CLI actual se compilan desde este código.

## Compilar y comprobar

Desde una consola con CMake y compilador disponibles:

```sh
cmake -S . -B out/core -DBUILD_TESTING=ON
cmake --build out/core --config Release --parallel
ctest --test-dir out/core -C Release --output-on-failure
```

En Windows Visual Studio: `out/core/Release/routemine_cli.exe --demo`.
En Linux con generador de una configuración: `out/core/routemine_cli --demo`.
Use `--cost-demo` para comparar candidatos fijos con precios ficticios.
Para generar un corredor nuevo desde OBJ y una solicitud configurable:

```sh
out/core/Release/routemine_cli.exe --plan fixtures/ridge.obj fixtures/ridge.request out/route.csv
```

Con solo coordenadas A/B y perfiles sintéticos predeterminados:

```sh
out/core/Release/routemine_cli.exe --plan-basic fixtures/ridge.obj fixtures/basic.request out/basic.csv
out/core/Release/routemine_cli.exe --show-config fixtures/ridge.obj fixtures/basic.request
```

Cada planificación muestra y guarda la configuración efectiva en
`salida.csv.project.conf`. No hay perfil vehicular real aprobado todavía.

El ejemplo es sintético. La CLI escribe el CSV indicado (reemplaza ese archivo
si ya existe), pero rechaza usar como salida alguno de los archivos de entrada.
El código de salida 0 significa corredor preliminar generado; 3 significa sin
camino en la malla elegida; 4 presupuesto agotado; 2 error de datos/configuración.
La validación Linux adicional se documenta en [Revisión del repositorio](docs/REVISION_REPOSITORIO.md).

Con Qt 6 y VTK compilado para Qt 6 instalados. El entorno local comprobado
está en `out/viewer-deps`; la guía de interfaz explica cómo restaurarlo:

```sh
cmake -S . -B out/viewer -DROUTEMINE_BUILD_VIEWER=ON -DCMAKE_PREFIX_PATH="<Qt>;<VTK>"
cmake --build out/viewer --config Release
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
- [Interfaz adaptada, DXF, proyectos y validación gráfica](docs/INTERFAZ_ROUTETEC.md).

El código está versionado en GitHub. El estado inicial conserva evidencia de pruebas Windows anteriores.
Los directorios `out/` y `build/` no se versionan. Use un directorio de build
nuevo si un caché preexistente conserva la ruta de otra ubicación del repositorio.
