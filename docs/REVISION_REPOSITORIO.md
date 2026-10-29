# Revisión del repositorio RouteMine

Fecha de revisión: 28 de octubre de 2026 (Perú).
Código revisado: b2753d0e808373b99cd1c4719f0d247458eb25a9.

## Resultado

El repositorio contiene un núcleo C++20 y un planificador CPU preliminar funcional,
con configuración básica/avanzada, importación OBJ y exportación de coordenadas CSV.
No equivale todavía a la aplicación de escritorio RouteTec completa.

La evidencia histórica Windows de docs/VALIDACION.md y docs/CTEST_WINDOWS.txt
registra 28 pruebas aprobadas con MSVC. La revisión actual añade compilación
Release con GCC 13.3.0 y CMake 3.28.3 en Linux x64: CTest 28/28 aprobado.
Véase CTEST_LINUX.txt. Las pruebas son sintéticas; no validan topografías reales
ni equivalencia completa con el programa Python original.

## Hallazgos

| Prioridad | Hallazgo | Consecuencia |
|---|---|---|
| Alta | app/viewer.cpp solo dibuja un cono sintético | No hay interfaz terminada para abrir terreno y seleccionar extremos |
| Alta | Falta importación DXF | El flujo disponible exige terreno OBJ triangulado |
| Alta | Faltan verificaciones vehiculares de curvas, radios y secciones | El corredor no debe presentarse como carretera certificada |
| Media | CUDA está escrito pero no probado | No se acredita aceleración GPU ni equivalencia CPU/GPU |
| Media | Consultas de terreno recorren caras sin índice espacial | Rendimiento con mallas grandes pendiente de medir |
| Baja | README negaba exportación CSV y existencia de Git | Se corrigieron ambas afirmaciones |
| Baja | GCC emite avisos de sangría engañosa en config.cpp, geometry.cpp y obj.cpp | Conviene expandir las instrucciones comprimidas para facilitar mantenimiento |

main, integration y develop apuntaban al mismo snapshot inicial al comenzar la
revisión. No había workflows ni Releases y no se distribuían ejecutables.

## Entrega

Se incluyen ejecutables CPU en bin/, instrucciones, demostración para Windows
y hashes SHA256. El binario Windows se compila con MinGW-w64 x64 y runtime
estático. Su ejecución nativa está pendiente; no se confunde con las pruebas
MSVC históricas. El binario Linux se ha ejecutado en este entorno.

Se añade un workflow que compila y ejecuta CTest en Windows y Linux y publica
artefactos por commit. Sus resultados se consultan en GitHub Actions; el archivo
del workflow por sí solo no demuestra que una ejecución haya aprobado.

Siguiente trabajo recomendado: importación DXF con un archivo real y visor de
terreno/ruta, seguido de validación geométrica vehicular y rendimiento.
