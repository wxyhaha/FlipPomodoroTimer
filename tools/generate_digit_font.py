"""Generate original antialiased seven-segment masks using Python stdlib only."""
from pathlib import Path

def horizontal(y):
    return [(6,y),(24,y),(28,y+2.5),(24,y+5),(6,y+5),(2,y+2.5)]

def vertical(x,y):
    return [(x+2.5,y),(x+5,y+3),(x+5,y+17),(x+2.5,y+20),(x,y+17),(x,y+3)]

def inside(x,y,polygon):
    signs=[(bx-ax)*(y-ay)-(by-ay)*(x-ax)
           for (ax,ay),(bx,by) in zip(polygon,polygon[1:]+polygon[:1])]
    return all(v>=0 for v in signs) or all(v<=0 for v in signs)

segments=[horizontal(0),vertical(25,5),vertical(25,25),horizontal(45),
          vertical(0,25),vertical(0,5),horizontal(22.5)]
masks=[0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F]
# Render each final pixel size directly: no low-resolution source enlargement.
sizes=[(64,104),(70,116),(24,40),(23,39)]
lines=['// Generated native-size glyphs; tools/generate_digit_font.py; original artwork.',
       '#pragma once','#include <stdint.h>']
for width,height in sizes:
    coverage=[]
    for poly in segments:
        poly=[(round(x*width/30),round(y*height/50)) for x,y in poly]
        bits=[0]*(width*height)
        left=max(0,min(x for x,y in poly));right=min(width,max(x for x,y in poly)+1)
        top=max(0,min(y for x,y in poly));bottom=min(height,max(y for x,y in poly)+1)
        for y in range(top,bottom):
            for x in range(left,right):
                value=0
                for sy in range(4):
                    for sx in range(4):
                        if inside(x+(sx+.5)/4,y+(sy+.5)/4,poly):value|=1<<(sy*4+sx)
                bits[y*width+x]=value
        coverage.append(bits)
    lines.append(f'static const uint8_t DIGITS_{width}_{height}[10][{width*height}] = {{')
    for mask in masks:
        values=[]
        for pos in range(width*height):
            bits=0
            for i in range(7):
                if mask & (1<<i):bits|=coverage[i][pos]
            values.append((bits.bit_count()*255+8)//16)
        lines.append('  {')
        lines.extend('    '+','.join(map(str,values[i:i+width]))+',' for i in range(0,len(values),width))
        lines.append('  },')
    lines.append('};')
target=Path(__file__).resolve().parents[1]/'firmware/FlipPomodoroTimer/DigitFont.h'
target.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
print('Generated native sizes:',sizes)
