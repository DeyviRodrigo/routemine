# Comparar modificar el terreno frente a bordear

Requisito: una pendiente de terreno superior al límite no descarta por sí sola
un corredor si un perfil proyectado factible permite excavar/rellenar y reduce
el costo total. El límite duro se aplica a la pendiente del perfil final.

`estimate_cost` evalúa candidatos suministrados o producidos por `plan_cpu`.
Primero comprueba pendiente
del perfil, terreno en cruces de caras y límites independientes de corte/relleno. En modo
superficie también exige separación menor que la tolerancia; en modo movimiento
de tierras no impone la pendiente del terreno original como límite.

## Implementado

Volumen preliminar por integración por caras de profundidad de eje por ancho
constante. Se separan cruces de signo corte/relleno. Precio de excavación ponderado
por fracción roca/suelo. Reutilización configurable: mínimo entre relleno y corte
reutilizable; importación y excedente se obtienen por balance con factor 1:1.
El costo suma superficie de vía, excavación, colocación de relleno, compra de
material, disposición y acarreo. El acarreo distingue material reutilizado,
importado y excedente con distancias medias suministradas. Todas las tarifas son
entradas en una misma moneda elegida por el usuario; no hay precios reales
predeterminados. Un cero significa costo omitido explícitamente en el modelo.

Radio, rectas, ancho libre, pendiente en curvas e intersecciones siguen pendientes
en el informe. El ancho SÍ afecta cantidades/precio; eso NO verifica que la sección
quepa. Un candidato con costos bajos no se declara integralmente factible.

## Caso sintético ejecutable

`routemine_cli --cost-demo` y CTest `economics`:

- Mismos extremos A=(0,0,0), B=(100,0,0).
- Cresta triangular del terreno: cota 3 m en x=50 sobre y=0, cota cero en extremos.
  Pendiente superficial de las laderas: 6 %, superior al límite proyectado de 2 %.
- Candidato directo: 100 m, rasante horizontal; corte 150 m³ para ancho ficticio
  de 1 m. Corte permitido 5 m, relleno permitido 0 m.
- Candidato de rodeo: 160 m por y=30; rasante horizontal, sin corte.
- Vía ficticia: 10 unidades monetarias/m²; demás costos cero para aislar el efecto.

| Excavación ficticia | Directo | Rodeo | Menor costo preliminar |
|---|---:|---:|---|
| 1 unidad/m³ | 1150 | 1600 | Excavar |
| 10 unidades/m³ | 2500 | 1600 | Bordear |

Las esquinas del rodeo y el ancho ficticio son para la prueba económica. No
constituyen geometría vehicular verificada. Las pruebas también rechazan el
directo si el corte máximo baja a 2 m o se exige modo superficie.

## Generación incorporada y ampliaciones pendientes

`plan_cpu` ya genera corredores y cotas discretas con restricciones del perfil
final y costo aditivo. En esta búsqueda se exige `reusable_cut_fraction=0`:
reutilizar material requiere un estado de balance o una optimización global
posterior. El evaluador de candidatos conserva su soporte de reutilización.
La búsqueda no la ignora ni sustituye en silencio. Véase GENERADOR_CPU.md.

Generar alternativas de eje y rasante conjuntamente, aplicar restricciones duras
y ordenar candidatos por costo monetario total y métricas adicionales reportadas
por separado. Restricciones de geometría no se compran mediante una penalización.
Mantener alternativas no dominadas para sensibilidad a costos y materiales;
no prometer óptimo global con búsqueda discreta/heurística.

Faltan secciones transversales reales, taludes, factores de esponjamiento y
compactación, materiales por zona, roca excavable, muros/drenajes, acarreos con
rutas reales y restricciones de zonas excluidas. Faltan factibilidad geotécnica y
datos sobre permisibilidad de intervenir; no se infieren del costo. Estos datos
deben ser contratos explícitos y condiciones de factibilidad en las fases
siguientes. No se estimó un presupuesto de construcción real.
