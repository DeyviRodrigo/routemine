# Interfaz adaptada al monolito local RouteTec

Fuente revisada el 23 de octubre de 2026: `D:\Downloads\RouteTec\RouteTec_3D.py`.
SHA-256: `21f28bc6ff08857bd652ae392d401abc0e33dc00017064f69d66b6ee7a0868b0`.
Se leyó el archivo; no se ejecutó ni modificó el monolito.

## Iniciar

En este equipo, abrir `D:\Proyectos\routemine\Abrir-RouteMine.bat`.
El ejecutable actualizado está en `out/viewer/Release/terrain-options/routemine_viewer.exe`; el lanzador
configura las DLL y los plugins de Qt para ese proceso. Las dependencias están
aisladas en `out/viewer-deps`, dentro del proyecto.

1. Usar **CARGAR TOPOGRAFÍA DXF / OBJ** en la sección **Terreno** del panel izquierdo.
2. Registrar los tres valores XYZ de A y de B, o seleccionarlos sobre la TIN.
3. Abrir **Configuración > Diseño** y ajustar la pendiente máxima y el radio; las curvas contiguas siempre están permitidas. Elegir
   una de las dos propuestas del desplegable y pulsar **CALCULAR RUTA**.
4. Consultar los resultados desplazando el panel izquierdo; usar **Archivo > Exportar ruta CSV** o **Exportar eje DXF**,
   o usar **Archivo > Guardar proyecto** para guardar el JSON. Para guardar un proyecto deben estar registrados A/B.
5. **Archivo > Abrir proyecto** recupera la configuración y vuelve a comprobar el eje
   guardado contra el terreno. Si falta el archivo de terreno, permite localizarlo.

### Dos propuestas seleccionables (30-10-2026)

El desplegable ofrece siempre **1. Propuesta respetando el terreno.** y
**2. Propuesto con corte y/o relleno.**, incluso antes de calcular. La selección
determina qué propuesta genera el botón. La primera sigue las cotas de la TIN,
incluidos los cruces de caras; no diseña excavación ni relleno del eje. La segunda
busca una rasante dentro de los límites independientes de corte y relleno.
Ambas mantienen pendiente máxima, radio mínimo, cobertura y separación entre vueltas;
las curvas contiguas no implican por sí mismas movimiento de tierras.

Configuración avanzada > Diseño contiene **Opción 2 · corte máximo (m)** y
**Opción 2 · relleno máximo (m)**: valores iniciales 2 m cada uno. Se permite desde
0 hasta cada máximo; 0 prohíbe ese movimiento. El rango del control (0–10000 m)
es informático, no una recomendación de profundidades ni un criterio de estabilidad.
La búsqueda no aumenta estos máximos ni la pendiente para conseguir una conexión.
La admisibilidad de las profundidades necesita evaluación del terreno y del diseño.
El ajuste de rodeo/atajo y el margen angular pertenecen al modo anterior de la CLI;
el visor mantiene margen adicional cero en ambas opciones.

Se conserva cada resultado mientras no cambien A/B, terreno o configuración.
Seleccionar una propuesta calculada la muestra inmediatamente; seleccionar una
pendiente muestra **POR GENERAR** y deshabilita exportar hasta obtenerla. Cancelar
una búsqueda o no encontrar un resultado conserva las propuestas anteriores.
Con ambas calculadas, la opción 2 informa el ahorro horizontal en metros y porcentaje;
si no es más corta, lo indica expresamente. No se garantiza el óptimo global.

La primera se dibuja amarilla y se exporta como `SOBRE_TERRENO` / `ROUTETEC_EJE`;
la segunda naranja, como `RAMPA_PROYECTO` / `ROUTEMINE_RAMPA`. Guardar o exportar
usa la propuesta seleccionada. El JSON conserva su modo y al reabrir se valida
contra la TIN original; la otra propuesta se vuelve a generar si se desea comparar.
Los límites guardados en proyectos anteriores se conservan; Restablecer valores
recupera 2 m por movimiento. La plataforma completa y los taludes requieren diseño
posterior, incluso cuando el eje sigue la topografía.

