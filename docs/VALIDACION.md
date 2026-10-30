# Entorno y validación

## Dos propuestas y límites independientes: 30 de octubre de 2026

- Compilación Release de núcleo, CLI, pruebas y visor Qt/VTK completada.
- CTest: **76/76**, con tres casos nuevos: perfil superficial sobre una loma,
  comparación de dos propuestas con las mismas restricciones geométricas y
  comprobación independiente del límite de relleno. Se comprueba también que
  cero corte o un corte insuficiente rechacen un eje que necesita excavación.
- Prueba gráfica `--smoke-proposals fixtures/proposal-hill.obj`: ambas opciones
  seleccionables antes de generar; opción 1 sin movimiento de tierras; cálculo
  independiente de opción 2; cambio entre resultados conservados; comparación
  de longitud; cancelación sin pérdida; exportación/reapertura de la selección;
  invalidación de ambas al editar parámetros; generación con corte prohibido.
- Caso sintético, pendiente máxima 2 %, radio mínimo 50 m, ancho 8 m, límites
  2 m y adaptación desactivada: opción 1 **272.710640 m**, corte 0 m y relleno
  numérico menor de 1e-15 m; opción 2 **200 m**, corte 1 m y relleno 0 m.
  Con corte máximo 0, se obtiene otro eje de 200 m, corte 0 m y relleno 0.6004 m,
  pendiente máxima 1.998 %, sin aumentar los límites. No es un proyecto real.
- Pruebas gráficas anteriores `--smoke-test fixtures/ridge.obj` y
  `--smoke-road-options fixtures/hill-15.obj` aprobadas: menús, ventanas,
  progreso, cancelación, exportación, reapertura, muestreo, restablecimiento,
  migración y recálculo. En la segunda se reponen explícitamente los 6 m del
  fixture después de comprobar el restablecimiento inicial de 2 m.
- Coordenadas CSV contrastadas con el JSON mediante lector Python independiente.
  Capturas revisadas: selector, ejes y referencia, resultados y límites avanzados.
  Evidencias locales: `out/terrain-options-qa`, `out/terrain-options-basic-qa`
  y `out/terrain-options-road-qa`.

El lanzador apunta a `out/viewer/Release/terrain-options/routemine_viewer.exe`.
Se conservó la instancia anterior de `Release/ramps` abierta durante la actualización.
Las comprobaciones no certifican plataforma, estabilidad de taludes ni geometría
vehicular integral, y no se repitió la matriz completa de terrenos reales.

## Curvas permanentes y comparación de candidatos: 29 de octubre de 2026

El visor mantiene espirales y curvas contiguas activas, también al importar
proyectos anteriores. La búsqueda compara candidatos por costo total validado
y desempata por longitud; conserva el mejor candidato al refinar.

- Compilación Release de núcleo, pruebas y visor completada.
- CTest: **72/72**, incluidas curvas contiguas, separación, radios, pendiente,
  cancelación, persistencia y comparación por costos. `ramp_objective` comprueba
  invariancia al cambiar la unidad monetaria y que refinar no pierda el candidato.
- Prueba gráfica con `fixtures/ridge.obj`: cálculo, exportación CSV/DXF/JSON,
  reapertura, cancelación, presupuesto de muestreo, restablecimiento, opción
  permanente y migración de proyecto anterior con recálculo.
- Caso real `fixtures/surf-selected.request` sobre `Surf_2027_FB.dxf`: ruta
  validada de **10517.247180 m**, pendiente máxima **9.990000047 %**, radio
  mínimo **50 m**, corte **12.677711 m**, relleno **30 m**, 493 primitivas de
  curva, 10760 puntos. Tiempo local: **53.32 s**. La comparación completa usó
  2941827 expansiones y 26476331 comprobaciones, sumadas entre intentos.

Evidencias locales: `out/always-curves-smoke`, `out/always-curves-smoke.log`
y `out/always-curves-selected.metrics.json` con sus CSV/DXF. Esta revisión no
repitió la matriz de 46 casos que sigue; esos resultados corresponden a la
selección anterior. No se acredita óptimo global ni diseño vial completo.

