# Arquitectura y plan técnico

Objetivo confirmado: aplicación de escritorio Windows/Linux, C++20, CUDA y
renderizado 3D multiplataforma. El núcleo funciona sin GPU NVIDIA.

## Capas elegidas

1. **Dominio C++20**: `Vec3`/TIN/Alignment/DesignParameters/ValidationReport;
   coordenadas de cálculo en `double`, unidades y CRS explícitos en el futuro
   formato de proyecto. Sin tipos de Qt, VTK o CUDA en los contratos del dominio.
2. **Geometría y terreno**: malla original inmutable, índices AABB/BVH para
   localizar triángulos, interpolación baricéntrica; huecos y cotas múltiples
   explícitos. Visualización con origen local y LOD separado del cálculo.
3. **Planificación CPU**: grafo/lattice y A* con estados de dirección y futuras
   primitivas de alineación. Coste, restricciones y heurística por separado;
   verificación posterior independiente. No prometer óptimo global para una
   búsqueda discreta o heurísticas de atajos.
   En movimiento de tierras, buscar eje y rasante final conjuntamente: comparar
   bordear con modificar terreno por costo total, conservando pendiente final,
   radio, límites de corte/relleno y zonas excluidas como restricciones duras.
   Incorporar costos de vía, suelo/roca, colocación, importación, disposición y
   acarreo y reportar sensibilidad/Pareto; no confundir precios con factibilidad.
4. **Backends por lotes**: CPU de referencia; CUDA optativo para evaluaciones
   independientes del terreno/candidatos. Mantener datos en dispositivo cuando
   los perfiles demuestren beneficio. La cola irregular de A* empieza en CPU.
5. **Aplicación**: trabajos con snapshot de terreno/parámetros, IDs y cancelación;
   publicar resultados completos en hilo de UI, descartar resultados obsoletos.
6. **Escritorio Qt 6 Widgets + VTK/OpenGL**: selección A/B sobre TIN, color de cota,
   ejes, leyendas y rutas. Renderizar malla relativa al origen; presentar estados
   pendientes/inviables sin confundirlos con un diseño comprobado.
7. **Adaptadores**: importador DXF con pruebas por tipo de entidad; CSV/DXF y
   proyecto JSON versionado con escritura temporal y reemplazo atómico.

Qt y VTK siguen siendo opcionales. El visor ya implementa el flujo de escritorio
descrito en INTERFAZ_ROUTETEC.md: DXF/OBJ, TIN original, A/B, cálculo en segundo
plano, configuración y proyectos/exportaciones. El adaptador DXF nativo cubre
3DFACE ASCII, contrastado con los dos archivos reales entregados; no sustituye
un importador general de todas las entidades. Las consultas usan un BVH sobre
las caras originales. La alineación vehicular continúa pendiente.

El escritorio usa ahora `plan_road`: busca primero sobre la superficie mediante
A* XY y, si hace falta, estados a cotas admisibles sobre bordes de caras TIN.
Evita construir el producto completo XY/Z; conserva comprobación posterior de
pendiente y cobertura. Después examina atajos entre vértices de la primera ruta,
con corte/relleno limitado, margen angular inicial 0° y relación de distancias
estrictamente mayor que 5. Las propuestas se renderizan en rojo con referencia
negra y se verifican de nuevo al abrir sus proyectos. La búsqueda es discreta y
no garantiza óptimo global. La CLI Dijkstra anterior permanece disponible.
Véase RUTAS_Y_CORTES.md.

## Fases y criterios de salida

| Fase | Entregable | Criterio |
|---|---|---|
| 1 — entregada | Núcleo CPU, TIN, métricas, validación por muestras, CLI y pruebas | MSVC Release + CTest; límites y fallos explícitos |
| 2 — parcial | DXF 3DFACE ASCII e índice BVH entregados | Dos terrenos reales cargados y renderizados; otros tipos de entidad, áreas y cobertura exhaustiva pendientes |
| 3 | Planificador CPU y alineación con tangentes/arcos | Pendiente, radio, recta mínima, curvas horizontales, corte/relleno, ancho y no intersección verificados independientemente; sin solución se informa sin fabricar carretera |
| 4 — parcial | Aplicación Qt/VTK y persistencia base entregadas en Windows | Abrir DXF, A/B, progreso/cancelación, exportar y reabrir comprobados; Linux y distribución pendientes |
| 5 | CUDA medido | Equivalencia CPU/GPU dentro de tolerancia, fallos de dispositivo y benchmark reproducible incluyendo transferencias |
| 6 | Distribución | CI Windows/Linux, artefactos reproducibles, licencias y dependencias inventariadas |

El segundo bloque ya implementa importación OBJ, búsqueda Dijkstra XY/Z y
comprobación de cruces de las caras originales a lo largo de cada segmento,
incluyendo cambios de pendiente del terreno y huecos entre muestras. La
interpolación lineal en caras originales evita rellenar agujeros por una
triangulación global nueva. El índice BVH ya está implementado; sigue pendiente
el modelo de alineación vehicular. Véase GENERADOR_CPU.md para la CLI anterior.

## Entorno CUDA

Equipo observado: RTX 2070 Max-Q, 8 GB, SM 7.5, driver 546.33. No se encontró
`nvcc` ni toolkit en su ubicación estándar. El archivo `.cu` es un primer
backend real de pendientes por lote, sin prueba de compilación/ejecución todavía.
Antes de instalar: seleccionar una combinación compatible de toolkit, driver y
MSVC 19.44; no asumir que la versión más reciente funcionará con el driver actual.
No se instalaron componentes ni se actualizaron drivers.

## Fuentes oficiales

- Qt y plataformas: https://doc.qt.io/qt-6/supported-platforms.html
- Integración VTK/Qt: https://vtk.org/doc/nightly/html/classQVTKOpenGLNativeWidget.html
- CUDA Windows y compiladores: https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html

Las versiones de dependencias deben fijarse al preparar la fase gráfica/GPU;
las páginas actuales no sustituyen una matriz de compatibilidad probada.
