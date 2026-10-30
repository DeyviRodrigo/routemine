# Configuración básica y avanzada

Implementado en `config.hpp` / `config.cpp`, compartido por núcleo, CLI e
interfaz Qt/VTK. La configuración de planificación se resuelve en
un único lugar; los defaults antiguos de estructuras del núcleo siguen por
compatibilidad de API, pero la CLI usa siempre el resolvedor central.

## Básico: terreno y dos puntos

```powershell
.\build\Release\routemine_cli.exe --plan-basic fixtures\ridge.obj fixtures\basic.request build\basic.csv
```

Solo son obligatorias las seis coordenadas XYZ de origen/destino en el archivo;
el terreno es el argumento OBJ. No se cambia automáticamente la cota ingresada.
El archivo `basic.request` muestra este mínimo. Los perfiles por defecto son
**SYNTHETIC examples**, no configuraciones aprobadas de carretera ni de volquete.
La aplicación muestra esta advertencia y las restricciones no implementadas.

El enfoque confirmado es vías mineras de volquetes. El perfil inicial se llama
`mining_haul_provisional`: identifica ese contexto, pero usa únicamente valores
sintéticos de demostración. No define modelo, dimensiones ni capacidad de giro
de un volquete real ni valida una vía minera. La selección se separa en:

- `road_profile`: `mining_haul_provisional` por defecto, `synthetic_example` o `custom`.
- `cost_profile`: `synthetic_example` o `custom`.
- `vehicle_profile`: `haul_truck_unspecified` por defecto o `unspecified`; ambos
  son marcadores sin dimensiones. No hay perfiles geométricos de vehículo
  implementados. Cualquier modelo supuesto se rechaza.

La interfaz de escritorio utiliza además `road_profile=routetec_monolith`:
pendiente 10 %, radio 50 m, corte/relleno 30 m, curvas continuas y ancho 8 m
como configuración inicial de exploración. Los proyectos anteriores conservan
sus valores explícitos; el perfil sin overrides mantiene compatibilidad anterior.
Su objetivo inicial `cost_profile=length_only` fija vía=1 y los demás pesos=0;
no representa dinero. Un precio distinto requiere `custom`. Este perfil no
cambia los defaults de la CLI anterior. Véase INTERFAZ_ROUTETEC.md.

Ejemplo sintético heredado de los fixtures, NO valores normativos: pendiente 2 %,
corte 5 m, relleno 0 m, ancho de cálculo 1 m; vía 10 CU/m² y excavación suelo
1 CU/m³; demás tarifas/fracciones/distancias cero. Cero significa costo omitido
en este ejemplo. Precisión inicial: XY 10 m, Z 1 m, muestras adicionales 2 m,
tolerancia de superficie 0.01 m; límites de estados/expansiones/aristas de la fase
anterior. El dominio Z se deriva de bounds del terreno/endpoints y corte/relleno;
puede cambiarse explícitamente. No se ajusta la precisión en silencio si excede
recursos: se informa el error o presupuesto agotado.

## Avanzado: cambiar solo lo necesario

Añada al mismo archivo claves soportadas. Ejemplo:

```text
# Se suman a las seis coordenadas básicas.
max_grade_percent=7.5
max_cut_m=3
soil_cut_per_m3=20
xy_spacing_m=5
```

Precedencia: **override explícito > perfil elegido > defaults centrales**. Cambiar
una clave no borra las demás. Con `road_profile=custom` deben declararse pendiente,
corte, relleno, ancho y modo; con `cost_profile=custom` deben declararse las trece
tarifas/fracciones/distancias del fixture completo. Así un perfil personalizado
no incorpora tarifas sintéticas que el usuario olvidó definir.

Grupos avanzados implementados:

- Restricciones: `max_grade_percent`, `max_cut_m`, `max_fill_m`, `road_width_m`,
  `earthwork_mode`, `surface_tolerance_m`. Ancho afecta cantidades/costos; no
  verifica clearance ni sección vehicular.
- Costos: `road_per_m2`, `soil_cut_per_m3`, `rock_cut_per_m3`,
  `fill_placement_per_m3`, `imported_material_per_m3`, `disposal_per_m3`,
  `haul_per_m3_km`, `rock_fraction`, `reusable_cut_fraction`, `internal_haul_km`,
  `import_haul_km`, `disposal_haul_km`. La búsqueda exige reutilización cero;
  cualquier otro valor se rechaza, aunque el evaluador independiente sí la admite.
- Precisión/recursos: `xy_spacing_m`, `z_min`, `z_max`, `z_step_m`,
  `sample_spacing_m`, `max_samples`, `max_states`, `max_expansions`, `max_edges`,
  `max_ramp_candidates` (64 candidatos reconstruidos por variante inicialmente).