La carga de topografía y el cálculo de rutas abren una **ventana flotante de progreso**
con la actividad, una barra con **porcentaje siempre visible**, el tiempo transcurrido
y **CANCELAR**. El **tiempo restante estimado** aparece primero y destacado, también
al minimizar al panel inferior. Se estima con el tiempo transcurrido y el porcentaje
completado; al inicio muestra «Estimando…», al llegar al 99 % muestra «Finalizando…»
y al cancelar muestra «Cancelando…». Es aproximado y puede aumentar si una etapa
se demora. El porcentaje es un avance estimado según bytes leídos, etapas completadas y
presupuesto de exploración consumido; no se simula a partir del tiempo. Puede
mantenerse en un porcentaje mientras termina una etapa costosa. **MINIMIZAR**, el botón de
minimizar de la ventana, cerrar o Escape llevan el proceso al panel inferior sin
cancelarlo. Allí se muestran el mismo porcentaje y el tiempo y se puede **VER PROGRESO** para restaurar
la ventana o **CANCELAR**. Ambas vistas se ocultan al terminar; cada nueva operación
abre de nuevo la ventana flotante.

## Correspondencia con el monolito

| Flujo observado | Implementación C++ |
|---|---|
| `ROUTETEC_HTML`: Terreno, A/B, Diseño, Exportar/proyecto, Resultados | Panel lateral en español, menú Archivo y ventanas de configuración |
| Cargar DXF sin bloquear el visor | Carga en segundo plano; ventana flotante con porcentaje, tiempo restante estimado destacado, tiempo transcurrido y descarte de una carga cancelada |
| Ingreso obligatorio XYZ y registro independiente de A/B | Se rechazan vacíos, texto no numérico y valores no finitos; se conserva la cota manual |
| Seleccionar A/B en el modelo | Picking sobre las caras de la TIN; sincronización de los tres campos y marcadores A/B |
| Pendiente visible inicial 10 %, rango 0.1–100 % | Perfil central `routetec_monolith`, compartido con la CLI |
| Ancho interno 8 m y banda topográfica fija ±5 m | Ancho inicial 8 m; opción 1 sobre terreno y opción 2 con corte/relleno inicial 2 m, ajustables e independientes |
| Calcular y limpiar | Búsqueda CPU fuera del hilo de interfaz; cancelación cooperativa; limpiar elimina puntos, eje y métricas |
| Modificar un punto | Invalida inmediatamente eje, resultados y exportación; también ocurre al modificar la configuración |
| Exportar CSV | `ID,Progresiva_m,X,Y,Z,Pendiente_tramo_pct`; pendiente con signo y XYZ en doble precisión |
| Exportar DXF | Polilínea 3D en `ROUTETEC_EJE`, puntos y rótulos A/B en `ROUTETEC_A_B`; DXF ASCII R12 |
| Guardar proyecto JSON | Configuración efectiva versionada, terreno, puntos, eje y métricas; escritura atómica |
| Vistas ISO/TOP/BOT/FRONT/BACK/LEFT/RIGHT y zoom | Cámara VTK, navegación orbital, marcadores, eje y leyenda de cotas |

La CLI conserva el motor Dijkstra XY/Z existente. El escritorio utiliza
Hybrid A* con curvas continuas, en modo superficie o rasante con corte/relleno según la selección; se describe en
[RUTAS_Y_CORTES.md](RUTAS_Y_CORTES.md). No se han portado los buscadores de
horquillas ni los suavizados del monolito: no hay equivalencia de trazados entre
ambos motores. La interfaz separa las restricciones comprobadas de las pendientes.

La fuente activa `start_operational_route` envía 8 m y límites de 1e6 al worker,
pero `calculate_route` usa una banda fija de 5 m y no aplica esos límites recibidos.
Por eso el perfil nuevo representa los límites efectivos observados, en vez de
copiar los argumentos inactivos. Los 500 m de recta y el objetivo de radio 25 m
del Python no se convierten en restricciones certificadas del generador C++.

## Configuración

