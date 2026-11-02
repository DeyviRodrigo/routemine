"""Rebuild the illustrated novice guide from real captures and reviewed text.
Run with the bundled document Python. Word/PDF rendering is a separate QA step.
"""
from pathlib import Path
import json,io,subprocess,shutil
from PIL import Image
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from docx import Document
from docx.shared import Inches,Pt,RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'docs/manual_visual/capturas'
WORK=ROOT/'out/manual-visual-20261101'
FIG=WORK/'figures';FIG.mkdir(parents=True,exist_ok=True)
OUT=ROOT/'entrega/RouteTec';OUT.mkdir(parents=True,exist_ok=True)
pdfmetrics.registerFont(TTFont('Guide','C:/Windows/Fonts/arial.ttf'))
pdfmetrics.registerFont(TTFont('GuideBold','C:/Windows/Fonts/arialbd.ttf'))
W,H=1400,650
c=None

def label(text,x,y,size=22):
    c.setFillColor(HexColor('#172D3A'));c.setFont('GuideBold',size);c.drawString(x,H-y,text)
def badge(n,x,y):
    c.setFillColor(HexColor('#FFCF43'));c.setStrokeColor(HexColor('#102C39'));c.setLineWidth(2)
    c.circle(x,H-y,18,stroke=1,fill=1);c.setFillColor(HexColor('#102C39'));c.setFont('GuideBold',21);c.drawCentredString(x,H-y-7,str(n))
def pane(name,box=(35,20,1330,610),crop=None,marks=(),outside=False):
    im=Image.open(ASSETS/name).convert('RGB')
    if crop:im=im.crop(crop)
    x,y,bw,bh=box;sw,sh=im.size;scale=min(bw/sw,bh/sh);ww,hh=sw*scale,sh*scale
    x+=(bw-ww)/2;y+=(bh-hh)/2
    c.drawImage(ImageReader(im),x,H-y-hh,ww,hh)
    for n,rx,ry,rw,rh in marks:
        xx,yy=x+rx*scale,y+ry*scale
        c.setStrokeColor(HexColor('#FFCF43'));c.setLineWidth(3)
        c.roundRect(xx,H-yy-rh*scale,rw*scale,rh*scale,5,stroke=1,fill=0)
        if outside or any(t in name for t in ('diseno','precision','costos','recursos','panel_controles','intermedios')):
            bx=max(20,x-24);by=max(20,yy)
            c.line(bx+18,H-by,xx,H-yy);badge(n,bx,by)
        else:badge(n,max(20,xx-19),max(20,yy+10))
    return x,y,scale