## Matriz multipunto ampliada: 29 de octubre de 2026

Se probaron **46 combinaciones dirigidas**, 23 por DXF, conservando pendiente
máxima 10 %, radio mínimo 50 m, ancho 8 m y corte/relleno máximo 30 m. Por terreno
se seleccionaron 13 puntos: máximo y mínimo exactos válidos de la TIN, centro
de una zona de cota mínima, nueve posiciones distribuidas en una cuadrícula
espacial y un punto cercano al centro. Se probaron los extremos en ambos
sentidos, nueve accesos al fondo, cuatro conexiones diagonales/transversales
en ambos sentidos y una conexión corta en ambos sentidos.

| Terreno | Casos | Resueltos y revalidados | Sin resolver por presupuesto |
|---|---:|---:|---:|
| Surf_2027_FB.dxf | 23 | 17 | 6 |
| Topografia_Campo.dxf | 23 | 23 | 0 |
| Total | 46 | 40 | 6 |

Los máximos/mínimos exactos se resolvieron en ambos sentidos:

| Terreno | Z máxima (m) | Z mínima (m) | Horizontal máximo-mínimo (m) | Pendiente máxima (%) |
|---|---:|---:|---:|---:|
| Surf | 4447.5 | 3420 | 10811.7354 | 9.99000005 |
| Topografia | 4682.5 | 4237.5 | 5750.6481 | 9.99000004 |

El mínimo exacto es un vértice original y difiere en XY del punto interior de
cota mínima usado en pruebas anteriores. Ambos se incluyen: no se sustituye
el destino de un caso para facilitar la conexión.

La primera ronda obtuvo 30/46. Las pruebas llevaron a adelantar la detección
de cruces, explorar también desde el extremo bajo, permitir el giro necesario
en el enlace final, refinar XY automáticamente y evitar vueltas cuando la recta
ya cumple. La ronda final obtuvo los 40/46 indicados, con los mismos límites
físicos. Estos ajustes no hacen exhaustiva la búsqueda ni acreditan optimalidad.

Pendientes de Surf: SO, NO, N y NE hacia el fondo, y la diagonal SO-NE en ambos
sentidos. Los identificadores del CSV son `SW_to_FLOOR`, `NW_to_FLOOR`,
`N_to_FLOOR`, `NE_to_FLOOR`, `SW_to_NE`, `NE_to_SW`. Todos agotaron recursos;
no hubo errores de ejecución ni rutas inválidas publicadas. El límite del
arnés fue 30 s cooperativos por caso; ningún caso terminó por ese temporizador.
Un presupuesto agotado no demuestra imposibilidad geométrica.

Evidencias finales locales:

- `out/real-matrix-final/combined-results.csv`: los 46 casos y métricas.
- `out/real-matrix-final/{surf,topografia}/points.csv`: coordenadas de los 26
  puntos seleccionados, sin modificar los DXF originales.
- En cada carpeta, una solicitud `.request` por caso y CSV/DXF de cada caso
  resuelto. Se llamó de nuevo al validador del núcleo y se comprobaron A/B.
- `out/real-matrix-final/audit.json`: auditoría externa de los 40 CSV/DXF,
  195309 puntos exportados; XYZ coincidentes, cero errores y cero reparaciones
  DXF. Pendientes calculadas desde los XYZ dentro de 10 %; métricas de radio,
  corte y relleno dentro de los límites con tolerancia numérica.
- `out/real-matrix-final-ctest.txt`: **71/71 CTest** aprobados.
- Tres flujos gráficos de la versión actual: extremos exactos de ambos DXF y
  los A/B de la sesión del usuario. Cálculo, exportación, reapertura,
  cancelación, restablecimiento y recálculo terminaron con salida 0.
  Capturas TOP/ISO inspeccionadas. Los casos extremos están en
  `out/real-matrix-final/gui-surf-extremes` y `gui-topografia-extremes`.

Para repetir esta matriz con la TIN cargada una vez por archivo:

