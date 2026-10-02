"""Developer-only rasterization of the original geometric SVG icon."""
from PIL import Image,ImageDraw
from pathlib import Path
root=Path(__file__).resolve().parent.parent/'assets/icons'
scale=16
image=Image.new('RGBA',(64*scale,64*scale),(0,0,0,0))
draw=ImageDraw.Draw(image)
draw.rounded_rectangle(tuple(v*scale for v in (4,4,60,60)),radius=13*scale,fill='#2563eb')
def line(points):
    points=[(x*scale,y*scale) for x,y in points]
    draw.line(points,fill='white',width=4*scale,joint='curve')
    for x,y in points:draw.ellipse((x-2*scale,y-2*scale,x+2*scale,y+2*scale),fill='white')
line([(16,21),(48,21),(48,48),(16,48),(16,21)])
line([(13,21),(32,12),(51,21)])
line([(25,29),(32,40),(39,29)])
image.resize((256,256),Image.Resampling.LANCZOS).save(root/'vynx-arc.png')
image.resize((256,256),Image.Resampling.LANCZOS).save(root/'vynx-arc.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
