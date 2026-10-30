# Parámetros del flujo Python activo

Este análisis corresponde a la versión anterior indicada abajo. Para la copia
local `D:\Downloads\RouteTec\RouteTec_3D.py` revisada el 23/10/2026 y su interfaz
adaptada, consultar INTERFAZ_ROUTETEC.md.

Fuente leída: Library `RouteTec_3D_01.py`, 319495 bytes / 10259 líneas;
`libfile_94256cef52888191ad55a5d92d55021d`.
Hash comunicado por el coordinador, no recalculado localmente:
`13d3090d94452ea65c4dbd4ea05ab27a71c8a03ce81723c52534940c303c982a`.
La copia original no fue materializada ni ejecutada en este equipo.

## Firma y cadena invocada

Líneas 9052–9055:

```python
def calculate_route(
    self, max_grade_percent: float, min_radius=30.0, road_width=8.0,
    design_mode='earthwork', max_cut=20.0, max_fill=20.0, min_straight=500.0,
):
```

La UI llama `start_operational_route(grade)` (10053), que llama
`start_route_calculation(grade, 0.0, 8.0, "earthwork", 1e6, 1e6, 500.0)`
(7528–7545). El iniciador convierte los argumentos y limita `min_straight`
a por lo menos 200 m (7464–7526). El worker llama `calculate_route(*args)`
(7547–7592). La conversión/transmisión no implica uso en el cálculo.

| Parámetro | Uso en `calculate_route` activo | Sustitución o ausencia | Uso en otros métodos |
|---|---|---|---|
| `max_grade_percent` | Sí: convierte a `grade`, exige `0 < grade <= 100`, lo pasa a búsquedas y compara la pendiente calculada | Comparación final `actual <= grade + 1e-4`; no garantiza una solución válida, porque hay fallbacks ilustrativos | La UI operacional exige además `0.1 <= grade <= 100` |
| `min_radius` | No se lee dentro del cuerpo | Una rama llama `_make_vehicle_drivable(... radius_m=25.0)`; varias métricas/advertencias usan 25 m. Esa rama puede omitirse; no es garantía global de radio | `_hybrid_lattice` (3824–3834), `_hybrid_astar_route` (4013 y siguientes) y `_plan_switchback_route` (7172 y siguientes) tienen lógica de radio |
| `road_width` | No se lee | No hay sustitución que compruebe una sección/corredor en el cuerpo activo. El 8 m transmitido por UI no valida ancho | `_hybrid_edge_profile` (3863–3990) comprueba clearance frente a medio ancho; `_optimize_cut_fill_profile` (5067 y siguientes) modela volúmenes preliminares |
| `design_mode` | No se lee | La cadena activa sigue su secuencia fija. `metrics['design_mode']` es una etiqueta de resultado; no aplica el argumento recibido | `_hybrid_edge_profile` distingue `surface` de `earthwork`; `_plan_switchback_route` recibe el modo |
| `max_cut` | No se lee | Varias búsquedas y la verificación usan banda simétrica fija `5.0` / `5.0001`, no el límite de corte solicitado | `_optimize_cut_fill_profile` limita cotas por `terrain_z - max_cut` (5464–5520) |
| `max_fill` | No se lee | Misma banda simétrica fija; el 1e6 transmitido por UI no controla el resultado | `_optimize_cut_fill_profile` limita por `terrain_z + max_fill` (5464–5520) |
| `min_straight` | No se lee | Primer candidato `_long_leg_hairpin(... min_leg=500.0)`. No implica que todos los fallbacks cumplan 500 m | `_operational_switchback_route` (6477–6508) consume el argumento y lo limita a mínimo 200 m; `_plan_switchback_route` lo recibe |

La secuencia inicial activa es `_long_leg_hairpin`,
`_search_constrained_terrain_route`, `_tin_grade_band_search` y finalmente
`_visible_topographic_fallback`. Después hay limpieza y suavizado condicionales.
El cuerpo 9052–9158 no llama `_plan_switchback_route`,
`_operational_switchback_route`, `_hybrid_astar_route` ni
`_optimize_cut_fill_profile` para aplicar los argumentos avanzados.

## Defectos que no se trasladan como contrato correcto

- `ok: True` también puede representar una propuesta no validada. La UI debe
  distinguir resultado disponible de restricciones verificadas.
- La marca `min_radius_target_m` describe un objetivo y no acredita curvatura.
- La banda ±5 m no equivale a dos límites configurables de corte y relleno.
- La Delaunay XY de navegación (1148–1220) no necesariamente conserva las caras,
  huecos ni múltiples cotas de la TIN original.
- En C++ radio y rectas fueron inicialmente parámetros pendientes. Desde el
  29-10-2026 se aplican en el modo de tangentes y arcos muestreados, junto con
  el giro máximo; véase RUTAS_Y_CORTES.md. El ancho vehicular sigue pendiente.
  En el modo anterior se aplican pendiente,
  modo superficie y límites independientes de corte/relleno; el segundo bloque
  comprueba además todos los cruces con las caras originales de la TIN.