```powershell
out/viewer/Release/routemine_real_matrix.exe D:/Downloads/RouteTec/Inputs/Surf_2027_FB.dxf out/real-matrix-repeat/surf
out/viewer/Release/routemine_real_matrix.exe D:/Downloads/RouteTec/Inputs/Topografia_Campo.dxf out/real-matrix-repeat/topografia
```

Se valida el eje muestreado y su separación. Sección transversal completa,
envolvente del vehículo, peralte y geotecnia siguen fuera de esta validación.

## Rampas continuas sobre insumos reales: 29 de octubre de 2026

Se probaron los dos DXF originales de `D:/Downloads/RouteTec/Inputs` sin
modificarlos. Además se recuperaron por accesibilidad, sin editar la ventana
anterior, los A/B que permanecían cargados en la captura del usuario.

| Caso | Origen Z (m) | Destino Z (m) | Horizontal (m) | Pendiente máxima (%) | Radio mínimo (m) | Corte máximo (m) | Relleno máximo (m) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Surf, A/B de la sesión del usuario | 4447.5 | 3435 | 10488.6414 | 9.99000005 | 50 | 12.6777 | 30 |
| Surf, punto alto a fondo mínimo | 4447.5 | 3420 | 11047.3563 | 9.99000009 | 50 | 26.3613 | 30 |
| Topografia, punto alto a mínimo | 4682.5 | 4237.5 | 5289.3744 | 9.99000017 | 50 | 1.6557 | 25.9690 |

La trayectoria Surf recorre aproximadamente 1.49 vueltas netas alrededor del B
elegido por el usuario; el caso al fondo mínimo recorre unas 1.72. Es una rampa
envolvente adaptada al relieve, no una elipse matemática impuesta. Se permiten
pequeñas subidas locales (8.49 m acumulados en el caso seleccionado) para
conectar el relieve; la pendiente de cada tramo mantiene su límite.

Comparación reproducible con el modo anterior: pendiente 10 %, radio 50 m,
recta mínima 85 m y giro aislado máximo 120°. Ese modo, que exige seguir la
superficie, no halló ruta para ninguno de los pares alto-mínimo en las
resoluciones exploradas: Surf 52 expansiones / 86611 comprobaciones;
Topografia 0 expansiones / 434 comprobaciones. Esto no demuestra inexistencia
de una carretera. La combinación de curvas continuas y rasante con movimiento
de tierras sí produjo los tres resultados de la tabla. No se afirma óptimo global.

Validación final de esta revisión:

- Compilación Release MSVC de núcleo, CLI, pruebas y visor Qt 6/VTK.
- **70/70 CTest**: incluye cuenca, bancos, sentido inverso, curvas enlazadas sin
  tangentes forzadas, cruces/superposición, cancelación, límites y exportación.
- Tres flujos gráficos completos, uno por cada fila: cálculo, invalidación,
  CSV/DXF, reapertura de JSON con revalidación, cancelación, bloqueo de muestreo
  incompatible, restablecimiento y recálculo. Todos terminaron con salida 0.
  En los tres se probó el botón de destino automático en cota mínima.
- Inspección visual de vistas TOP e ISO de las rutas reales.
- Auditoría externa ezdxf 1.4.3: respectivamente 10723, 11446 y 6757 puntos;
  XYZ idénticos en JSON/CSV/DXF; cero errores y cero reparaciones. CSV identifica
  `RAMPA_PROYECTO` y DXF utiliza `ROUTEMINE_RAMPA`. Radio comprobado desde los
  centros de arco. Sin pares de puntos exportados de tramos no contiguos a
  distancia horizontal menor que 8 m; el núcleo comprueba también segmentos.

Evidencias locales: `out/ramp-research/*.metrics.json`, `export-audit.json`,
`ctest.txt` y carpetas `out/ramp-qa-selected`, `out/ramp-qa-surf`,
`out/ramp-qa-topografia`. Las solicitudes se versionan como
`fixtures/surf-selected.request`, `surf-ramp.request`, `topografia-ramp.request`.
`Abrir-Rampa-Probada.bat` abre directamente el proyecto con los A/B de la sesión.

Comandos para repetir las pruebas reales del núcleo, con las dependencias ya
compiladas y los DXF disponibles en su ubicación local:

