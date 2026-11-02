"""Build the RouteTec manuals and a complete, verifiable source-code volume.

Run with the document runtime supplied by Codex or with python-docx and Pillow.
Screenshots are real application captures, never drawn UI mockups.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import textwrap
from pathlib import Path
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'entrega' / 'RouteTec'
QA = ROOT / 'out' / 'delivery-docs'
BLUE = '18556A'

def field(p, instruction):
    f = OxmlElement('w:fldSimple'); f.set(qn('w:instr'), instruction); p._p.append(f)

def setup(title, subtitle, landscape=False):
    d = Document(); s=d.sections[0]
    s.page_width=Inches(11 if landscape else 8.5)
    s.page_height=Inches(8.5 if landscape else 11)
    s.top_margin=s.bottom_margin=Inches(.65)
    s.left_margin=s.right_margin=Inches(.7)
    normal=d.styles['Normal'];normal.font.name='Calibri';normal.font.size=Pt(11)
    normal.font.color.rgb=RGBColor(0,0,0)
    normal.paragraph_format.space_after=Pt(7)
    normal.paragraph_format.line_spacing=1.08
    for name,size in [('Title',28),('Heading 1',19),('Heading 2',14),('Heading 3',12)]:
        style=d.styles[name];style.font.name='Calibri';style.font.size=Pt(size)
        style.font.color.rgb=RGBColor.from_string('000000' if name=='Title' else BLUE)
        style.paragraph_format.space_before=Pt(13);style.paragraph_format.space_after=Pt(8)
    for style in d.styles:
        if style._element.pPr is not None:
            for border in list(style._element.pPr.findall(qn('w:pBdr'))):
                style._element.pPr.remove(border)
    d.styles['Subtitle'].font.color.rgb=RGBColor.from_string('555555')
    h=s.header.paragraphs[0];h.text='RouteTec    |    Versión 1.0.0';h.style='Caption'
    foot=s.footer.paragraphs[0];foot.alignment=WD_ALIGN_PARAGRAPH.RIGHT
    foot.add_run('RouteTec   ·   ');field(foot,'PAGE')
    d.add_paragraph(title,'Title');d.add_paragraph(subtitle,'Subtitle')
    d.add_paragraph('Edición del 30 de octubre de 2026')
    return d

def body(d, text):
    return d.add_paragraph(text)

def table(d, headers, rows):
    t=d.add_table(rows=1, cols=len(headers));t.style='Light Shading Accent 1'
    if len(headers)==2:
        t.autofit=False;t.columns[0].width=Inches(2.15);t.columns[1].width=Inches(4.95)
    for c,v in zip(t.rows[0].cells,headers):c.text=v
    pr=t.rows[0]._tr.get_or_add_trPr();pr.append(OxmlElement('w:tblHeader'))
    for row in rows:
        for c,v in zip(t.add_row().cells,row):c.text=str(v)
    for i,row in enumerate(t.rows):
        if len(headers)==2:
            row.cells[0].width=Inches(2.15);row.cells[1].width=Inches(4.95)
        for cell in row.cells:
            for p in cell.paragraphs:
                p.paragraph_format.space_before=Pt(3);p.paragraph_format.space_after=Pt(4)
                for r in p.runs:r.font.color.rgb=RGBColor.from_string(BLUE if i==0 else '111111')
    return t

def figure(d, path, caption, max_height=5.6):
    path=Path(path)
    if not path.exists():raise FileNotFoundError(path)
    w,h=Image.open(path).size
    width=min(7.05,max_height*w/h)
    p=d.add_paragraph();p.paragraph_format.keep_with_next=True
    p.alignment=WD_ALIGN_PARAGRAPH.CENTER
    r=p.add_run();r.add_picture(str(path),width=Inches(width))
    inline=r._r.xpath('.//wp:docPr')
    if inline:inline[0].set('descr',caption)
    p=d.add_paragraph(caption,'Caption');p.paragraph_format.keep_with_next=False

def chapter(d,title,text=None):
    heading=d.add_heading(title,1);heading.paragraph_format.page_break_before=True
    if text:body(d,text)

def source_files():
    extensions={'.cpp','.hpp','.h','.cu','.c','.py','.ps1','.bat','.cmake','.nsi','.iss','.json','.yaml','.yml','.txt','.request','.md','.rc','.manifest','.in'}
    fs=[]
    for folder in ['app','include','src','tests','packaging','tools']:
        base=ROOT/folder
        if base.exists():
            fs += [p for p in base.rglob('*') if p.is_file() and p.suffix.lower() in extensions and '__pycache__' not in p.parts]
    fs += [p for p in ROOT.iterdir() if p.is_file() and (p.name=='CMakeLists.txt' or p.suffix in {'.bat','.yml','.yaml'})]
    return sorted(set(fs),key=lambda p:p.relative_to(ROOT).as_posix().casefold())

FOLDER_INFO={
    'app':'Interfaz nativa, visualización tridimensional, persistencia de proyectos y entrada de la línea de comandos.',
    'include':'Contratos públicos del núcleo de cálculo, modelos geométricos y configuración.',
    'include/routemine':'Cabeceras del espacio de nombres técnico conservado por compatibilidad con el núcleo.',
    'src':'Algoritmos de búsqueda, mallas, geometría, validación, movimientos de tierra y exportación.',
    'tests':'Pruebas automatizadas, regresiones geométricas y programas de evaluación con terrenos reales.',
    'packaging':'Preparación de dependencias, instalación y distribución para Windows.',
    'tools':'Herramientas de documentación y comprobación de la entrega.',
    'raíz':'Definición de compilación, restauración del entorno y lanzadores locales.'}

def describe(p):
    n=p.name
    exact={
      'viewer.cpp':'Ventana principal, diálogos de configuración, tareas asíncronas, controles y pruebas gráficas.',
      'terrain_view.cpp':'Escena VTK, terreno y carretera, cámaras, selección de puntos, leyenda y rutas exploradas.',
      'project_io.cpp':'Lectura y escritura atómica de proyectos JSON y restauración de rutas.',
      'core.hpp':'Tipos, parámetros y funciones públicas de geometría, búsqueda, cálculo y validación.',
      'config.hpp':'Interfaz pública de resolución y persistencia de configuración.',
      'config.cpp':'Valores iniciales, lectura de parámetros, validación y serialización de configuración.',
      'ramp_search.cpp':'Planificación de curvas continuas y reconstrucción de rasante validada.',
      'road_search.cpp':'Selección de estrategias de búsqueda y planificación de alternativas.',
      'adaptive_road.cpp':'Secuencia controlada de intentos de planificación y registro de parámetros efectivos.',
      'route_refinement.cpp':'Refinamiento y optimización de intervalos de una ruta.',
      'alignment.cpp':'Geometría y validación de alineaciones tangentes y curvas.',
      'geometry.cpp':'Consultas geométricas de la malla y perfiles sobre sus caras originales.',
      'validation.cpp':'Comprobación independiente de pendiente, cobertura y límites geométricos.',
      'cost.cpp':'Cantidades y objetivo de comparación de rutas.',
      'dxf.cpp':'Importación de caras de terreno desde DXF.',
      'obj.cpp':'Importación y validación de mallas OBJ.',
      'terrain_io.cpp':'Selección del lector según el formato del archivo topográfico.',
      'earthwork_segments.cpp':'Clasificación de segmentos de superficie, corte y relleno.',
      'route_export.cpp':'Exportación de ejes y segmentos a CSV y DXF.',
      'search.cpp':'Búsqueda discreta del planificador original.',
      'backend_cpu.cpp':'Backend de cálculo de pendientes en CPU.',
      'backend_cuda.cu':'Backend CUDA opcional que no se requiere para la edición distribuida.',
      'search_trace.hpp':'Intercambio acotado entre búsqueda y visualización de ramas exploradas.',
      'main.cpp':'Entrada de los comandos de inspección, demostración y planificación.',
      'plan_cli.cpp':'Ejecución de planificación por línea de comandos y escritura del resultado.',
      'CMakeLists.txt':'Definición de biblioteca, ejecutables, opciones, dependencias y pruebas.',
      'build_delivery_documents.py':'Generación de manuales ilustrados y del volumen completo del código fuente.'}
    if n in exact:return exact[n]
    if p.parent.name=='tests':return 'Casos y utilidades de prueba para '+p.stem.replace('_',' ')+'.'
    if p.suffix=='.hpp':return 'Declaraciones y contratos utilizados por '+p.stem.replace('_',' ')+'.'
    if p.suffix=='.bat':return 'Lanzador Windows del aplicativo o de un escenario de prueba.'
    if 'packaging' in p.parts:return 'Herramienta de distribución e instalación de RouteTec para Windows.'
    return 'Configuración o herramienta auxiliar de '+p.stem.replace('_',' ')+'.'

def build_source():
    d=setup('Código fuente de RouteTec','Organización por carpetas y archivos con transcripción íntegra del código',True)
    files=source_files()
    body(d,'Este volumen contiene el código propio del aplicativo, las pruebas y las herramientas de compilación, documentación y empaquetado. Los nombres técnicos del espacio de nombres routemine se conservan para compatibilidad; el nombre comercial y el ejecutable de distribución son RouteTec.')
    body(d,'No se transcriben bibliotecas externas Qt, VTK y runtimes, carpetas generadas out y build, historial .git, ejecutables, capturas ni documentos binarios. Las licencias de terceros se distribuyen con el instalador. Los terrenos de ejemplo son datos y no código fuente.')
    manifest=[]
    table(d,['Carpeta','Descripción'],list(FOLDER_INFO.items()))
    table(d,['Archivos','Integridad'],[(str(len(files)),'Cada archivo incluye SHA-256 y número de líneas para cotejar con el repositorio entregado.')])
    for p in files:
        raw=p.read_bytes(); text=raw.decode('utf-8-sig');lines=text.splitlines()
        rel=p.relative_to(ROOT).as_posix();sha=hashlib.sha256(raw).hexdigest()
        manifest.append({'path':rel,'sha256':sha,'lines':len(lines),'bytes':len(raw)})
        chapter(d,rel)
        body(d,describe(p));body(d,f'{len(lines)} líneas · {len(raw)} bytes\nSHA-256 {sha}')
        for line_index,line in enumerate(lines):
            par=d.add_paragraph();pf=par.paragraph_format
            pf.space_before=pf.space_after=Pt(0);pf.line_spacing=1
            # Keep the file's final block together so a closing brace cannot
            # become an otherwise empty continuation page.
            pf.keep_with_next=(line_index>=max(0,len(lines)-8) and line_index<len(lines)-1)
            pf.widow_control=False
            # Native Word soft wrapping retains every source character, including tabs.
            run=par.add_run(line);run.font.name='Consolas';run.font.size=Pt(9)
    target=OUT/'Código fuente.docx';d.save(target)
    QA.mkdir(parents=True,exist_ok=True)
    (QA/'source-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    return target

def build_user(captures):
    # The visual guide owns its reviewed, versioned captures and page content.
    # Keep the existing delivery command as the public entry point.
    from build_visual_user_guide import build
    return build()

def build_installation(captures):
    captures=captures or ROOT/'out/installer-captures'
    d=setup('Guía de instalación de RouteTec','Requisitos y puesta en marcha de la edición Windows de 64 bits')
    body(d,'Instale RouteTec con el paquete RouteTec-Setup-1.0.0.exe. El instalador incluye el aplicativo, Qt, VTK y las bibliotecas de ejecución de Visual C++ necesarias. No necesita instalar un lenguaje de programación, un compilador ni un framework por separado.')
    body(d,'La instalación es local para la cuenta de Windows que la ejecuta. No necesita Internet durante la instalación ni una cuenta de servicio para utilizar el programa.')
    table(d,['Requisito','Condición'],[
      ('Sistema operativo','Windows 10 de 64 bits versión 1809 o posterior, o Windows 11 de 64 bits.'),
      ('Gráficos','Controlador gráfico con OpenGL 3.2 o posterior para la vista tridimensional.'),
      ('Memoria','Se recomiendan 16 GB de RAM para topografías grandes. El consumo depende del terreno y los recursos de búsqueda.'),
      ('Espacio','Reserve al menos 500 MB para aplicativo y paquete. Los levantamientos, proyectos y exportaciones necesitan espacio adicional.'),
      ('Permisos','Permiso de escritura en la carpeta de su perfil. La instalación predeterminada no solicita privilegios de administrador.'),
      ('Programas adicionales','No requiere Python, Visual Studio, conda, Qt ni VTK instalados en el sistema.')])
    body(d,'El ejecutable distribuido utiliza el CPU. No requiere CUDA ni una GPU NVIDIA. La aceleración gráfica de la escena sí depende del controlador OpenGL.')
    pages=[
      ('01-bienvenida','Iniciar el instalador','Cierre una instancia de RouteTec que esté usando la misma carpeta de destino. Haga doble clic en RouteTec-Setup-1.0.0.exe y pulse Siguiente.',[
          ('Archivo','Use el instalador entregado con esta edición. Mantenga una copia para reinstalar.'),('Identidad','El nombre del producto y la versión deben indicar RouteTec 1.0.0.')]),
      ('02-carpeta','Elegir la carpeta de instalación','Conserve la carpeta propuesta o pulse Examinar para seleccionar otro destino propio. Confirme Instalar.',[
          ('Destino inicial',r'%LOCALAPPDATA%\Programs\RouteTec'),('Carpeta de trabajo','Guarde topografías y proyectos en Documentos u otra carpeta independiente de la instalación.')]),
      ('03-archivos-instalados','Comprobar los archivos instalados','El instalador copia el programa, bibliotecas, plugins gráficos, ejemplos y licencias. No descarga componentes de Internet. La captura muestra una instalación de prueba ya completada.',[
          ('Dependencias','Qt y VTK se copian junto al ejecutable.'),('Visual C++','Las bibliotecas de ejecución se incluyen localmente; no se necesita instalar el compilador C++.'),('Interrupción','Si la copia no termina, vuelva a ejecutar el instalador después de cerrar RouteTec.')]),
      ('04_finalizar','Finalizar y abrir RouteTec','Al terminar, pulse Finalizar. Abra RouteTec desde el acceso directo del escritorio o desde el menú Inicio.',[
          ('Inicio','Busque RouteTec en el menú Inicio.'),('Ejecutable','También puede abrir RouteTec.exe en la carpeta instalada.'),('Ejemplos','La subcarpeta Ejemplos contiene mallas de práctica expresadas en metros.')]),
    ]
    for filename,title,text,rows in pages:
        chapter(d,title,text)
        p=captures/(filename+'.png')
        if p.exists():figure(d,p,title,5.2)
        table(d,['Elemento','Qué hacer o comprobar'],rows)
    chapter(d,'Comprobar el primer inicio')
    body(d,'Cargue una malla de la carpeta Ejemplos, registre A y B sobre ella y pulse Calcular ruta. Use un par cercano sobre una zona llana para comprobar primero la apertura, selección y dibujo del resultado. El manual de usuario contiene el procedimiento completo.')
    p=captures/'05_aplicacion_instalada.png'
    if p.exists():figure(d,p,'RouteTec iniciado desde una instalación independiente del entorno de desarrollo.',4.8)
    table(d,['Comprobación','Resultado esperado'],[
      ('Inicio','Aparece RouteTec y la vista tridimensional.'),('Topografía','Se dibuja la malla sin errores de bibliotecas.'),('A y B','Se registran los dos puntos elegidos.'),('Carretera','Se dibuja la ruta cuando el par y las restricciones permiten una solución.'),('Guardar','Puede guardar un JSON en su carpeta de trabajo.')])
    chapter(d,'Usar la edición portable')
    body(d,'Como alternativa al instalador, extraiga RouteTec-Portable-1.0.0.zip en una carpeta propia y abra RouteTec.exe dentro de RouteTec. Mantenga toda la carpeta unida: las DLL, platforms, plugins y Licencias son parte del aplicativo. No ejecute el programa directamente desde la vista comprimida del ZIP.')
    table(d,['Edición','Diferencia práctica'],[
      ('Instalable','Copia los archivos, crea accesos directos y añade un desinstalador.'),('Portable','No necesita instalación. El usuario extrae y conserva completa la carpeta.'),('Funcionalidad','Ambas utilizan el mismo aplicativo y los mismos algoritmos.')])
    chapter(d,'Actualizar y desinstalar')
    body(d,'Antes de actualizar, guarde sus proyectos y cierre RouteTec. Ejecute el instalador de la nueva edición sobre la carpeta de la instalación anterior. Conserve por separado sus topografías y resultados.')
    body(d,'Para desinstalar, abra Configuración de Windows, Aplicaciones, RouteTec y Desinstalar, o ejecute Desinstalar.exe en la carpeta instalada. Confirme únicamente después de guardar y cerrar su trabajo. El desinstalador elimina los archivos de distribución registrados y conserva archivos ajenos al manifiesto. No use la carpeta instalada para almacenar sus únicos respaldos.')
    p=captures/'04-desinstalar.png'
    if p.exists():figure(d,p,'Ventana de confirmación de desinstalación de RouteTec.',4.4)
    chapter(d,'Resolver problemas de instalación e inicio')
    table(d,['Mensaje o síntoma','Acción'],[
      ('Sistema no compatible','Compruebe que Windows sea de 64 bits y cumpla la versión mínima.'),
      ('No puede escribir en destino','Elija una carpeta de su perfil; cierre otras instancias del aplicativo.'),
      ('Falta una DLL','Reinstale el paquete completo. No descargue DLL aisladas de sitios desconocidos.'),
      ('Error del plugin de plataforma Qt','Conserve la carpeta platforms junto al aplicativo; vuelva a extraer o reinstalar el paquete completo.'),
      ('Vista negra o error de OpenGL','Actualice el controlador desde el fabricante de la GPU y compruebe el soporte de OpenGL 3.2. En escritorios remotos depende del controlador disponible.'),
      ('No encuentra un terreno al abrir JSON','Restaure el archivo topográfico referenciado por el proyecto.'),
      ('No encuentra una carretera','La instalación puede ser correcta. Revise A/B, cobertura y restricciones con la guía de usuario.')])
    chapter(d,'Componentes incluidos y requisitos técnicos')
    table(d,['Componente','Función'],[
      ('RouteTec.exe','Aplicativo compilado para Windows x64.'),('Qt 6','Ventanas, controles e integración nativa.'),('VTK','Representación y selección en la escena tridimensional.'),('Visual C++ Runtime','Ejecución del programa C++ y sus bibliotecas. Se distribuye junto al ejecutable.'),('Plugins y dependencias','Soporte de plataforma Windows y bibliotecas utilizadas por Qt y VTK.'),('Licencias','Textos y versiones de terceros incluidos en la carpeta Licencias.')])
    body(d,'No instale lenguajes o frameworks adicionales para ejecutar este paquete. Solo una reconstrucción desde código fuente requiere herramientas de desarrollo; esa tarea queda fuera de la instalación del usuario final.')
    body(d,'Fuentes de requisitos y distribución: documentación oficial de plataformas compatibles de Qt 6.10 y documentación de Microsoft sobre distribución de bibliotecas Visual C++.')
    body(d,'https://doc.qt.io/qt-6.10/supported-platforms.html')
    body(d,'https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170')
    path=OUT/'Guía de Instalación.docx';d.save(path);return path

def main():
    p=argparse.ArgumentParser();p.add_argument('kind',choices=['source','user','installation','all']);p.add_argument('--captures',type=Path);a=p.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    if a.kind in {'source','all'}: print(build_source())
    if a.kind in {'user','all'}: print(build_user(a.captures))
    if a.kind in {'installation','all'}: print(build_installation(a.captures))

if __name__=='__main__':main()
