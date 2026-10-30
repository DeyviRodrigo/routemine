"""Build numbered contact sheets from the real rendered document pages."""
import argparse
import re
from pathlib import Path
from PIL import Image,ImageDraw

def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
    paths=sorted(a.folder.glob('page-*.png'),key=lambda p:int(re.search(r'(\d+)$',p.stem).group(1)))
    target=a.folder/'contact';target.mkdir(exist_ok=True)
    for first in range(0,len(paths),12):
        sheet=Image.new('RGB',(1400,1500),'#dddddd');draw=ImageDraw.Draw(sheet)
        for n,path in enumerate(paths[first:first+12]):
            img=Image.open(path).convert('RGB');img.thumbnail((345,460))
            x=(n%4)*350+(350-img.width)//2;y=(n//4)*500+30
            sheet.paste(img,(x,y));draw.text(((n%4)*350+8,(n//4)*500+8),path.stem,fill='black')
        sheet.save(target/f'contact-{first//12+1:02}.jpg',quality=88)
    print(f'{len(paths)} pages in {(len(paths)+11)//12} contact sheets')

if __name__=='__main__':main()