```powershell
out/viewer/Release/routemine_ramp_bench.exe plan D:/Downloads/RouteTec/Inputs/Surf_2027_FB.dxf fixtures/surf-selected.request out/ramp-research/repeat-selected
out/viewer/Release/routemine_ramp_bench.exe plan D:/Downloads/RouteTec/Inputs/Surf_2027_FB.dxf fixtures/surf-ramp.request out/ramp-research/repeat-surf
out/viewer/Release/routemine_ramp_bench.exe plan D:/Downloads/RouteTec/Inputs/Topografia_Campo.dxf fixtures/topografia-ramp.request out/ramp-research/repeat-topografia
```

SHA-256 de los insumos usados:

- Surf_2027_FB.dxf: `76EFBAFDFABB87F987A7163D9FDB5569C72E036BC81F369FA8F7E7703C078D92`.
- Topografia_Campo.dxf: `76AF8E40B0D0AA7CC3DD227508202665CFAA929D76C7FD95E9455F0C96B6F10D`.

Los resultados validan el eje poligonal muestreado y las restricciones indicadas.
La sección transversal completa, estabilidad, drenaje, visibilidad, peralte,
envolvente vehicular y transiciones siguen requiriendo revisión de ingeniería.

## Tangentes y curvas: 29 de octubre de 2026

- Compilación Release de núcleo, CLI y visor Qt/VTK en `out/viewer`.
- 61/61 pruebas CTest: geometría, ángulo total por curva, recta libre, radio,
  manipulación de XYZ/metadatos, horquilla de 180°, búsqueda en UTM, falta de
  espacio, presupuestos, cancelación y recuperación JSON, además de las anteriores.
- Flujo gráfico `--smoke-request fixtures/flat-hole.obj fixtures/alignment.request`:
  cálculo, invalidación al editar, exportación, reapertura, cancelación,
  bloqueo de muestreo incompatible, restablecimiento y recálculo. Salida 0.
  Este flujo con solicitud no ejercita el picking 3D, aunque el mensaje genérico
  del arnés también lo mencione.
- Inspección visual de `out/alignment-qa/route.png` y `advanced-reset.png`:
  controles visibles, texto legible y curvas redondeadas alrededor del hueco.
- Auditoría externa con ezdxf 1.4.3: 122 puntos idénticos en JSON/CSV/DXF,
  cero errores y cero reparaciones. Cuatro curvas, radio mínimo obtenido 50 m,
  recta mínima obtenida 84 m, giro máximo obtenido 45°. Longitud horizontal
  1152.1617944 m sobre terreno plano sintético.

La prueba no acredita una solución sobre la topografía de las capturas del
usuario. Se valida el eje muestreado; faltan envolvente y sección del vehículo,
peralte, transiciones, autointersecciones y estabilidad. Con geometría activa
no se ofrecen los antiguos atajos rectos de corte, que romperían empalmes.
Evidencias locales en `out/alignment-qa` (ignoradas por Git); fixtures y pruebas
automatizadas en el repositorio.

## Dos opciones de trazado — 23 de octubre de 2026

Búsqueda de escritorio sobre XY/TIN y cotas de borde generadas bajo demanda;
evita el producto XY/Z que producía `Lattice exceeds max_states`.
Segunda propuesta con tramos de corte/nivelación rojos, referencia negra y
umbral individual estricto >5×. Margen angular adicional inicial 0°.
CTest: 52/52 aprobados, incluyendo continuidad UTM, cambio de dirección sobre aristas,
pendientes, cerro de 15°, profundidad máxima, rechazo por relación de distancias
y recuperación/verificación de las intervenciones del JSON.

En `Topografia_Campo.dxf`, prueba real entre (781600, 8443700, 4468.4969573927801)
y (781300, 8443100, 4537.7434683154579), pendiente máxima 12 %: se generó y
validó un trazado de aproximadamente 838 m. Se probó en el visor su cálculo,
exportación, invalidación, reapertura y cancelación. Una prueba adicional entre
centros de caras alejadas produjo 4534.23 m horizontales (4556.39 m en 3D),
con pendiente máxima medida de 11.99886926 %. Este caso también completó el
cálculo, exportación y reapertura en la interfaz.
Son casos concretos; no acreditan factibilidad universal ni geometría vehicular.

