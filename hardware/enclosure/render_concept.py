"""Render enclosure concept views; this illustration is not a manufacturing drawing."""
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import numpy as np

HERE = Path(__file__).resolve().parent
FONT = FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
BG = '#f3f5f6'
INK = '#20323e'


def box(ax, x, y, z, w, d, h, color, edge='#8c969b'):
    v = np.array([[x,y,z],[x+w,y,z],[x+w,y+d,z],[x,y+d,z],
                  [x,y,z+h],[x+w,y,z+h],[x+w,y+d,z+h],[x,y+d,z+h]])
    faces = [[v[i] for i in indices] for indices in
             [[0,1,2,3],[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7],[4,5,6,7]]]
    rgb = matplotlib.colors.to_rgb(color)
    colors = [tuple(c * factor for c in rgb) for factor in [.72,.91,.80,.85,.88,1]]
    if not hasattr(ax, '_concept_faces'):
        ax._concept_faces, ax._concept_colors = [], []
    ax._concept_faces.extend(faces)
    ax._concept_colors.extend(colors)


def body(ax, cut=False):
    ivory = '#e5e6df'
    box(ax,0,0,-2,86.5,90.5,2,ivory)
    if not cut: box(ax,0,0,86.5,86.5,90.5,2,ivory)
    box(ax,0,14.9,0,2,75.6,86.5,ivory)
    if not cut: box(ax,84.5,14.9,0,2,75.6,86.5,ivory)
    box(ax,0,-2,0,86.5,2,1.8,ivory)
    box(ax,0,-2,84.7,86.5,2,1.8,ivory)
    for z in [0,84.5]: box(ax,0,14.9,z,86.5,2,2,ivory)
    for z in [-2,86.5]: box(ax,86.5,0,z,2,16.9,2,'#dfa95a')
    if not cut: box(ax,0,90.5,-2,86.5,2,90.5,ivory)


def panel(ax):
    box(ax,0,0,0,86.5,14.5,86.5,'#d1d5d6')
    box(ax,.5,-.16,.5,85.5,.12,85.5,'#191e23',edge='#303840')
    box(ax,7.285,-.3,7.285,71.93,.12,71.93,'#263540',edge='#374b59')
    # Approximate locations only, deliberately not dimensions for fabrication.
    for z in [62,72]: box(ax,86.5,5,z,1.0,3.2,4.8,'#b8bdc0')
    for z in [25,40]: box(ax,86.52,4.7,z,.15,4.0,8.0,'#202c34')


def battery(ax):
    for x in [34.25,52.25]:
        angles=np.linspace(0,2*np.pi,40)
        bottom=np.array([[x+9.2*np.cos(t),48+9.2*np.sin(t),10] for t in angles])
        top=bottom+np.array([0,0,66.5])
        faces=[bottom,top]+[[bottom[i],bottom[(i+1)%40],top[(i+1)%40],top[i]] for i in range(40)]
        ax._concept_faces.extend(faces)
        ax._concept_colors.extend(['#5e8fba'] * len(faces))


def setup(ax,elev,azim):
    ax.set_xlim(-4,94);ax.set_ylim(-7,102);ax.set_zlim(-6,94)
    ax.set_box_aspect((98,109,100));ax.view_init(elev=elev,azim=azim)
    ax.set_axis_off();ax.set_facecolor(BG)
    ax.set_proj_type('ortho')


def text(fig,x,y,s,size=12,color=INK,weight='normal'):
    fig.text(x,y,s,fontproperties=FONT,fontsize=size,color=color,weight=weight)


fig=plt.figure(figsize=(16,10),dpi=140,facecolor=BG)
text(fig,.045,.94,'FlipPomodoroTimer  /  外壳结构草案 V0',23,weight='bold')
text(fig,.045,.902,'左右侧边开放 · 面板整体侧插 · 后置电池仓 · 近正方体轮廓',13)

ax=fig.add_axes([-.02,.18,.60,.69],projection='3d')
setup(ax,22,-56);body(ax);panel(ax)
text(fig,.045,.835,'01  装配外观',15,weight='bold')
text(fig,.045,.19,'暖黄色：右侧上下护脚，向外突出 2mm',12,color='#996522')
text(fig,.045,.16,'中间开放段直接露出原面板侧边，按钮和 USB 可用',12)

ax2=fig.add_axes([.54,.45,.42,.40],projection='3d')
setup(ax2,25,52);body(ax2,cut=True);panel(ax2);battery(ax2)
text(fig,.60,.835,'02  后仓剖开示意',15,weight='bold')
text(fig,.60,.46,'移除后盖、顶板和右后壁，仅为展示内部',11,color='#687780')
text(fig,.60,.43,'蓝色：双节 18650 占位，尚未设计固定件',11,color='#45749b')

ax3=fig.add_axes([.595,.145,.34,.235],facecolor=BG)
from matplotlib.patches import Rectangle, FancyArrowPatch
ax3.add_patch(Rectangle((0,-2),90.5,90.5,facecolor='#dfe3de',edgecolor='#74818b'))
ax3.add_patch(Rectangle((-.2,0),14.7,86.5,facecolor='#c7ced2',edgecolor='#6a7a85'))
ax3.add_patch(Rectangle((2,0),10.5,86.5,facecolor=BG,edgecolor='none'))
ax3.add_patch(Rectangle((0,25),14.5,8,facecolor='#536775',edgecolor='none'))
ax3.add_patch(Rectangle((0,40),14.5,8,facecolor='#536775',edgecolor='none'))
for z in [62,72]: ax3.add_patch(Rectangle((5,z),3.2,4.8,facecolor='#aeb9bf'))
for z in [-2,86.5]: ax3.add_patch(Rectangle((0,z),16.9,2,facecolor='#dfa95a'))
ax3.text(43,43,'后部电池仓',fontproperties=FONT,fontsize=11,color=INK)
ax3.text(-2,94,'前',fontproperties=FONT,color=INK)
ax3.text(82,94,'后',fontproperties=FONT,color=INK)
ax3.set_xlim(-5,99);ax3.set_ylim(-6,101);ax3.set_aspect('equal');ax3.axis('off')
text(fig,.60,.39,'03  右侧：前段开放，后段为仓壁',13,weight='bold')

text(fig,.045,.095,'面板基准 86.5 × 86.5 × 14.5mm   |   外壳主体约 86.5 × 90.5 × 90.5mm（宽 × 深 × 高）',12)
text(fig,.045,.062,'含护脚/前后薄板包络约 88.5 × 94.5 × 90.5mm；为了左右外露，暂不强求严格正方体。',11,color='#687780')
text(fig,.045,.03,'结构讨论稿：未实测电池、未完成锁定件/后盖连接/声孔；不可直接按本图下单打印。',11,color='#986238')
for view in [ax, ax2]:
    view.add_collection3d(Poly3DCollection(view._concept_faces, facecolors=view._concept_colors, edgecolors='#919b9f', linewidths=.35, zsort='average'))
fig.savefig(HERE/'enclosure-v0.png',facecolor=BG)
print(HERE/'enclosure-v0.png')
