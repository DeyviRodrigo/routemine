# Ruta por pendiente y propuesta de corte/nivelación

## Revisión vigente: ajustes e intervalos locales (30-10-2026)

La recta mínima ahora se aplica al **invertir el sentido del giro**. Las curvas
contiguas del mismo sentido y las vueltas completas siguen permitidas. Los valores
habituales son pendiente 0–12 %, radio mínimo ≥30 m y recta ≥35 m, sin máximos de
uso para radio/recta. Los apartados anteriores que describen la recta como inactiva
corresponden a revisiones históricas.

El botón de ajustes prueba recursos/resolución, recta, radio y pendiente, en orden,
sin aumentar corte/relleno. P1/P2 se marcan sobre una ruta ya calculada y tienen
configuración local propia. Se prueban conexiones tangentes de círculo–recta–círculo
con ambos rumbos fijados; solo se acepta una mejora validada del recorrido completo,
conservando las restricciones externas. Véase INTERFAZ_ROUTETEC.md para el flujo.
Los tramos del eje con corte son rojos, con relleno verdes y sin movimiento amarillos.


## Dos propuestas con curvas continuas: 30-10-2026

El visor ofrece **1. Propuesta respetando el terreno.** y
**2. Propuesto con corte y/o relleno.**. Ambas admiten curvas contiguas sin
rectas obligatorias y mantienen la pendiente máxima y el radio mínimo elegidos.
Se genera únicamente la opción seleccionada al pulsar CALCULAR RUTA; los resultados
calculados se conservan para cambiar de propuesta sin repetir la búsqueda.

La opción 1 reconstruye el eje sobre las cotas originales, incorporando todos los
cruces de caras. No utiliza la banda de corte/relleno para ajustar la rasante.
La opción 2 explora rasantes con **corte máximo inicial 2 m y relleno máximo inicial
2 m**, independientes y editables en Configuración avanzada > Diseño. Cada rango
es de 0 al máximo configurado; cero impide ese movimiento. No se elevan estos
límites ni la pendiente para forzar una ruta. Son parámetros de exploración;
la profundidad admisible y la plataforma completa necesitan estudio de ingeniería.
Los controles no representan una norma de estabilidad.

Cuando se han calculado ambas opciones se muestra la reducción de longitud
horizontal en metros y porcentaje, o se indica que no se consiguió una ruta más
corta. La segunda se dibuja naranja y la primera se superpone como referencia
negra. El objetivo inicial es longitud; con costos personalizados se prioriza
el costo validado y podría no resultar una carretera más corta. La exploración
es acotada y no garantiza el óptimo global.

El CSV/DXF/JSON corresponde a la opción seleccionada; la reapertura conserva
su modo y revalida el eje contra la TIN. Solo se guarda esa propuesta; la otra
se calcula de nuevo si se desea comparar. Cambiar parámetros invalida ambas;
cancelar conserva los resultados anteriores. Los proyectos anteriores mantienen
sus límites guardados: Restablecer valores recupera 2 m de corte y 2 m de relleno.
El lanzador prioriza `out/viewer/Release/terrain-options` y permite conservar
abierta una instancia anterior durante la actualización.

La prueba sintética `fixtures/proposal-hill.obj`, A=(-100,0,0), B=(100,0,0),
pendiente 2 %, radio 50 m, ancho 8 m y adaptación desactivada, verifica ambas
opciones con los límites iniciales. No representa una carretera real aprobada.
Los apartados fechados anteriormente documentan los motores y parámetros de esas
revisiones; el visor actual utiliza los valores y el flujo de este apartado.

## Rampas continuas: revisión con los DXF reales, 29-10-2026

El visor siempre permite **espirales y curvas contiguas**, con Hybrid A* y estados XY,
rumbo y cota proyectada. Las primitivas cortas pueden enlazarse sin tangentes
forzadas y dar varias vueltas; el radio mínimo se mantiene. La recta mínima y
el ángulo de curva aislada solo se aplican al modo anterior de la CLI. No se
interpreta el giro acumulado de una espiral como una horquilla instantánea.