El fixture `hill-15.obj` produjo una comparación de aproximadamente 641 m frente
a 40 m, con corte máximo 5.35898 m y pendiente vial de 0 % en el atajo. En ese
fixture se configuró corte permitido de 6 m; el default de proyecto sigue en
5 m. El DXF distingue la intervención en la capa roja `ROUTETEC_CORTE`.
Las pruebas gráficas finales de ruta básica, propuesta y terreno real terminaron
con código 0. Sus CSV, DXF y JSON conservan los mismos XYZ; auditoría independiente
con ezdxf 1.4.3: cero errores y cero reparaciones, incluido el color de la capa roja.
Evidencias locales: `out/gui-basic-final`, `out/gui-options-final`, `out/gui-real-long`.
Véase RUTAS_Y_CORTES.md y CTEST_RUTAS_WINDOWS.txt.

## Adaptación local RouteTec — 23 de octubre de 2026

En `D:\Proyectos\routemine`, compilación Release con MSVC 19.44 y Qt 6.10.2 /
VTK 9.5.2 de un entorno conda-forge local `out/viewer-deps`. Núcleo, CLI, visor y
pruebas compilados. CTest: 40/40. Prueba gráfica: picking 3D, registro manual,
cálculo, invalidación, CSV/DXF/JSON, reapertura de proyecto y cancelación.
Exportaciones contrastadas entre sí y con ezdxf 1.4.3 (auditoría sin errores ni
reparaciones). Los dos DXF de `D:\Downloads\RouteTec\Inputs` se cargaron y
renderizaron: 174560 y 200607 triángulos válidos; el segundo omite 611 caras
degeneradas en XY con conteo visible. No se probaron rutas reales arbitrarias,
CUDA, Linux ni geometría vehicular. Véase INTERFAZ_ROUTETEC.md.

Los apartados siguientes conservan el historial de verificaciones anteriores.

Equipo: LAPTOP-A0ICMCF6. CPU Intel Core i7-10750H. RAM no confirmada porque
la consulta CIM fue rechazada. GPU RTX 2070 Max-Q 8192 MiB, driver 546.33,
compute capability 7.5 (`nvidia-smi`).

Toolchain comprobado: MSVC x64 19.44.35228, Visual Studio Build Tools 2022,
CMake 3.31.6. Los ejecutables no estaban todos en PATH; CMake se invocó por su
ruta dentro de Build Tools. Sin instalaciones ni descargas de dependencias.

No existía `D:\Proyectos\Fase 1\routemine` al iniciar. No se encontraron
AGENTS.md en D:\, D:\Proyectos ni D:\Proyectos\Fase 1. No había Git ni rama
de RouteMine que preservar. No se aplican acuerdos de rama de TrazMAPE.

Pruebas: origen UTM, pendiente, entradas inválidas/presupuesto de
muestras, malla, huecos, cotas superpuestas, corte/relleno asimétricos, modo
superficie, restricciones no verificadas, backend CPU, comparación económica
y balance de corte/relleno con acarreo. Usan comprobaciones
activas también en Release (no dependen de `assert`).

Limitaciones: no ejecutado en Linux; CUDA y Qt/VTK no compilados ni ejecutados;
sin DXF real ni benchmark de rendimiento. El Python fuente se consultó mediante
Library y no se ejecutó ni reconstruyó localmente. No se tocaron otras carpetas
de proyectos, bases de datos, credenciales, remotos ni producción.

Resultado realizado en carpeta final: compilación Release correcta con MSVC y
CTest 12/12 aprobado (0 fallos). Véanse CTEST_WINDOWS.txt y DEMOS_WINDOWS.txt.
Comando de build usado: cmake --build build --config Release -- /m:1 /nr:false.
La primera compilación paralela del workspace no terminó oportunamente y se
interrumpió; la compilación serial posterior y la del destino terminaron sin errores.
Los ejemplos verificaron 100 m / 10 % y la elección económica 1150 < 1600 frente
al cambio de elección cuando el costo directo pasa a 2500 > 1600.

