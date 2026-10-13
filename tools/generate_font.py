from pathlib import Path
from PIL import Image,ImageFont,ImageDraw
import re,sys
project=Path(__file__).resolve().parent.parent
source=(project/'main/ui.c').read_text(encoding='utf8')
chars=set(chr(i) for i in range(32,127))|set(chr(i) for i in range(160,256))|set(c for c in source if ord(c)>127)|set('℃周一二三四五六日容量电量充电晴间多云阴雾毛毛雨冻雪阵雷未知缓存联网校时')
font_path=sys.argv[1]
styles=[('SMALL',10,500,9,12,chars),('BODY',12,500,11,15,chars),
        ('VALUE',20,550,19,25,set('0123456789.-')),
        ('TEMP',26,500,24,31,set('0123456789.-')),
        ('CLOCK',44,500,38,45,set('0123456789:-'))]
pool=bytearray();arrays=[];counts=[]
for style,size,weight,baseline,height,alphabet in styles:
    font=ImageFont.truetype(font_path,size);font.set_variation_by_axes([weight]);glyphs=[]
    for c in sorted(alphabet,key=ord):
        l,t,r,b=font.getbbox(c,anchor='ls');w=max(0,r-l);h=max(0,b-t)
        advance=round(font.getlength(c));offset=len(pool)
        if w and h:
            im=Image.new('L',(w,h));ImageDraw.Draw(im).text((-l,-t),c,font=font,fill=255,anchor='ls')
            pixels=[min(15,(v+8)//17) for v in im.getdata()]
            pool.extend((pixels[i]<<4)|(pixels[i+1] if i+1<len(pixels) else 0) for i in range(0,len(pixels),2))
        glyphs.append(f'{{{ord(c)},{w},{h},{advance},{l},{baseline+t},{offset}}}')
    counts.append(len(glyphs))
    arrays.append(f'static const station_glyph_t font_{style.lower()}[]={{\n'+',\n'.join(glyphs)+'\n};\n')
header='''#pragma once
#include <stdint.h>
typedef enum { FONT_SMALL, FONT_BODY, FONT_VALUE, FONT_TEMP, FONT_CLOCK } station_font_t;
typedef struct { uint16_t codepoint; uint8_t width,height,advance; int8_t left,top; uint32_t offset; } station_glyph_t;
const station_glyph_t *station_glyph(unsigned codepoint, station_font_t font);
uint8_t station_glyph_alpha(const station_glyph_t *glyph, int x, int y);
'''
(project/'main/font.h').write_text(header,encoding='utf8')
body='/* Noto Sans SC bitmap subset, SIL OFL 1.1; see third_party/NotoSansSC-OFL.txt.\n * 4-bit coverage, generated at native sizes by tools/generate_font.py. */\n#include "font.h"\n#include <stddef.h>\n'
body+='static const uint8_t coverage[]={\n'+'\n'.join(','.join(f'0x{v:02x}' for v in pool[i:i+24])+',' for i in range(0,len(pool),24))+'\n};\n'
body+=''.join(arrays)
body+='static const station_glyph_t *fonts[]={'+','.join('font_'+s[0].lower() for s in styles)+'};\n'
body+='static const unsigned counts[]={'+','.join(map(str,counts))+'};\n'
body+='''const station_glyph_t *station_glyph(unsigned cp, station_font_t font) {
    if ((unsigned)font>=sizeof(counts)/sizeof(counts[0])) return NULL;
    unsigned a=0,b=counts[font]; const station_glyph_t *g=fonts[font];
    while(a<b){unsigned m=(a+b)/2;if(g[m].codepoint<cp)a=m+1;else b=m;}
    return a<counts[font]&&g[a].codepoint==cp?g+a:NULL;
}
uint8_t station_glyph_alpha(const station_glyph_t *g,int x,int y) {
    unsigned at=y*g->width+x;uint8_t v=coverage[g->offset+at/2];return at&1?v&15:v>>4;
}
'''
(project/'main/font.c').write_text(body,encoding='utf8')
print('Generated native-size 4-bit fonts:',len(pool),'coverage bytes;',sum(counts),'glyphs')
