# Segundo bloque: generar corredores y rasantes en CPU

## Funcionalidad

`load_obj` lee vértices XYZ y caras triangulares originales. Acepta índices de
vértice positivos y negativos referidos a vértices anteriores; etiquetas o/g/s
se ignoran. Rechaza polígonos no triangulados, índices fuera de rango, caras
degeneradas/verticales en XY, coordenadas no finitas, UV/normales y otras
directivas no soportadas. Este es un subconjunto OBJ documentado: no pretende
leer todo OBJ. El adaptador separado `load_dxf` incorpora 3DFACE ASCII; véase
INTERFAZ_ROUTETEC.md. Nunca triangula globalmente ni rellena huecos.

`Terrain::segment_profile` corta el segmento XY contra los triángulos mediante
pesos baricéntricos. Reúne los límites de intervalos y comprueba sus extremos y
puntos medios para detectar huecos/cotas superpuestas. Elevación del terreno y
perfil proyectado son lineales por intervalo: se comprueban sus extremos para
límites de corte/relleno y se integra separando cambios de signo. Se usan
tolerancias numéricas: pertenencia baricéntrica 1e-12; discrepancia de cotas
superpuestas mayor que 1e-6 m se rechaza. No es geometría exacta simbólica.

`plan_cpu` crea ejes X/Y en los bounds del terreno con espaciado máximo solicitado
e inserta las coordenadas exactas A/B; crea niveles Z configurados e inserta sus
cotas exactas. Conecta ocho vecinos XY con niveles Z cuya pendiente del perfil
no excede el límite. No hay movimientos verticales en el mismo XY. Cada arista
debe permanecer sobre caras válidas, sin ambigüedades, y respetar corte/relleno
o separación de superficie según modo. Un terreno empinado no bloquea por sí
solo una rasante final factible en modo movimiento de tierras.

Dijkstra usa costos no negativos configurables: superficie de vía, suelo/roca,
relleno, compra, disposición y acarreo. La búsqueda exige fracción reutilizable
cero para conservar aditividad del costo. No simula balance global de materiales.
El evaluador independiente sí admite reutilización para rutas ya conocidas.

La salida distingue encontrado, sin camino en esta malla y presupuesto agotado.
El menor costo se refiere exclusivamente al grafo construido y al modelo
preliminar, sin optimalidad global sobre el terreno continuo. No se flexibilizan
restricciones geométricas para comprar una solución barata.

## CLI reproducible

```sh
build/Release/routemine_cli.exe --plan fixtures/ridge.obj fixtures/ridge.request build/route.csv
```

En Linux usar `build/routemine_cli`. El archivo de solicitud contiene key=value;
comentarios empiezan por #. Se rechazan claves duplicadas, desconocidas o valores
no finitos. Deben declararse endpoints, pendiente/corte/relleno/ancho, modo,
resolución XY/Z, separación de muestras y todas las tarifas/fracciones/distancias
incluidas en el fixture. No hay tarifas reales predeterminadas. `earthwork_mode=1`
permite modificar terreno; `0` exige seguir su superficie. Las muestras uniformes
adicionales son redundantes para comprobar el perfil por caras, pero mantienen
el contrato existente y su límite de recursos.

`max_states`, `max_expansions`, `max_edges` son opcionales (200000, 100000,
1000000 respectivamente). El dominio Z es explícito; si excluye cotas necesarias,
puede no haber camino aunque exista uno fuera del dominio. Los endpoints deben
estar dentro de los bounds XY y Z. No se ajustan silenciosamente a otra posición.

La CLI sobrescribe únicamente el archivo de salida solicitado y rechaza que
sea uno de los archivos de entrada. El CSV contiene XYZ absolutos en doble
precisión. El informe conserva restricciones pendientes: radio, rectas, ancho
libre, pendiente en curvas e intersecciones. Salida 0 NO significa diseño vial
integralmente validado. No se implementó sección transversal ni normativa vial.

Actualización 29-10-2026: al indicar `min_radius_m` positivo se usa el nuevo
buscador de tangentes y arcos también en CLI. `min_straight_m` y
`max_turn_degrees` se comprueban sobre la alineación completa. Véase
RUTAS_Y_CORTES.md; el texto anterior de radio/rectas pendientes corresponde al
modo Dijkstra sin geometría, conservado para proyectos anteriores.

## Pruebas y observaciones

- OBJ válido/índices negativos, inválidos/triángulos degenerados.
- Discontinuidad de 1 mm detectada aunque el espaciado uniforme sea de 100 m.
- Generación de rasante con cotas variables, límite de pendiente e imposibilidad.
- Límites de corte, recursos y rechazo de reutilización no aditiva.
- Coordenadas UTM grandes con resultado económico equivalente al origen local.
- CLI real desde fixture OBJ y solicitud, con CSV generado.
- Cambio de tarifa: con excavación ficticia 1 CU/m³ genera 100 m y 150 m³ de
  corte, costo 1150 CU. Con 20 CU/m³ genera un rodeo de unos 136.569 m y 2.652 m³
  de corte, costo aproximado 1418.72 CU. El rodeo generado puede mejorar al
  candidato manual de 160 m; sigue siendo una polilínea sin curvas vehiculares.

## Complejidad y límites

Con V=Nx*Ny*Nz estados y K niveles de cota factibles por vecino, E<=8*V*K.
Dijkstra cuesta O(E log V), además del costo geométrico de cada arista. El
BVH actual reduce las caras candidatas por envolvente XY para las consultas de
cota y segmento. En casos con muchas caras superpuestas la complejidad puede
seguir acercándose al recorrido completo y a consultas cuadráticas por arista.
No se garantiza rendimiento sobre millones de triángulos. Memoria O(V+E+F) en
peor caso incluyendo cola e índice; los presupuestos limitan estados,
expansiones y aristas. Sigue pendiente medir búsquedas grandes y cachear perfiles.

DXF y GUI Qt/VTK ya están implementados y comprobados según INTERFAZ_ROUTETEC.md.
Continúan pendientes CUDA probado, zonas de exclusión, factibilidad geotécnica,
precios reales y datos de intervención. El entorno gráfico local añade dependencias
aisladas en `out/viewer-deps`.