Segundo bloque verificado: compilación Release del destino y CTest 21/21
aprobado (0 fallos), incluyendo OBJ, generación económica, perfil variable,
imposibilidad, recursos, discontinuidades, coordenadas grandes y CLI con CSV.
La cobertura continua se comprueba ahora por cruces de caras originales, además
del muestreo uniforme. Véanse CTEST_WINDOWS.txt y GENERADOR_WINDOWS.txt.
Resultados sintéticos generados: tarifa de corte 1 => 100 m, 150 m³, 1150 CU;
tarifa 20 => aproximadamente 136.569 m, 2.652 m³, 1418.72 CU. No son volúmenes
finales ni una carretera con geometría vehicular verificada. No hubo instalaciones.

Configuración básica/avanzada verificada: Release correcto y CTest 28/28,
incluyendo defaults, overrides, rechazo de configuraciones inválidas/no
implementadas, persistencia, compatibilidad legacy y CLI básica/mostrar.
Recarga adicional del snapshot de proyecto produjo un CSV idéntico (SHA256)
al de la planificación original. Véase CONFIGURACION_EJEMPLO.txt.
Perfil inicial mining_haul_provisional / haul_truck_unspecified: contexto minero
confirmado, sin dimensiones/modelo de vehículo aprobados; números y costos
siguen siendo sintéticos, con advertencias explícitas. No hubo GUI ni instalaciones.

### Cruces TIN casi coincidentes (30/10/2026)

Se reprodujo `Degenerate or overflowing segment` en un perfil sintético con coordenadas UTM al cruzar caras adyacentes: parámetros de intersección distintos por redondeo generaban segmentos XY de hasta 1e-8 m. `segment_profile` ahora unifica esas muestras únicamente cuando sus cotas coinciden dentro de 1e-8 m; comprueba la cobertura de todos los intervalos originales y conserva el extremo final. Las discontinuidades verticales no se fusionan. La reconstrucción de rampas descarta candidatos con segmentos degenerados antes de validar, sin suspender toda la búsqueda.

Regresión `interface_profile_roundoff`: verifica ambos sentidos del perfil y su validación. También se verificó el perfil A/B observado sobre `Surf_2027_FB.dxf` (391 puntos en cada sentido). Esta comprobación del perfil no certifica la existencia de una carretera factible entre esos extremos. Suite completa: 78/78 pruebas.

## Ajustes, refinamiento local y clasificación (30-10-2026)

- Suite Release: **91/91**. Incluye orden/límites de ajustes, radio/recta/pendiente,
  cancelación, ambos modos, recta antes de giro inverso, rechazo de relajaciones
  fuera de un intervalo, persistencia de intervalos/historial y clasificación de
  corte/relleno incluso cuando cambia el signo dentro de una arista.
- Prueba nativa `--smoke-features`: picking de P1/P2 sobre el actor de carretera,
  diálogo y aplicación de parámetros locales, mejora 822.648 → 338.184 m en fixture
  sintético plano, validación/reapertura de intervalos, visualización/CSV/DXF
  clasificados, diagnóstico, ajustes en ambos modos y persistencia efectiva.
- Matriz real: **26 casos**, dos DXF y ambos modos, hasta 30 s por caso y recursos
  acotados (30000 estados, 6000 expansiones, 70000 aristas iniciales por variante;
  ampliados por el controlador). Resultado: **9 rutas validadas, 14 presupuestos
  agotados y 3 tiempos agotados**, sin excepciones. No equivale a 26 carreteras
  resueltas ni demuestra imposibilidad de las otras 17.
- Surf MIDDLE_LOW con límites de corte/relleno de 2 m: 893.512 m, pendiente máxima
  10.989 %, radio exigido 30 m y recta exigida 35 m; solución en el séptimo intento.