def fig(id):
    global c
    path=FIG/(id+'.pdf');c=canvas.Canvas(str(path),pagesize=(W,H));c.setFillColor(HexColor('#F1F5F7'));c.rect(0,0,W,H,stroke=0,fill=1)
    panel='05_panel_controles.png'
    if id=='portada':
        pane('real.png',(330,0,1060,650),crop=(460,280,1745,1060)); label('APRENDA',35,70,24);label('CON IMÁGENES',35,106,24)
        for n,t in enumerate(['Cargar el terreno','Marcar A y B','Buscar y revisar','Guardar y exportar'],1):badge(n,65,140+n*75);label(t,100,149+n*75,24)
        label('30 láminas paso a paso',35,590,22)
    elif id=='iniciar':
        pane('00_carpeta.png',(35,20,700,610),marks=[(1,0,0,620,52),(2,15,612,600,34)])
        pane('01_inicio.png',(790,125,570,420));badge(3,790,115);label('La ventana que debe aparecer',800,90,21)
    elif id=='pantalla':pane('01_inicio.png',marks=[(1,10,60,435,125),(2,10,205,435,480),(3,465,55,1260,1005),(4,12,810,430,106)])
    elif id=='cargar':
        pane(panel,(40,65,490,370),crop=(0,0,441,165),marks=[(1,32,50,380,45)])
        pane('26_cargar_topografia.png',(570,15,795,620),marks=[(2,103,14,430,36),(3,170,60,630,190),(3,735,442,67,42)])
    elif id=='carga_progreso':
        pane('02_progreso_carga.png',(30,30,850,535),marks=[(1,7,8,647,238),(2,410,320,110,45)])
        pane(panel,(920,100,440,300),crop=(0,0,441,165),marks=[(3,25,100,380,50)])
        label('VER PROGRESO vuelve a abrir el avance',40,605,24)
    elif id=='vistas':
        pane('19_menu_vistas.png',(45,15,370,620),marks=[(1,10,5,195,75),(2,10,283,195,78),(4,10,82,195,196)])
        pane('real.png',(470,20,900,590),crop=(475,310,1740,1040));badge(3,490,610)
    elif id in ('origen','destino'):
        yy=175 if id=='origen' else 425
        pane('real.png',(40,75,540,500),crop=(0,190 if id=='origen' else 440,450,440 if id=='origen' else 690),marks=[(1,230,180,195,43),(3,48,45,378,130)],outside=True)
        crop=(1100,260,1490,510) if id=='origen' else (900,500,1280,760)
        pane('real.png',(650,85,710,480),crop=crop,marks=[(2,178,65,60,52)] if id=='origen' else [(2,170,95,64,58)])
        label('El clic coloca el punto y completa X Y Z',650,610,23)
    elif id=='coordenadas':
        pane(panel,(50,35,620,580),crop=(0,175,441,415),marks=[(1,50,40,360,83),(2,50,130,360,40),(3,32,175,187,42)])
        pane(panel,(755,35,600,580),crop=(0,425,441,665),marks=[(4,20,40,400,180)])
    elif id=='contexto_diseno':
        label('Abra Configuración',35,130,22)
        pane('09_menu_configuracion.png',(35,160,330,170),marks=[(1,6,4,232,32)])
        pane('10_diseno.png',(425,20,935,610),crop=(0,40,813,500),marks=[(1,315,42,450,40),(2,315,87,450,124),(3,315,219,450,43),(3,25,300,300,40),(4,315,344,450,43)])
    elif id=='geometria':
        pane('10b_diseno_geometria.png',(30,25,825,600),crop=(0,275,813,638),marks=[(1,315,39,450,42),(2,315,128,450,42),(3,315,215,450,44)])
        pane('10c_diseno_criterios.png',(890,50,480,300),crop=(0,505,813,638),marks=[(4,34,5,730,46)])
        label('Aplicar criterios de diseño',900,145,23)
        label('Pendiente = 100 × desnivel',900,430,21);label('/ distancia horizontal',900,460,21)
        c.setStrokeColor(HexColor('#1B7580'));c.setLineWidth(5);c.line(920,80,1310,80);c.line(920,80,1310,119);c.line(1310,80,1310,119)
        label('20 m',1070,610,23);label('2 m',1320,550,23);label('10 %',1055,520,28)
    elif id=='intervencion':
        label('Abra Configuración',35,130,22)
        pane('09_menu_configuracion.png',(35,160,330,170),marks=[(1,6,44,232,32)])
        label('Pestaña Diseño',35,390,23)
        pane('11_avanzado_diseno.png',(425,20,935,610),crop=(0,180,813,600),marks=[(1,350,43,425,42),(2,350,131,425,42),(3,350,220,425,44),(4,350,270,425,44)])
        label('Al terminar',35,440,22)
        pane('11_avanzado_diseno.png',(70,465,260,130),crop=(720,845,813,913),marks=[(4,15,11,64,42)])
    elif id=='buscar':
        pane(panel,(35,15,500,620),crop=(0,160,441,725),marks=[(1,10,34,420,470),(2,10,514,420,44)])
        pane('live.png',(570,80,800,500),crop=(460,50,1750,1000));badge(3,590,75)
    elif id=='detener':
        pane('live.png',(45,20,700,410),crop=(0,820,455,955),marks=[(1,20,38,410,95)])
        pane('live.png',(65,465,1270,145),crop=(1170,1005,1750,1057),marks=[(2,253,5,133,43),(3,391,5,168,43)])
        label('Espere a que los controles vuelvan a habilitarse',775,220,21);badge(4,785,170)
    elif id=='colores':
        pane('real.png',(60,90,930,450),crop=(490,78,902,230),marks=[(1,30,10,375,33),(2,30,44,375,66),(3,30,111,375,35)])
        pane('real.png',(1100,15,240,620),crop=(1590,510,1700,1000),marks=[(4,2,5,102,477)])
    elif id=='conexion':
        label('TODAVÍA PENDIENTE',45,45,25);pane('real.png',(35,80,650,240),crop=(10,830,447,872),marks=[(1,10,7,415,31)])
        label('CONEXIÓN ENCONTRADA',750,45,25);pane('live.png',(740,80,620,240),crop=(10,855,448,890),marks=[(2,15,2,410,31)])
        pane('19_menu_vistas.png',(500,400,370,240),crop=(0,0,230,83),marks=[(3,5,8,215,70)])
    elif id=='detalles':
        pane('20_menu_ayuda.png',(35,65,405,200),marks=[(1,5,2,217,40)])
        pane('21_detalles_resultado.png',(490,15,870,620),marks=[(2,15,10,990,520),(3,935,695,75,44)])
    elif id in ('guardar','exportar_csv','exportar_dxf'):
        row={'guardar':44,'exportar_csv':128,'exportar_dxf':170}[id]
        pane('08_menu_archivo.png',(35,90,390,395),marks=[(1 if id=='guardar' else 2,8,row,320,40)])
        file={'guardar':'23_guardar_proyecto.png','exportar_csv':'24_exportar_csv.png','exportar_dxf':'25_exportar_dxf.png'}[id]
        pane(file,(470,20,900,600),marks=[(2 if id=='guardar' else 3,106,442,620,42),(3,735,442,67,42)])
        if id=='guardar':label('4   Guardar proyecto como…',35,550,21);label('crea otra copia',35,585,21)
        else:label('Primero detenga la búsqueda',35,600,23);badge(1,45,550)
    elif id=='abrir_proyecto':
        pane('08_menu_archivo.png',(40,60,470,500),marks=[(1,8,4,320,40)])
        pane('23_guardar_proyecto.png',(570,70,800,500),crop=(0,0,816,220),marks=[(2,170,91,470,29)])
        label('Seleccione el .json guardado',600,465,25);label('3   Localice el DXF u OBJ si se lo solicita',570,520,22);label('4   Espere y revise los datos recuperados',570,575,22)
    elif id=='habilitar_intermedios':
        label('Abra Configuración',35,130,22)
        pane('09_menu_configuracion.png',(35,160,330,170),marks=[(1,6,84,232,32)])
        pane('15_configuracion_intermedios.png',(425,20,935,610),marks=[(1,10,7,680,40),(2,240,267,450,145),(3,625,416,68,45)])
    elif id=='optimizar_intermedios':
        label('Seleccione los puntos sobre la ruta',50,75,28)
        for n,crop,box in [(1,(20,39,207,83),(90,140,530,180)),(2,(213,39,400,83),(780,140,530,180)),(3,(20,89,207,133),(90,370,530,180)),(4,(213,89,400,133),(780,370,530,180))]:
            pane('17_cuatro_botones_intermedios.png',box,crop=crop,marks=[(n,1,1,184,41)])
    elif id=='precision':pane('12_avanzado_precision.png',crop=(0,180,813,650),marks=[(1,202,43,575,45),(2,202,132,575,45),(3,202,220,575,45),(4,202,307,575,45)])
    elif id=='recursos':pane('13_avanzado_recursos.png',crop=(0,180,813,715),marks=[(1,30,38,735,40),(2,200,82,575,135),(3,200,256,575,44),(4,200,344,575,135)])
    elif id=='objetivo_costos':pane('14c_perfiles_costos.png',marks=[(1,4,4,710,34),(2,4,40,710,34),(3,4,80,710,34)])
    elif id=='tarifas':pane('14_avanzado_costos.png',crop=(0,220,813,790),marks=[(1,200,48,575,43),(2,200,135,575,133),(3,200,312,575,43),(4,200,400,575,132)])
    elif id=='acarreos':pane('14b_costos_acarreos.png',crop=(0,400,813,913),marks=[(1,210,10,550,44),(2,210,97,550,44),(3,210,185,550,220),(4,733,455,66,45)])
    elif id=='reiniciar':
        pane('08_menu_archivo.png',(50,35,480,240),crop=(0,80,338,132),marks=[(1,4,3,330,43)])
        pane('11_avanzado_diseno.png',(630,35,730,240),crop=(0,0,813,145),marks=[(2,13,40,787,45)])
        pane(panel,(90,350,600,230),crop=(0,670,441,775),marks=[(3,10,60,420,42)])
        pane(panel,(815,340,450,270),crop=(0,175,441,415),marks=[(4,20,34,400,186)])
    elif id=='problemas':
        pane(panel,(20,30,600,270),crop=(0,175,441,415),marks=[(1,20,34,400,186)])
        pane('08_menu_archivo.png',(750,30,570,270),crop=(0,126,338,212),marks=[(2,4,3,330,80)])
        pane('20_menu_ayuda.png',(35,380,570,230),marks=[(3,4,4,220,36)])
        pane('12_avanzado_precision.png',(750,360,570,250),crop=(0,300,813,395),marks=[(4,202,12,575,45)])
    else:raise ValueError(id)
    c.save()
    subprocess.run(['pdftoppm','-scale-to','2000','-singlefile','-png',str(path),str(FIG/id)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return FIG/(id+'.png')

def build():
    pages=json.loads((ROOT/'tools/manual_visual_content.json').read_text(encoding='utf-8'))
    doc=Document();s=doc.sections[0];s.page_width=Inches(11.69);s.page_height=Inches(8.27)
    s.top_margin=Inches(.38);s.bottom_margin=Inches(.38);s.left_margin=s.right_margin=Inches(.48)
    s.header_distance=s.footer_distance=Inches(.15)
    for name,size in [('Normal',11),('Title',25),('Heading 1',23),('Subtitle',11)]:
        st=doc.styles[name];st.font.name='Arial';st.font.size=Pt(size);st.font.color.rgb=RGBColor(0,0,0)
        st.paragraph_format.space_after=Pt(3);st.paragraph_format.space_before=Pt(0)
    doc.styles['Normal'].paragraph_format.line_spacing=1.0
    footer=s.footer.paragraphs[0];footer.alignment=WD_ALIGN_PARAGRAPH.RIGHT
    footer.add_run('RouteTec · Guía visual · 01/11/2026     |     ').font.size=Pt(8)
    field=OxmlElement('w:fldSimple');field.set(qn('w:instr'),'PAGE');footer._p.append(field)
    audit=[]
    for i,p in enumerate(pages):
        if i:doc.add_page_break()
        doc.add_paragraph(p['title'],'Title' if i==0 else 'Heading 1')
        doc.add_paragraph(p['kicker'],'Subtitle')
        png=fig(p['id']);para=doc.add_paragraph();para.paragraph_format.space_after=Pt(4)
        run=para.add_run();run.add_picture(str(png),width=Inches(10.65));run._r.xpath('.//wp:docPr')[0].set('descr',p['title']+' Controles resaltados con números que corresponden a las instrucciones de esta página.')
        for n,t in enumerate(p['steps'],1):
            para=doc.add_paragraph();para.paragraph_format.space_after=Pt(2)
            run=para.add_run(f'{n}  ');run.bold=True;run.font.color.rgb=RGBColor.from_string('886000')
            para.add_run(t)
        para=doc.add_paragraph();para.paragraph_format.space_before=Pt(4);para.paragraph_format.space_after=Pt(0)
        para.add_run('Recuerde  ').bold=True;para.add_run(p['note'])
        for run in para.runs:run.font.size=Pt(10)
        audit.append({'page':i+1,'id':p['id'],'steps':len(p['steps']),'figure':str(png.relative_to(ROOT))})
    doc.core_properties.title='Guía visual de usuario de RouteTec';doc.core_properties.subject='Manual ilustrado para principiantes';doc.core_properties.author='RouteTec'
    target=OUT/'Guía de Usuario.docx'
    if target.exists() and not (WORK/'Guía de Usuario anterior.docx').exists():shutil.copy2(target,WORK/'Guía de Usuario anterior.docx')
    doc.save(target);(WORK/'audit.json').write_text(json.dumps(audit,indent=2,ensure_ascii=False),encoding='utf-8')
    return target
if __name__=='__main__':print(build())