La barra de menú contiene **Vistas**, **Archivo** (Abrir proyecto, Guardar proyecto,
Guardar proyecto como, Exportar ruta CSV, Exportar eje DXF y Salir) y
**Configuración**. La carga de topografía permanece en la sección
**Terreno** del panel izquierdo; las exportaciones están en **Archivo** y se
habilitan cuando hay una ruta vigente.
**Ctrl+O** abre un proyecto, **Ctrl+S** guarda en el JSON actual y
**Ctrl+Mayús+S** permite elegir otro archivo. En el primer guardado se solicita
el destino; se conserva la escritura atómica y la protección del terreno.

**Diseño** abre una ventana independiente con pendiente, radio y referencias
geométricas. **Configuración avanzada** abre una ventana flotante, redimensionable,
con cuatro encabezados en pestañas: **Diseño** (rasante, corte/relleno, ancho y
propuestas), **Precisión** (paso XY, muestreo, tolerancia y paso angular),
**Recursos** (adaptación y presupuestos CPU) y **Costos** (objetivo y tarifas).
El menú **Vistas** ofrece **ISO, Top, Bot, Front, Back, Left, Right, Acercar y
Alejar**. Estas acciones cambian únicamente la cámara y sustituyen la fila de
botones del visor; la navegación con ratón sigue disponible.
Cada pestaña tiene desplazamiento propio. Restablecer valores queda accesible
encima de las pestañas. Ambas ventanas permiten seguir consultando el visor;
cerrarlas conserva los cambios, que se aplican inmediatamente e invalidan la ruta.
Para conservarlos en disco se usa Guardar proyecto. Durante la carga o el cálculo,
las opciones de edición y los comandos de archivo quedan deshabilitados.


La interfaz comienza con `road_profile=routetec_monolith`,
`vehicle_profile=haul_truck_unspecified` y `cost_profile=length_only`.
Los defaults de la CLI anterior siguen siendo compatibles con sus fixtures.

La revisión actual incluye curvas continuas activas, radio inicial de 30 m y límites
de corte/relleno iniciales 2 m para la segunda propuesta. Son parámetros de
exploración; no constituyen aprobación vial. Véase RUTAS_Y_CORTES.md.
La recta mínima y el giro aislado se muestran como datos del modo anterior,
deshabilitados. Las espirales y curvas contiguas están siempre permitidas;
la rampa puede desarrollar varias vueltas sin violar el radio. Los proyectos
anteriores con ese modo desactivado requieren recalcular. Se comparan candidatos
por costo validado o longitud, incluidos los del refinamiento adaptativo.

**Restablecer valores**, al inicio de Configuración avanzada, recupera todos los
valores iniciales de ese panel: corte/relleno 2 m, margen adicional 0°,
paso angular 7.5°, ancho 8 m, paso XY 10 m, muestreo 2 m,
tolerancia 0.01 m, presupuestos iniciales, adaptación activada y objetivo
por longitud sin precios. Conserva terreno, A/B, pendiente máxima y propuesta
seleccionada; invalida ambas rutas para volver a calcular. También elimina los
valores avanzados recuperados del proyecto, incluidos costos y dominio Z manual.

Cada control numérico muestra su rango permitido y valor inicial. Estos rangos
son límites del programa, no normas de diseño vial. El muestreo admite
0.001–1000 m, pero su combinación con **Muestras** depende de la longitud del
recorrido y de los cruces de caras TIN. Si el presupuesto no alcanza ni para la
distancia directa A/B, el panel indica el mínimo necesario y bloquea CALCULAR
RUTA hasta corregir la combinación. El recorrido real puede requerir más
muestras. Los errores de muestreo se muestran en español e indican qué ajustar;
la aplicación no modifica silenciosamente la precisión solicitada. La combinación
de pendiente y ángulo adicional también se comprueba antes de calcular.

`length_only` usa peso de vía 1 y el resto de pesos 0: minimizar el costo aditivo
equivale a minimizar longitud horizontal para el ancho constante indicado.
No se presenta ese valor como dinero. Para comparar costos se elige **Costos
configurados (CU)** y se introducen explícitamente tarifas, fracciones y distancias.
No hay precios reales incluidos ni un vehículo con dimensiones aprobadas.

