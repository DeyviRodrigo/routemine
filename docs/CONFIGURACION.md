# Configuración básica y avanzada

Implementado en `config.hpp` / `config.cpp`, compartible por núcleo, CLI y futura
interfaz. No se ha creado GUI. La configuración de planificación se resuelve en
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
  `sample_spacing_m`, `max_samples`, `max_states`, `max_expansions`, `max_edges`.

Radio, rectas mínimas y geometría vehicular NO son ajustes operativos todavía.
Si se intenta introducir `min_radius_m`, `min_radius`, `min_straight_m`,
`min_straight` o `zero_grade_on_curves`, se devuelve error explícito de función
no implementada. Las advertencias se muestran incluso si no se introducen esas
claves; no se inventan valores aprobados en el perfil.

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

## GUI futura, sin implementar

Pantalla básica: terreno, A/B y selectores de perfiles verificados cuando
existan. Panel avanzado plegable: grupos anteriores, mostrando valores efectivos
y su origen. Deberá usar este mismo resolvedor y persistencia; radio/geometría
permanecerán deshabilitados con explicación hasta implementarlos. El contexto
minero de volquetes está confirmado; faltan modelo/dimensiones y restricciones
validadas antes de añadir un perfil real. No se asume un volquete específico.
