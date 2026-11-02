# Guía visual de usuario de RouteTec

Edición del 1 de noviembre de 2026 para una persona que empieza a usar el programa.
Contiene 30 páginas apaisadas con capturas reales, ampliaciones, recuadros amarillos
y números que corresponden a instrucciones breves. Incluye carga, navegación,
extremos, diseño, búsqueda continua, lectura de resultados, proyectos, exportación
y ajustes opcionales.

## Fuentes

- `capturas/`: imágenes de la aplicación. La ventana de inicio, los menús y los
  diálogos proceden de `RouteTec.exe --capture-guide`. Las vistas `real.png` y
  `live.png` muestran respectivamente el caso de topografía real y un ensayo
  sintético con conexión A–B. No son evidencia de que el caso real esté conectado.
- `00_carpeta.png`: captura del Explorador con el lanzador real
  `Abrir-RouteTec.bat` de este equipo.
- `../../tools/manual_visual_content.json`: títulos e instrucciones editables.
- `../../tools/build_visual_user_guide.py`: composición y numeración de las
  capturas y generación del documento Word. Los textos de las instrucciones son
  editables; los recuadros se regeneran desde las coordenadas del script.

## Regenerar

En Windows, con Python y las dependencias de documentos (`python-docx`, Pillow y
ReportLab), y Poppler (`pdftoppm`) disponible:

```powershell
python tools/build_delivery_documents.py user
./tools/render_delivery_documents.ps1 `
  -Documents 'entrega/RouteTec/Guía de Usuario.docx' `
  -OutputRoot out/manual-visual-20261101/render-reviewed
```

El comando de entrega utiliza este generador visual y las capturas versionadas.
La opción histórica `--captures` sigue correspondiendo a los otros documentos;
para esta guía se deben actualizar las imágenes de `capturas/` y revisar sus
recuadros. La renderización usa una instancia independiente de Microsoft Word,
exporta a PDF y rasteriza todas sus páginas. No modifica documentos abiertos por
el usuario. Los entregables y las imágenes de revisión permanecen en `entrega/`
y `out/`, fuera del control de versiones.

Antes de entregar, comprobar las 30 páginas renderizadas, la correspondencia
entre número y control, la legibilidad de cada campo y que no haya texto cortado.
Copiar el PDF revisado junto al Word en `entrega/RouteTec/`.

## Verificación de esta edición

- Compilación Release del visor y captura automática de la guía completadas.
- Generación mediante el comando de entrega existente comprobada.
- Word y PDF: 30 páginas, 30 figuras numeradas y 26 capturas fuente.
- Verificación por extracción: todos los títulos, pasos y recordatorios están
  presentes en la página prevista del PDF, sin caracteres de sustitución.
- Revisión visual de las 30 páginas, con una segunda revisión independiente de
  las páginas 16 a 30. Se corrigieron recortes, superposiciones de números y
  alineaciones de los recuadros.
- Las figuras ocupan aproximadamente el 55 % del área total de cada página;
  el resto contiene instrucciones breves y espacio de lectura.

Esta verificación cubre el documento y el modo de captura. No constituye una
nueva validación del algoritmo de búsqueda ni una certificación de las rutas.