La búsqueda recibe una guía Dijkstra de costos del relieve, explora cotas dentro
de una banda de corte/relleno y admite pequeñas subidas si necesita salir de un
valle secundario. Antes de aceptar un candidato se reconstruye su polilínea
contra los cruces de las caras originales, se ajusta la rasante dentro de las
envolventes de profundidad y pendiente, y se valida independientemente. La
pendiente objetivo del ajuste usa un margen numérico del 0.1 % relativo al
límite elegido para no excederlo en segmentos diminutos de una TIN UTM.

La ampliación multipunto intenta construir desde ambos extremos cuando hace
falta. Descarta cruces evidentes durante la exploración y mantiene la comprobación
final de separación. Primero comprueba una conexión recta admisible para evitar
vueltas innecesarias en tramos cortos. Si se permite adaptación, un segundo nivel
refina XY al 75 % del paso objetivo y amplía recursos dentro de los topes
adaptativos. Se mantienen pendiente, radio, ancho y profundidades solicitados.
Se comparan candidatos desde ambos extremos y con dos guías de exploración
(objetivo y cercanía al terreno), conservando el menor costo total validado y,
en empate, la menor longitud. El costo de exploración usa los precios configurados
de vía, suelo/roca, relleno, importación, disposición y acarreo. Con adaptación
se compara también el nivel refinado aunque ya exista una ruta válida; un fallo
del refinamiento conserva el candidato anterior. Cada intento tiene sus propios
presupuestos de búsqueda; explorar más alternativas puede aumentar el tiempo.
El objetivo de longitud no cobra movimiento de tierras. No hay garantía de
óptimo global. La matriz real de 46 casos documentada anteriormente no se ha
repetido para esta selección de candidatos.

Se comprueban también tangencias, radios de los arcos y separación entre tramos
no contiguos de la rampa; se rechazan cruces o vueltas superpuestas a menos del
ancho configurado. Esta separación corresponde al eje muestreado: no sustituye
sección transversal, envolvente vehicular, peralte ni estabilidad de taludes.

Los valores iniciales de la revisión del 29-10 eran pendiente 10 %, radio 50 m, ancho de cálculo
8 m y límites de corte/relleno 30 m. Los 30 m se adoptan para explorar estos DXF,
no como valor normativo ni profundidad necesaria en todos los proyectos. Se
muestran las profundidades efectivamente utilizadas; no se amplían los límites
del usuario en silencio. Al abrir un proyecto sin curvas continuas, el visor
las habilita, conserva los demás parámetros y descarta la ruta anterior para
recalcular. Un radio ausente o cero se actualiza a 50 m.

Los pares de pruebas reales se conservan en
`fixtures/surf-ramp.request` y `fixtures/topografia-ramp.request`; no se afirma
que coincidan con cualquier captura. El par exacto recuperado posteriormente de
la sesión abierta está en `fixtures/surf-selected.request` y también fue probado.
`Abrir-Rampa-Probada.bat` permite abrir directamente ese resultado.

La rampa de proyecto se muestra naranja, porque puede atravesar los bancos
mediante corte o relleno. CSV utiliza `Tipo_tramo=RAMPA_PROYECTO`; DXF conserva
el eje 3D en `ROUTEMINE_RAMPA`. El JSON conserva la configuración y cada arco;
la reapertura vuelve a validar la rasante. El ejecutable de esta revisión está
en `out/viewer/Release/ramps`; el lanzador actual prioriza la versión posterior.