El desplegable principal elige el modo superficie/corte y relleno; el panel avanzado conecta límites independientes,
ancho, precisión XY/Z, muestreo, tolerancia, dominio Z opcional y presupuestos
de recursos al mismo resolvedor del núcleo. La reutilización de corte sigue en
cero por la restricción del objetivo aditivo. Los valores exactos recuperados de
un proyecto se conservan hasta su edición; el tooltip muestra el valor guardado.

Se importan también JSON `RouteTec 3D Professional / visor-threejs-1.0` con A/B
completos. Se recupera `max_grade`; los metadatos antiguos de radio/recta/algoritmo
no acreditan restricciones ni sustituyen el motor. Las métricas guardadas se
recalculan. Una ruta que no coincide con A/B o falla la validación se descarta.

## Terrenos y límites

Se admite DXF ASCII, sección ENTITIES, caras 3DFACE de espacio modelo; los
triángulos conservan sus coordenadas y conectividad. Los cuadriláteros deben ser
convexos y coplanares. MESH, POLYLINE, INSERT, SOLID/TRACE y otras superficies
no soportadas producen un error explícito: deben exportarse antes como 3DFACE.
No se incluye triangulación global, reparación de huecos ni importación DXF binaria.
Las anotaciones se omiten; las caras de bloques no se incorporan sin su transformación.

Las caras degeneradas en XY se omiten con conteo visible en el importador de
escritorio/CLI. El API sin informe conserva el rechazo estricto. Se mantiene
la ausencia de cobertura donde se omiten caras. Un índice BVH acelera las
consultas de cota y cruces sin cambiar las caras utilizadas para calcular.

La CLI XY/Z tiene presupuestos explícitos. El escritorio evita ese producto de
estados y adapta XY/recursos cuando está activada la búsqueda adaptativa; informa
la resolución efectiva. La carga y el renderizado de un terreno no acreditan que
se pueda calcular cualquier ruta: siguen existiendo límites de cobertura,
desarrollo, geometría y recursos. Véase RUTAS_Y_CORTES.md.

Radio de curva, recta mínima y ángulo máximo del eje están implementados en el
modo de tangentes y arcos muestreados desde el 29 de octubre de 2026. Véase
RUTAS_Y_CORTES.md para defaults, referencia y límites. Ancho barrido del vehículo,
peralte, transiciones, pendiente cero en curvas y autointersecciones siguen pendientes. No se declara carretera transitable ni
factibilidad integral. La búsqueda sigue siendo CPU; no se añadió planificación CUDA.

## Compilar el visor

Entorno Windows x64 comprobado: MSVC 19.44, CMake 3.31.6, Qt 6.10.2 y VTK 9.5.2
con soporte Qt 6. Se usaron paquetes conda-forge en una carpeta local; se incluyen
`environment-viewer.yml` y el listado exacto `docs/dependencias-visor-win64.lock`.
En un equipo nuevo, con micromamba disponible:

```powershell
micromamba create -p .\out\viewer-deps -f .\docs\dependencias-visor-win64.lock
cmake -S . -B out/viewer -DROUTEMINE_BUILD_VIEWER=ON -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$PWD/out/viewer-deps/Library"
cmake --build out/viewer --config Release -- /m:1 /nr:false
$env:PATH="$PWD/out/viewer-deps/Library/bin;$env:PATH"
ctest --test-dir out/viewer -C Release --output-on-failure
```

CMake no descarga dependencias. La restauración del entorno sí necesita red.
El lanzador local no constituye un paquete portable independiente; requiere
las DLL de ese entorno. Linux y CUDA no se probaron en esta adaptación.

## Evidencias de validación inicial de la interfaz

- Compilación Release de núcleo, CLI, pruebas y visor Qt/VTK.
- 40 pruebas CTest: 28 anteriores y 12 de DXF, índice, perfil, cancelación,
  exportaciones y proyectos.
- Prueba gráfica de selección 3D, registro manual, cálculo, invalidación al
  editar, exportaciones, reapertura del JSON y cancelación.