- Comparación explícita independiente con los A/B observados en Surf: pendiente
  12 %, radio 30 m, recta 35 m y corte/relleno permitidos 30 m. Encontró una ruta de
  **9029.817 m**, pendiente máxima 11.9880001 %, 222 primitivas curvas, corte máximo
  30 m y relleno máximo 18.6945 m. Es una prueba con otros límites de profundidad;
  **no se cambiaron los valores iniciales de 2 m ni se demuestra que sean imposibles**.

Los registros y capturas están en `out/adaptive-qa`, con resultados CSV por terreno,
historiales por caso y JSON del ensayo gráfico. El corte de ocho candidatos se
sustituyó por un presupuesto visible; el estado final conserva cualquier agotamiento
registrado en las variantes. La guía de relieve se reutiliza entre variantes y las
restricciones de profundidad se exploran contra la TIN original.

La ruta real de 9029.817 m también se reabrió y revalidó en el ejecutable de escritorio, con capturas TOP/ISO y colores de corte/relleno. Proyecto de ensayo: `out/adaptive-qa/real-gui/validated-project.json`; no sustituye los límites iniciales ni certifica la obra.


## Búsqueda visible — 30 de octubre de 2026

- Visor Release compilado en `out/viewer/Release/terrain-options/routemine_viewer.exe` (seleccionado por `Abrir-RouteMine.bat`).
- CTest: **93/93 aprobadas**, con las DLL de Qt disponibles en PATH. Incluye `ramp_trace`, que comprueba ramas, candidatos completos, extremos y equivalencia exacta de XYZ, expansiones y aristas con el observador desactivado; y `search_trace_mailbox`, que comprueba transferencia desde el trabajador, límites de memoria, deduplicación y conservación de extremos al simplificar la vista.
- `--smoke-search`: búsqueda real sobre una cuenca sintética, actualización durante el trabajo, resultado validado, ocultar/restaurar historial y capturas revisadas. Archivos en `out/search-visual-check` (`search-live.png`, `search-complete.png`, `search-hidden.png`, `search-restored.png`); ejecución final con código 0.
- `--smoke-test`: progreso, cancelación sincronizada, exportaciones, reapertura, restablecimiento y recálculo; código 0. Capturas en `out/search-regression`.
- `--smoke-features`: selección sobre la ruta, optimización por tramo, validación/reapertura, colores/exportaciones de corte y relleno y ajustes automáticos en ambos modos; código 0. Capturas en `out/search-features-check`. El ensayo fija explícitamente el radio base de 50 m que compara con el radio local de 30 m, independientemente del valor inicial de la interfaz.

El historial gráfico muestra ramas reales del buscador y candidatos completos verificados, con actualización por lotes. Conserva como máximo 3000 ramas y 16 alternativas; no constituye un registro exhaustivo ni garantiza un óptimo global. La comprobación visual se realizó sobre terrenos sintéticos; no se repitió la matriz de topografías reales para este cambio de visualización.


### 2026-10-30: falso fallo de cobertura en aristas UTM

El tramo reportado en Surf_2027_FB.dxf, desde (791612.7352246094,
8441460.638916016,3982.5) hasta (791612.5738476563,8441439.497070312,
3980.484375000001), es una arista original de la TIN. La tolerancia baricéntrica
fija rechazaba 4336 de 10001 muestras interpoladas. La contención ahora controla
el error de redondeo en unidades XY (ULP de las coordenadas), sin ampliar todas
las caras por una fracción fija. Tras el cambio pasan las 10001 consultas; siguen
rechazándose huecos reales y solapes con cotas incompatibles.

Regresiones: `utm_seam`, `road_coverage_rejection`, `road_candidate_guide`.
El grafo comprueba el perfil completo de cada arista y descarta candidatos que no
pasan la validación antes de elegir estrategia. La búsqueda unificada también puede
continuar sin corredor A–B mediante una guía de anclaje explícitamente no validada,
que no se dibuja ni exporta como carretera; se guardan y reabren únicamente sus
alternativas verificadas. `--smoke-guide-fallback` comprobó dos parches desconectados.
La prueba `--smoke-candidates` con los A/B de la captura encontró 18 ramas verificadas,
sin afirmar una conexión vial completa. CTest: 123/123. Evidencia en
`out/coverage-fix-qa/`.