`min_radius_m`, `min_straight_m` y `max_turn_degrees` activan el buscador de
tangentes y arcos circulares muestreados, también desde la CLI. Se requiere
radio positivo; el ángulo debe estar en (0, 180]. Los nuevos proyectos del visor
comienzan con 50 m, 84 m y 120°, respectivamente. Su referencia y limitaciones
se explican en RUTAS_Y_CORTES.md. Los proyectos antiguos sin estas claves conservan
radio/recta 0 y giro 180°: búsqueda anterior sin restricciones de alineación.
Los alias `min_radius`, `min_straight` y `zero_grade_on_curves` se rechazan.

`continuous_curves=1` activa la rampa Hybrid A* guiada por el relieve, con cota
proyectada, arcos enlazados y vueltas completas. En este modo se aplica la
recta mínima al invertir el sentido del giro; entre arcos del mismo sentido no
se exige recta y no hay límite al giro acumulado. Se mantienen radio, pendiente,
corte/relleno y separación entre vueltas. `earthwork_mode=1` permite construir
la rasante dentro de las profundidades elegidas; `earthwork_mode=0` exige que
siga la TIN dentro de la tolerancia. Los defaults nuevos del escritorio incluyen
`continuous_curves=1`; el visor lo mantiene activo incluso al abrir proyectos
anteriores. En ese caso descarta la ruta antigua y solicita recalcular; si no
había radio positivo utiliza 50 m. La CLI conserva el modo anterior cuando falta
esa clave. El resolvedor no aumenta silenciosamente profundidades explícitas.

## Mostrar, guardar y recuperar

```powershell
.\build\Release\routemine_cli.exe --show-config fixtures\ridge.obj fixtures\basic.request
```

Muestra la configuración efectiva sin buscar ni guardar archivos. Al planificar,
la CLI también la muestra y guarda `salida.csv.project.conf` junto al CSV. El
snapshot incluye versión de esquema 1, perfiles, terreno, endpoints y todos los
valores efectivos con precisión de doble; se guarda incluso si no se encuentra
camino. No es todavía el formato completo de proyecto con malla/ruta embebidas.

Puede recargarse como solicitud en `--plan-basic`, eligiendo otra salida. El
terreno pasado explícitamente a CLI prevalece sobre el path guardado; se advierte
si difieren. Las cotas/límites efectivos guardados se mantienen como overrides,
sin recalcular defaults silenciosamente. Rutas entre comillas admiten espacios,
barras invertidas y #. Se rechazan claves desconocidas/duplicadas, valores
no finitos, perfiles inexistentes y versiones de esquema no soportadas.

La CLI anterior `--plan terreno.obj solicitud-completa salida.csv` sigue
funcionando, exige sus campos previos y también muestra/guarda configuración.
No se sobrescriben los archivos de entrada: si CSV o snapshot apuntan a ellos,
la operación falla y solicita otra ruta de salida.

## Interfaz de escritorio

La búsqueda de escritorio dispone ahora de dos variantes, descritas en
RUTAS_Y_CORTES.md. `adaptive_resolution`, `propose_earthwork`, `detour_ratio`,
`heading_step_degrees`, `max_shortcut_checks` y `additional_grade_degrees`
se guardan con el proyecto. El margen angular adicional inicial es 0° y el
umbral de rodeo es estrictamente mayor que 5 veces la conexión propuesta.
La primera variante sigue la TIN; corte/relleno se evalúan en la segunda.

`Abrir-RouteMine.bat` abre el visor Qt 6/VTK. El flujo básico contiene terreno,
registro manual/selección 3D de A/B y pendiente. El panel avanzado plegable
conecta las restricciones, precisión, recursos y costos soportados al mismo
resolvedor. El guardado JSON incluye la configuración efectiva y el eje; al
reabrir se comprueba de nuevo la ruta, incluidos sus arcos y empalmes. El grupo
Diseño muestra radio mínimo, recta mínima y giro máximo. Restablecer valores
también recupera estos tres valores iniciales. No existe un vehículo con dimensiones aprobadas.

### Configuración local en proyectos JSON

`routing.local_sections` conserva intervalos inclusivos `first`/`last` de la ruta
con `max_grade_percent`, `min_radius_m` y `min_straight_m` locales. No se solapan
(salvo el vértice común de intervalos contiguos). La configuración base rige en los
otros intervalos; se validan por separado y se comprueba el recorrido completo.
Un giro inverso en una frontera conserva la recta más exigente de los intervalos
implicados. `routing.adjustment_attempts` y `adjustment_report` conservan el historial
de la búsqueda con ajustes. Los proyectos sin estas claves siguen siendo válidos.
Los rumbos de empalme usados por el optimizador son temporales; la geometría y las
restricciones guardadas permiten revalidar independientemente el resultado.