- CSV, DXF y JSON de la prueba gráfica contienen los mismos XYZ. Lectura externa
  con ezdxf 1.4.3: polilínea 3D, dos puntos A/B, cero errores y cero reparaciones
  en su auditoría.
- `Topografia_Campo.dxf`: 174560 caras, ninguna degenerada.
- `Surf_2027_FB.dxf`: 201218 caras originales; 200607 válidas y 611 degeneradas
  omitidas con aviso. Ambos archivos se cargaron y renderizaron en el visor.

Capturas y exportaciones locales: `out/viewer-smoke`, `out/capture-topografia`
y `out/capture-surf`. Estas pruebas no certifican rutas reales sobre esos terrenos.

La ampliación posterior de rutas y cortes pasó 52/52 pruebas y los flujos gráficos
de cálculo, exportación y reapertura, incluido un recorrido real de 4534.23 m
horizontales con pendiente máxima de 11.99886926 %. Las propuestas rojas se
contrastaron también con un lector DXF independiente. Véanse
[rutas y cortes](RUTAS_Y_CORTES.md) y [validación actualizada](VALIDACION.md).

Fuentes de implementación: [Qt Concurrent](https://doc.qt.io/qt-6/qtconcurrentrun.html),
[QSaveFile](https://doc.qt.io/qt-6/qsavefile.html) y
[integración Qt/VTK](https://examples.vtk.org/site/Cxx/Qt/MinimalQtVTKApp/).

### Límites habituales de diseño

La pendiente máxima se configura entre 0 y 12 %, el radio mínimo desde 30 m y la recta mínima al invertir el giro desde 35 m. Radio y recta no tienen máximo de uso (solo el límite de representación numérica). Los valores iniciales son 10 %, 30 m y 35 m, respectivamente. **Extender los límites** permite pendiente hasta 100 % y mínimos de radio/recta desde cero; la geometría activa sigue exigiendo radio positivo. Al abrir proyectos fuera del rango habitual se activa automáticamente para conservar sus valores. Desactivarlo ajusta los valores al rango habitual e invalida la ruta. Pendiente cero permite únicamente recorridos horizontales. La recta se exige entre giros de sentidos opuestos; no se exige entre arcos contiguos del mismo sentido. El giro máximo del modo anterior permanece inactivo.

## Ajustes progresivos y puntos intermedios (30-10-2026)

**BUSCAR CON AJUSTES PERMITIDOS** está disponible para ambas propuestas. Primero
refina XY/direcciones y amplía recursos, después reduce recta en niveles 60/45/35 m,
reduce radio en niveles 40/30 m y aumenta pendiente por niveles hasta 12 %.
Omite niveles que no amplían la configuración solicitada y realiza un refinamiento
final. Se detiene con la primera ruta validada de la secuencia. No cambia A/B,
ancho, cotas originales ni máximos de corte/relleno. Los controles conservan los
valores solicitados; Resultados muestra los utilizados y cada propuesta conserva
su configuración efectiva. Guardar proyecto utiliza esa configuración efectiva.

**VER DIAGNÓSTICO Y AJUSTES** explica cada intento y sus descartes observados:
pendiente, cobertura, profundidad, separación/cruces y geometría. Incluye accesos
a Diseño y Corte/relleno. Agotar recursos no demuestra que una ruta sea imposible.
Se explica expresamente la incompatibilidad de pendiente cero y cotas diferentes.
El control **Candidatos por variante** (inicial 64) reemplaza la antigua interrupción
interna después de ocho candidatos rechazados; no limita el número de vueltas.

Para optimización puntual:

1. Calcule o abra una ruta validada y use **MARCAR P1** y **MARCAR P2** en la sección
   **Puntos intermedios**; haga clic en la carretera, no en el terreno.
2. Abra **CONFIGURACIÓN PUNTOS INTERMEDIOS**, también disponible en el menú
   Configuración. Puede cambiar los puntos del recorrido por número y configurar
   pendiente 0–12 %, radio desde 30 m y recta al invertir el giro desde 35 m.
3. Pulse **OPTIMIZAR TRAMO P1 → P2**. Se prueban tres resoluciones con rumbos de
   entrada/salida fijos. Solo se aplica un candidato que mejore el objetivo completo
   y supere comprobaciones de continuidad, tangencias, radios, pendiente,
   cobertura, profundidad y separación. Si no mejora, se conserva la ruta anterior.
4. Repita en otros tramos si lo necesita. El JSON conserva cada intervalo y sus
   restricciones; las partes externas mantienen las suyas. Los cambios de parámetros
   globales siguen invalidando la ruta completa.

La comparación entre propuestas señala cuando sus parámetros difieren.
En la opción de corte/relleno, rojo significa eje bajo el terreno (corte), verde
eje sobre el terreno (relleno) y amarillo profundidad dentro de la tolerancia.
La clasificación se corta en cruces de caras y transiciones de signo/tolerancia.
DXF exporta capas ROUTETEC_CORTE (rojo), ROUTETEC_RELLENO (verde) y ROUTETEC_EJE
(amarillo); CSV identifica CORTE, RELLENO y SOBRE_TERRENO. Son profundidades del eje,
no la plataforma ni taludes completos.

El motor sigue siendo Hybrid A* con intentos desde ambos extremos por separado.
No se implementó una búsqueda bidireccional de dos frentes simultáneos. La TIN
original no se modifica en ninguna de las opciones.


### Visualización de la búsqueda en vivo

Al pulsar CALCULAR RUTA, el visor dibuja las ramas reales que explora el buscador. El progreso se muestra en la barra inferior para dejar visible el terreno; VER PROGRESO abre la ventana de detalles. También se observa la búsqueda al optimizar un tramo intermedio.

- Gris, opacidad 40 %: ramas exploradas sin una conexión completa validada. No implican que el camino sea geométricamente imposible.
- Azul, opacidad 30 %: alternativas completas verificadas durante los intentos de búsqueda y la ruta de referencia, cuando existe.
- Azul sólido: ruta elegida al terminar, la mejor entre los candidatos evaluados; la búsqueda acotada no garantiza un óptimo global.
- Rojo: tramos de corte; verde: tramos de relleno del eje elegido, clasificados frente a la TIN original.

La acción **Vistas > Mostrar caminos explorados** permite ocultar o volver a mostrar las alternativas. Las ramas permanecen visibles al terminar o cancelar y se limpian al recalcular, cambiar de opción o modificar el diseño. El historial visual es temporal y no se guarda en el proyecto ni se exporta como una carretera.

La vista se actualiza cada 100 ms. Las 3000 ramas recientes alimentan una huella espacial acumulada que conserva las zonas recorridas en intentos anteriores, incluso al cambiar de extremo, finalizar o cancelar. Se agrupan segmentos en celdas XY de 2 m; si se superan 65536 celdas, se duplica la celda y se conserva un segmento representativo por celda. Esta huella es una representación de exploración, no una superficie completamente comprobada ni una ruta válida. También se conservan 16 alternativas completas. Solo la representación se simplifica; el cálculo, la validación y las exportaciones mantienen su precisión original. La huella se limpia al comenzar otro cálculo, cambiar los datos/opción o limpiar el proyecto. Una ruta directa puede resolverse sin generar ramas distintas.

El botón CALCULAR RUTA obtiene primero un corredor sobre la TIN con la pendiente solicitada. Si hace falta, ensaya pendientes superiores hasta 12 %. No exige curvas tangentes ni radio mínimo para mostrar ese corredor; la interfaz y el proyecto lo identifican como **curvas pendientes**. Pendiente, cobertura y separación respecto a la superficie se comprueban. Los valores efectivos se muestran y guardan con el resultado. Los reintentos comparten la huella gris.


### Diagnóstico de restricciones incumplidas

Si una ruta candidata falla en la validación final, se abre una ventana en español,
con desplazamiento y botón **Copiar diagnóstico**. Para cada restricción se muestra
la primera ubicación detectada: vértices numerados desde 1, distancia horizontal
acumulada desde A y coordenadas XYZ de los extremos del tramo. Pendiente, separación
de la superficie, corte y relleno muestran el valor observado y el límite exigido.
También se identifican cobertura TIN ausente o ambigua, geometría de curvas,
tangencia y separación o cruces entre vueltas. Si los índices de una curva son
inválidos, se indica que su ubicación no está disponible.

El diagnóstico corresponde al candidato rechazado; no demuestra que sea imposible
obtener otra ruta. Se mantienen los límites y tolerancias de validación.


### Búsqueda unificada de carreteras

La interfaz usa **BUSCAR RUTAS POSIBLES**. Ya no se elige entre respetar el terreno
y permitir intervenciones: una sola búsqueda incluye alternativas sobre la superficie,
con corte y con relleno. El corredor auxiliar sirve para orientar la búsqueda, pero
no se dibuja como carretera. No se muestran rayos radiales ni desvíos con quiebres:
los candidatos se forman con rectas y arcos tangentes, crecimiento por curvas de
contorno y familias de espirales elípticas/logarítmicas. Se admiten rodeos y vueltas
largos; una alternativa no se descarta únicamente por ser más larga que otra.

La exploración comienza directamente desde A, B y otros puntos de la TIN, sin
esperar un corredor completo. Los recorridos con cobertura continua y pendiente
del terreno de hasta **2× la máxima solicitada** se dibujan progresivamente en
como exploración, aunque aún fallen radio, recta, ancho o límites de intervención.
La exploración no certifica una carretera. Su huella permanece al explorar otros sectores y al
detener la búsqueda; **Vistas > Mostrar exploración** permite ocultarla o mostrarla.
La huella usa un resumen espacial acotado para mantener ágil la vista y no se
guarda como geometría de carretera en el proyecto.

Cada carretera conserva la pendiente máxima del eje, radio, rectas de avance,
separación entre sus vueltas y profundidades máximas de corte/relleno configuradas.
Además, la pendiente longitudinal del terreno debajo del eje no puede superar el
doble de la pendiente de diseño. Los excesos entre el máximo normal y el doble,
y los tramos continuos que necesitan corte/relleno, no pueden superar **100 m**
por defecto. Este valor se cambia en **Configuración avanzada > Diseño > Longitud
continua máxima de intervención (m)**. No se eleva la pendiente del eje para admitir
un candidato que requiera intervención.

- **Gris:** recorrido explorado cuya pendiente no supera la máxima normal,
  con otras comprobaciones todavía pendientes.
- **Rojo:** recorrido explorado con exceso de pendiente ascendente, hasta 2×
  la máxima normal, en el sentido de crecimiento del trazo.
- **Verde:** recorrido explorado con exceso de pendiente descendente, hasta 2×
  la máxima normal, en el sentido de crecimiento del trazo.
- **Celeste:** alternativa que supera las comprobaciones implementadas del eje:
  pendiente, cobertura, radio, tangencias, rectas y separación, junto con los
  límites de corte/relleno e intervención. Puede requerir movimiento de tierras.

Rojo y verde representan el signo del exceso de pendiente en esta búsqueda
unificada. El corte y relleno de las alternativas verificadas se conservan como
datos y en las exportaciones. La longitud de los arcos no cuenta como recta de
avance; los giros se construyen con arcos tangentes y radio mínimo configurado.
Cada bloque recto de avance se comprueba completo, sin imponer la longitud mínima
a cada pequeña arista de muestreo. Las curvas contiguas del mismo sentido pueden
enlazarse sin recta intermedia; al invertir el sentido de giro se conserva la
recta mínima configurada. El radio es una restricción del eje y debe elegirse para
el vehículo de proyecto; no se calcula todavía su trayectoria barrida completa.

Se dibujan también ramas viales verificadas que todavía no llegan a B. Cada rama
conserva su geometría propia; nunca se añade una línea artificial para unirla con
otra. Las distintas alternativas son mutuamente excluyentes y la elección final
corresponde al usuario; no se implementa todavía un flujo de selección de carretera.
La vista y el proyecto conservan hasta 256 ramas distintas y 128 alternativas
ancladas, dentro del presupuesto de vértices del proyecto.
La huella espacial celeste de la sesión sigue incorporando segmentos de nuevos
recorridos verificados después de alcanzar ese límite de geometrías guardables.
Conserva las regiones anteriores mediante un resumen espacial acotado; no une
segmentos de rutas distintas. Este historial de visualización es transitorio,
igual que la huella gris, y no se exporta como una carretera conectada.

La búsqueda sigue hasta **DETENER BÚSQUEDA**. La barra muestra tiempo y ronda actual,
sin porcentaje o tiempo restante ficticios. **Guardar** conserva las geometrías y
el límite de intervención; al abrir se validan nuevamente frente a la TIN antes de
colorearlas. Las exportaciones separan cada alternativa por identificador/polilínea.
La búsqueda no garantiza enumerar todas las soluciones ni encontrar el óptimo global.
La comprobación es preliminar del eje: plataforma, vehículo completo, estabilidad,
peralte y transiciones requieren diseño posterior.

Además de las espirales y recorridos de contorno, se ensayan serpentines con rectas
y horquillas tangentes. Se buscan conexiones completas A–B y se conserva la mejor
encontrada según el objetivo configurado (por defecto, longitud), sin detener la
exploración al encontrar la primera. Una conexión posterior sólo la reemplaza si
mejora el objetivo y supera nuevamente la validación del recorrido completo.
«Mejor conexión encontrada» describe el resultado disponible de la búsqueda;
no es un certificado de óptimo global. Los huecos de la TIN y los límites de diseño
pueden impedir una conexión aunque se permitan rodeos largos.

### Criterios de diseño y manuales

**Configuración > Diseño** incorpora velocidad de diseño (20 km/h inicialmente),
orografía, peralte de referencia, riesgo de hielo y altitud (0 usa la cota máxima
de la TIN). La orografía es una selección del proyectista, no una clasificación
automática por la pendiente longitudinal. El peralte es una entrada para calcular
el radio; todavía no se construyen ni validan su superficie y sus transiciones.

El perfil inicial **Exploración preliminar** utiliza la fórmula DG-2018
`R = V²/[127(e/100+f)]` y los mínimos S/O de tangente. A 20 km/h, con peralte 6%
y f=0,17, el radio calculado es 13,69 m y las tangentes calculadas son 27,8/55,6 m.
Se conserva el radio operativo inicial de 30 m y la recta mínima de avance de
35 m; el mínimo O se aplica a rectas entre curvas del mismo sentido. Las curvas
compuestas contiguas no se convierten artificialmente en rectas.

**20 km/h no es una fila de tercera clase de la Tabla 204.01.** Por ello no se
asignan a ese perfil una pendiente ni una calzada pretendidamente normativas.
El modo **DG2018 tercera clase** exige una combinación tabulada: en escarpado,
30 km/h; en accidentado, 30 o 40 km/h. Por ejemplo, a 30 km/h y más de 3000 m,
el máximo tabulado para estos terrenos baja de 10% a 9%, con tangentes S/O de
42/84 m. Con peralte 6% la fórmula da R≥30,81 m; se conserva el valor calculado,
sin redondearlo hacia abajo. El ancho se comprueba frente a la calzada base de
la tabla, conservando cualquier ancho mayor solicitado; faltan sobreancho y bermas.

Al adoptar estos criterios se aplica una comprobación conservadora de pendiente
≤8% en curvas R<50 m y se rechazan recorridos con pendiente >10% continua por
más de 180 m. El diagnóstico identifica el tramo. Los descansos y pendientes
medias, visibilidad, curvas verticales, envolvente del vehículo, sobreancho,
clotoides, drenaje y estabilidad siguen pendientes. El celeste representa las
comprobaciones implementadas del eje, no conformidad integral con DG-2018.

Fuentes: DG-2018, tablas 204.01, 302.01–302.04, 303.01, 304.01 y 304.05
(páginas impresas 97, 127–132, 170–172, 191 y 196). El manual de bajo volumen
de marzo de 2008 se revisó como referencia histórica; su resolución aprobatoria
fue declarada derogada por la [RM 137-2014-MTC/02](https://www.gob.pe/institucion/mtc/normas-legales/293549-137-2014-mtc-02).
La base principal es el [manual DG-2018 publicado por el MTC](https://portal.mtc.gob.pe/transportes/caminos/normas_carreteras/manuales.html).