La elección se apoya en [Hybrid A* de Dolgov et al.](https://ai.stanford.edu/~ddolgov/papers/dolgov_gpp_stair08.pdf)
y en la distinción entre rampas interiores y caminos exteriores de
[Yarmuch et al., Optimum ramp design in open pit mines](https://doi.org/10.1016/j.cor.2019.06.013).
El uso de rasantes con excavación/relleno coincide con la distinción de rampas
adaptativas y fijas descrita en la [documentación de Studio OP](https://docs.dataminesoftware.com/StudioOP/Latest/STUDIO_OP/Road%20Design%20Tips.htm).
Es una implementación propia y acotada: no reproduce los optimizadores completos
de esos trabajos ni garantiza el óptimo global o una solución para cualquier A/B.

La descripción siguiente conserva el modo de tangentes/arcos y las propuestas
de atajos anteriores; la exigencia de recta/giro aislado no rige en modo espiral.

## Uso

Desde el 29 de octubre de 2026, el grupo **Diseño** incorpora **Recta mínima
entre curvas (m)**, **Radio mínimo de curva (m)** y **Ángulo máximo por curva (°)**.
Los valores nuevos del visor son 84 m, 50 m y 120°. No se modifica la pendiente
del usuario: **10 % equivale a 5.71°; 10° equivale a 17.63 %**.

### Referencia de los valores iniciales

Se toma 30 km/h como referencia inicial de baja velocidad, ajustable mediante
los valores geométricos. El [MTC DG-2018](https://portal.mtc.gob.pe/transportes/caminos/normas_carreteras/documentos/manuales/Manual.de.Carreteras.DG-2018.pdf),
tabla 302.01 (p. 127), establece para esa velocidad 42 m entre curvas de sentido
opuesto y 84 m del mismo sentido. Se adopta 84 m como mínimo único.
Con su fórmula de radio (§302.04.02) y f=0.17 (tabla 302.02), tomando peralte
favorable cero, R=30²/(127·0.17)=41.69 m; se adopta 50 m con margen.
**120° es un límite elegido para el programa, no un máximo normativo.**
Estos tres valores no representan cumplimiento integral del manual ni un
vehículo minero aprobado. La [guía de caminos mineros IC 8758](https://stacks.cdc.gov/view/cdc/8915)
es una referencia histórica complementaria, no una norma peruana actual.

### Comportamiento con geometría activa

La búsqueda conserva posición y rumbo y combina tangentes con arcos de radio
mínimo. Cada arco cambia el rumbo como máximo el ángulo configurado en total;
no se aplica ese máximo solamente a sus pequeñas cuerdas. Entre arcos, y entre
A/B y el arco contiguo, se exige la recta mínima libre. Una ruta totalmente
recta, sin curvas, puede ser más corta que ese mínimo entre curvas.

Los arcos se discretizan a un máximo de 1° por cuerda y al paso de muestreo
solicitado, conservando cruces de caras TIN. Se comprueban pendiente y cobertura
de cada cuerda; la validación final contrasta radio, barrido, forma, tangencias
y longitud de las rectas con los XYZ. Se valida el eje poligonal muestreado, no
un corredor completo ni la superficie continua entre las cuerdas y el arco.
El espacio reservado es el del eje; faltan sobreancho y envolvente del vehículo.

La búsqueda usa direcciones discretas y tangentes de varias longitudes, con
radio igual al mínimo seleccionado. No es una búsqueda exhaustiva de todos los
radios o trazados. La adaptación permite un segundo intento con menor paso XY
y angular y presupuestos acotados. Un fallo puede deberse al terreno, al espacio
necesario para girar o a la discretización/recursos. No se relajan los controles
ni se sustituye el resultado por el zigzag anterior.

Con geometría activa no se ofrecen los atajos rectos de corte/nivelación del
motor anterior, porque eliminarían arcos y tangencias. Se indica en resultados.
Los proyectos previos conservan la búsqueda anterior al recuperarse: radio 0,
recta 0 y giro 180°. Para aplicar los controles hay que introducir los valores
o usar **Restablecer valores** y recalcular. El JSON guarda los arcos; la
reapertura los vuelve a comprobar. CSV/DXF conservan la polilínea muestreada.
El fixture `flat-hole.obj` con `alignment.request` permite verificar el rodeo
de un hueco con los valores iniciales del visor.

La descripción siguiente documenta el modo anterior y sus propuestas de corte.

Abrir `Abrir-RouteMine.bat`. El lanzador da prioridad al ejecutable actualizado
`out/viewer/Release/roads/routemine_viewer.exe`. Una ventana anterior que ya esté
abierta conserva su versión hasta cerrarla y volver a iniciar la aplicación.

Registrar A y B sobre la topografía e indicar la **pendiente máxima (%)**. El
botón **CALCULAR RUTA** realiza dos operaciones:

1. Busca una ruta sobre la TIN original que cumpla la pendiente máxima. Puede
   desarrollar un recorrido largo y cambiar de dirección para ganar o perder cota.
2. Examina conexiones entre puntos de esa ruta. Si un atajo necesita corte o
   relleno, evita un rodeo de más de cinco veces su longitud y supera las
   comprobaciones geométricas, lo ofrece como segunda opción.

El selector permite alternar entre **1. Ruta según pendiente máxima** y
**2. Propuesta de corte / nivelación**. La segunda permanece deshabilitada si
ninguna conexión evaluada reúne las condiciones. El panel de resultados informa
el motivo y los límites utilizados.

En la segunda opción, negro representa el recorrido original de referencia,
amarillo los tramos que se conservan y rojo el corte/nivelación propuesto. Los
ejes se muestran sobre la imagen del terreno para poder ver el rojo incluso
cuando la rasante pasa bajo un cerro; las coordenadas de cálculo/exportación
conservan su cota real.

## Comparación de cada tramo

Los extremos de cada propuesta son vértices de la primera ruta. Se compara la
longitud 3D recorrida entre ellos con la longitud 3D de la nueva conexión:

`longitud del rodeo / longitud de la propuesta > umbral`

El umbral inicial es **5**, con desigualdad estricta. No basta con que la ruta
completa sea larga ni se colorea una línea en rojo únicamente por ser más corta.
Cada tramo intervenido debe cumplir por sí mismo el umbral. Las propuestas
solapadas se seleccionan dando prioridad a la mayor longitud evitada.

Se comprueba cobertura continua sobre las caras originales, pendiente del eje,
corte máximo y relleno máximo. Se rechazan conexiones que crucen huecos o cotas
contradictorias. El resto de la propuesta conserva los XYZ de la primera ruta.
El resultado muestra distancias, relación, inclinación natural del terreno y
profundidades máximas; las cantidades y los costos son estimaciones del eje.

La inclinación del cerro y la de la carretera son magnitudes diferentes. Un
cerro de **15°** puede requerir excavación para que la carretera resultante
permanezca dentro de la pendiente elegida. 15° equivalen a aproximadamente
26.79 % de pendiente natural: no se interpretan como 15 % de pendiente vial.

## Configuración avanzada

- **Corte / relleno máximo propuesta (m)**: límites independientes, inicialmente
  5 m cada uno. El programa no aumenta esas profundidades para forzar un atajo.
- **Rodeo / atajo: mayor que**: inicialmente 5.
- **Ángulo adicional propuesta (°)**: inicialmente **0°**, según lo solicitado.
  Solo afecta a la segunda opción; la primera conserva la pendiente original.
- **Paso de direcciones (°)**: discretización angular de referencia para buscar
  recorridos. Se añaden direcciones cerca del límite admisible según la
  pendiente de cada cara del terreno.
- **Adaptar resolución y recursos**: busca en varios niveles XY y puede refinar
  y ampliar presupuestos iniciales. Se informa el paso efectivo y los recursos
  consumidos. La ampliación automática utiliza como referencia hasta 2 millones
  de nodos XY y 10 millones de evaluaciones de aristas por intento; un presupuesto
  inicial superior definido por el usuario se conserva.
- **Atajos a comprobar**: presupuesto de comprobaciones exactas de conexiones.

El margen angular utiliza grados, no puntos porcentuales:

`ángulo base = atan(pendiente máxima / 100)`

`pendiente máxima propuesta = 100 * tan(ángulo base + margen angular)`

Un margen de 0° mantiene exactamente el mismo límite vial. La propuesta nunca
interpreta la inclinación natural de un cerro como permiso automático para
superar ese límite. La interfaz muestra porcentaje y grados por separado.

## Motor y límites

El escritorio usa A* sobre nodos XY con cota consultada en la TIN, sin crear
todas las combinaciones XY/Z que provocaban `Lattice exceeds max_states`.
En cada arista se conserva el perfil de las caras originales y se controla su
pendiente, además de validar de nuevo la ruta final. La CLI Dijkstra XY/Z
anterior sigue disponible para compatibilidad.

Cuando la cuadrícula no conecta un punto, también se exploran las caras
originales mediante estados generados en sus bordes a cotas admisibles. Eso
permite cambiar de dirección en una arista del terreno, en lugar de exigir
que un único segmento recto cruce dos laderas con orientaciones distintas.
Los estados se crean según se necesitan, sin reservar un producto completo
de posiciones XY por cotas. Se añaden conexiones locales finas cerca de A/B
cuando hace falta. La malla original permanece intacta.
La exploración sobre caras usa una heurística ponderada para priorizar una
ruta factible; no se presenta como el camino óptimo global.

La búsqueda puede explorar niveles gruesos y finos. Las direcciones adicionales
se obtienen del gradiente local: si la pendiente natural es S y el límite es G,
una dirección respecto a la máxima pendiente debe satisfacer
`abs(S * cos(ángulo)) <= G`. Esto ayuda a generar desarrollos más largos sobre
laderas. No equivale a diseñar radios de curvas vehiculares.

La comparación de atajos utiliza hasta 1000 anclas sobre la ruta y un presupuesto
de comprobaciones. Es una propuesta discreta, no una optimización global de
excavación o geotecnia. No se garantiza una conexión entre cualquier par de
puntos: pueden faltar cobertura, espacio de desarrollo o recursos. Si se agota
la búsqueda, se informa sin publicar una carretera que incumpla la pendiente.

Los puntos manuales deben coincidir con la superficie para la primera opción;
su cota no se cambia en silencio. Los pasos y el dominio Z del generador antiguo
se conservan en sus proyectos/CLI, pero no son controles de la búsqueda nueva.

Siguen pendientes secciones vehiculares, peralte, transiciones, estabilidad de taludes y
autointersecciones. Los ejes obtenidos requieren revisión de ingeniería antes
de considerarse una carretera transitable.

## Exportación y proyectos

Se exporta la variante seleccionada. En la segunda opción el CSV añade
`Tipo_tramo`, referido al segmento que llega al punto: `SOBRE_TERRENO` o
`CORTE_NIVELACION`. El DXF separa el eje amarillo (`ROUTETEC_EJE`) del rojo
(`ROUTETEC_CORTE`, color 1), conservando los XYZ absolutos.

El JSON de una propuesta incluye la primera ruta de referencia y los índices
de cada tramo intervenido. Al abrirlo se vuelven a calcular las distancias,
los ángulos, el corte/relleno y el cumplimiento del umbral. No se confía en los
colores ni en las métricas guardadas. Guardar la segunda opción permite recuperar
también la primera desde su referencia; guardar solo la primera no conserva
atajos que no hayan sido seleccionados.

## Caso de comprobación

`fixtures/hill-15.obj` es un terreno sintético con una loma de 15°, altura
5.35898 m y 40 m de ancho. Usar A=(0,0,0), B=(40,0,0), pendiente máxima 2 %,
corte/relleno máximo 6 m y paso XY objetivo 5 m. La primera ruta desarrolla
longitud sobre la ladera; la segunda puede atravesar la loma con un eje de
40 m a cota constante. Con corte máximo 1 m la propuesta debe rechazarse.
Es un fixture de pruebas, no una recomendación de diseño vial.
