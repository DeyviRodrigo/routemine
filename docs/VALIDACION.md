# Entorno y validación

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
