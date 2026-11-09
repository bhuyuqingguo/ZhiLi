#!/usr/bin/env python3
"""Deterministically generate ZhiLi's original, illustrative SVG symbol set.

No downloaded artwork, external fonts, image links, or operational specifications.
Run from any directory with Python 3.8+. The Qt build uses committed output only.
"""
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'symbols'
CATEGORIES = [
    ('air', '空中平台', '#226384', '#e5f3fa', ['无人机', '空中', '飞行', 'UAV', 'aerial', 'air']),
    ('surface', '水面平台', '#196c83', '#e3f4f8', ['无人船', '水面', '船舶', 'USV', 'surface', 'boat']),
    ('ground', '地面平台', '#6a6334', '#f5f1dc', ['机器人', '无人车', '地面', 'UGV', 'ground', 'robot']),
    ('underwater', '水下平台', '#355f93', '#e9effb', ['水下', '海洋', '潜水', 'UUV', 'underwater', 'ocean']),
    ('sensing', '感知与测量', '#806039', '#fcf0df', ['传感器', '感知', '测量', 'sensor', 'sensing']),
    ('command', '指挥与协同', '#675393', '#f0eafa', ['指控', '指挥', '控制', '协同', 'command', 'control']),
    ('communications', '通信与网络', '#386b69', '#e4f3ef', ['通讯', '通信', '网络', 'communication', 'network']),
    ('support', '保障与设施', '#6c6474', '#f0edf3', ['保障', '设施', '基础设施', 'support', 'infrastructure']),
]

def p(d, fill='none', **attrs):
    return element('path', d=d, fill=fill, **attrs)

def element(tag, **attrs):
    attrs = ' '.join('%s="%s"' % (k.replace('_', '-'), html.escape(str(v), quote=True)) for k, v in attrs.items())
    return '<%s %s/>' % (tag, attrs)

def l(x1, y1, x2, y2): return element('line', x1=x1, y1=y1, x2=x2, y2=y2)
def r(x, y, w, h, fill='none', rx=2): return element('rect', x=x, y=y, width=w, height=h, rx=rx, fill=fill)
def c(x, y, radius, fill='none'): return element('circle', cx=x, cy=y, r=radius, fill=fill)
def e(x, y, rx, ry, fill='none'): return element('ellipse', cx=x, cy=y, rx=rx, ry=ry, fill=fill)
def group(*parts, transform=None): return '<g%s>%s</g>' % ((' transform="%s"' % transform) if transform else '', ''.join(parts))
F = '#FILL#'
assets = []

def add(cat, slug, name, english, art, *keywords):
    meta = next(v for v in CATEGORIES if v[0] == cat)
    ident = cat + '-' + slug
    assets.append({'id': ident, 'name': name, 'englishName': english, 'categoryId': cat,
                   'categoryName': meta[1], 'keywords': list(keywords),
                   'resourcePath': ':/symbols/%s/%s.svg' % (cat, ident),
                   'file': '%s/%s.svg' % (cat, ident), 'art': art})

# Air: recognizable platform silhouettes rather than color-only variations.
def rotor(x, y, size=6): return c(x, y, size, F) + l(x-size+2,y,x+size-2,y) + l(x,y-size+2,x,y+size-2)
def drone(positions, body=None):
    return ''.join(l(32,32,x,y) for x,y in positions) + ''.join(rotor(x,y) for x,y in positions) + (body or r(26,25,12,15,F,4))
add('air','quadcopter','四旋翼平台','Quadcopter',drone([(16,16),(48,16),(16,48),(48,48)]),'多旋翼','drone')
add('air','hexacopter','六旋翼平台','Hexacopter',drone([(16,14),(48,14),(10,32),(54,32),(16,50),(48,50)]),'多旋翼')
add('air','octocopter','八旋翼平台','Octocopter',drone([(17,12),(47,12),(10,26),(54,26),(10,40),(54,40),(21,53),(43,53)]),'多旋翼')
add('air','fixed-wing','固定翼平台','Fixed-wing aircraft',p('M32 7 L36 24 55 35 55 40 36 35 35 47 42 52 42 56 32 52 22 56 22 52 29 47 28 35 9 40 9 35 28 24Z',F),'飞机')
add('air','flying-wing','飞翼平台','Flying wing',p('M32 13 L57 46 42 44 32 36 22 44 7 46Z',F)+l(32,20,32,36),'翼身融合')
add('air','tiltrotor','倾转旋翼平台','Tiltrotor',p('M30 10 L34 10 37 34 48 43 48 48 34 43 32 53 30 43 16 48 16 43 27 34Z',F)+l(14,27,50,27)+rotor(14,27,7)+rotor(50,27,7),'垂直起降','VTOL')
add('air','helicopter','旋翼直升平台','Helicopter',p('M13 33 Q14 24 29 24 L41 30 53 22 57 22 53 38 41 38 Q36 46 17 42Z',F)+l(28,24,28,16)+l(9,16,49,16)+p('M18 44 L16 50 40 50')+l(34,43,36,50)+p('M16 32 L28 32 28 26'),'直升机')
add('air','airship','飞艇平台','Airship',e(30,28,23,13,F)+p('M49 25 L56 17 56 28 50 32 56 39 49 38')+r(24,41,14,7,F)+l(13,28,44,28),'轻于空气')
add('air','tethered-balloon','系留气球','Tethered balloon',e(32,22,16,13,F)+p('M22 33 L27 40 37 40 42 33')+p('M32 40 Q25 48 35 56')+l(27,56,43,56),'气球')
add('air','solar-wing','太阳能长翼平台','Solar aircraft',p('M30 10 L34 10 35 27 57 29 57 35 35 35 34 48 42 50 42 54 22 54 22 50 30 48 29 35 7 35 7 29 29 27Z',F)+''.join(l(x,29,x,35) for x in (13,19,25,39,45,51)),'新能源')
add('air','cargo-drone','货运无人机','Cargo drone',drone([(15,16),(49,16),(15,44),(49,44)],r(24,22,16,14,F))+r(24,40,16,15,F)+l(32,40,32,46)+l(26,35,26,40)+l(38,35,38,40),'运输','包裹','cargo')
add('air','camera-drone','航拍无人机','Camera drone',drone([(14,17),(50,17),(14,44),(50,44)])+r(26,43,12,11,F)+c(32,48,3),'相机','camera')
add('air','survey-drone','测绘无人机','Survey drone',p('M32 9 L37 28 55 32 55 38 36 36 35 46 42 50 32 53 22 50 29 46 28 36 9 38 9 32 27 28Z',F)+c(32,32,4)+p('M19 49 L14 55 M45 49 L50 55'),'测绘','survey')
add('air','glider','滑翔平台','Glider',p('M32 8 L34 25 57 29 57 33 34 32 33 48 43 50 43 54 32 52 21 54 21 50 31 48 30 32 7 33 7 29 30 25Z',F),'滑翔机')
add('air','parachute','伞降载荷','Parachute payload',p('M9 28 A23 21 0 0 1 55 28 Q43 23 32 28 Q20 23 9 28Z',F)+p('M21 26 Q21 13 32 7 Q43 13 43 26 M9 28 L26 46 M55 28 L38 46 M32 28 L32 46')+r(25,46,14,10,F),'降落伞')
add('air','drone-dock','无人机机巢','Drone dock',p('M10 30 L32 21 54 30 54 54 10 54Z',F)+r(19,34,26,14)+l(23,41,41,41)+drone([(24,9),(40,9)],r(28,9,8,9,F)),'机巢','停机坪')

# Surface: side/front silhouettes, buoys and shore interfaces.
def hull(extra='', shape='M8 38 L56 38 48 50 19 50Z'):
    return p(shape,F)+extra+p('M7 56 Q12 52 17 56 T27 56 T37 56 T47 56 T57 56')
add('surface','research-boat','科考船','Research vessel',hull(r(19,24,23,14,F)+r(25,15,12,9,F)+l(31,15,31,8)+l(31,10,43,10)+l(46,38,46,24)+l(46,24,55,30)),'科考','research')
add('surface','catamaran','双体无人船','Catamaran',p('M12 10 Q6 26 12 52 L22 52 22 15Z',F)+p('M42 15 L42 52 52 52 Q58 26 52 10Z',F)+r(20,23,24,15,F)+r(26,26,12,9)+l(32,23,32,14),'双体')
add('surface','trimaran','三体平台','Trimaran',p('M32 7 Q24 19 25 52 L39 52 Q40 19 32 7Z',F)+p('M10 23 L8 48 17 48 16 23Z',F)+p('M48 23 L47 48 56 48 54 23Z',F)+l(16,31,48,31)+l(16,43,48,43),'三体')
add('surface','sailboat','无人帆船','Sailboat',hull(p('M30 9 L30 36 12 36Z',F)+p('M35 13 L49 33 35 33Z',F)+l(32,7,32,38)),'帆船')
add('surface','inflatable-boat','充气艇','Inflatable boat',e(32,35,24,12,F)+e(30,33,15,6)+r(48,26,8,10,F)+l(23,25,15,13)+p('M11 10 L17 8 23 17 17 21Z',F),'橡皮艇')
add('surface','ferry','渡运平台','Ferry',hull(r(13,23,38,15,F)+r(20,14,24,9,F)+l(24,27,24,33)+l(32,27,32,33)+l(40,27,40,33)),'渡船')
add('surface','cargo-vessel','运输船','Cargo vessel',hull(r(10,22,12,16,F)+r(26,26,12,12,F)+r(38,26,12,12,F)+r(26,15,12,11,F)+l(15,22,15,14)),'运输','cargo')
add('surface','tugboat','拖带船','Tugboat',hull(r(20,25,20,13,F)+r(24,16,12,9,F)+c(16,44,4)+c(44,44,4)+p('M43 35 Q49 25 55 31')),'拖船')
add('surface','lifeboat','救援艇','Rescue boat',hull(p('M17 38 L24 24 38 24 45 38Z',F)+c(32,31,5)+l(32,27,32,35)+l(28,31,36,31)),'救援','rescue')
add('surface','buoy','航标浮标','Navigation buoy',hull(p('M22 38 L26 18 38 18 42 38Z',F)+r(27,10,10,8,F)+l(32,6,32,10), 'M17 38 L47 38 43 47 21 47Z'),'浮标')
add('surface','autonomous-buoy','观测浮标','Observation buoy',hull(e(32,40,20,7,F)+l(32,38,32,10)+p('M14 25 L29 20 29 29 14 33Z',F)+p('M35 20 L50 25 50 33 35 29Z',F)+c(32,8,2)),'观测')
add('surface','wave-glider','波浪滑翔平台','Wave glider',e(32,16,23,6,F)+l(32,22,32,41)+p('M10 41 L54 41 44 48 20 48Z',F)+l(19,41,19,49)+l(29,41,29,49)+l(39,41,39,49)+p('M7 29 Q12 25 17 29 T27 29 T37 29 T47 29 T57 29'),'波浪')
add('surface','surface-drone','小型无人艇','Surface drone',p('M32 7 Q15 20 18 49 L46 49 Q49 20 32 7Z',F)+r(25,26,14,15)+l(32,25,32,15)+c(32,13,2)+p('M23 54 L23 58 M41 54 L41 58'),'小艇')
add('surface','pontoon','浮动平台','Floating platform',p('M10 28 L32 17 54 28 32 39Z',F)+p('M10 28 L10 40 32 51 54 40 54 28 M32 39 L32 51')+l(18,24,40,35)+l(26,20,48,31),'浮台')
add('surface','dock','码头接口','Dock',r(8,23,48,9,F)+l(14,32,14,53)+l(50,32,50,53)+l(25,32,25,44)+l(39,32,39,44)+p('M7 51 Q12 47 17 51 T27 51 T37 51 T47 51 T57 51')+l(19,15,19,23)+l(45,15,45,23),'码头','岸基')
add('surface','lighthouse','岸基灯塔','Lighthouse',p('M24 25 L40 25 46 55 18 55Z',F)+r(23,14,18,11,F)+p('M21 14 L32 6 43 14Z',F)+l(25,35,41,35)+l(23,45,43,45)+l(8,19,17,19)+l(47,19,56,19),'灯塔')

# Ground: generic non-weaponized mobile systems.
def wheels(xs,y=46,radius=5): return ''.join(c(x,y,radius,F) for x in xs)
add('ground','four-wheel-rover','四轮无人车','Four-wheel rover',r(12,26,40,17,F)+wheels([19,45])+r(25,17,15,9,F)+l(32,17,32,10),'轮式','rover')
add('ground','six-wheel-rover','六轮巡视车','Six-wheel rover',p('M10 30 L19 24 43 24 54 30 54 42 10 42Z',F)+wheels([15,32,49],47)+l(32,24,32,13)+r(27,8,13,7,F),'巡视')
add('ground','tracked-rover','履带移动平台','Tracked rover',r(8,36,48,18,F,9)+c(18,45,5)+c(32,45,5)+c(46,45,5)+r(17,22,30,14,F)+l(32,22,32,13)+c(32,10,3),'履带')
add('ground','quadruped','四足机器人','Quadruped robot',r(17,21,28,13,F)+p('M19 31 L12 42 17 54 M27 34 L24 45 29 54 M37 34 L40 44 36 54 M43 31 L52 42 47 54')+p('M45 23 L52 19 56 23 52 30 45 29',F),'机器狗','四足')
add('ground','biped','双足机器人','Biped robot',r(24,8,16,12,F)+r(23,24,18,15,F)+p('M24 39 L19 53 12 53 M40 39 L45 53 52 53 M23 26 L13 35 10 30 M41 26 L49 34 54 29')+l(28,14,36,14),'双足','人形')
add('ground','mobile-manipulator','移动机械臂','Mobile manipulator',r(12,40,40,11,F)+wheels([19,45],52,4)+p('M25 40 L25 28 38 18 46 27')+c(25,28,4,F)+c(38,18,4,F)+p('M43 25 L45 32 52 30 M49 26 L53 21'),'机械臂')
add('ground','agv','搬运机器人','Automated guided vehicle',r(9,34,46,17,F,5)+wheels([18,46],51,4)+r(21,16,22,18,F)+l(32,16,32,24)+c(47,41,2),'AGV','自动导引')
add('ground','forklift','无人叉车','Autonomous forklift',p('M10 30 L34 30 34 47 10 47Z',F)+wheels([17,34],49,5)+p('M16 30 L16 14 33 14 33 30 M42 10 L42 46 56 46 M47 10 L47 40 56 40')+l(24,16,24,29),'叉车')
add('ground','delivery-robot','配送机器人','Delivery robot',p('M17 22 Q32 11 47 22 L49 43 15 43Z',F)+wheels([19,45],48)+l(18,30,46,30)+c(27,24,2)+c(37,24,2)+l(47,21,50,10),'配送')
add('ground','rover-camera','巡检移动平台','Inspection rover',r(16,34,32,12,F)+wheels([20,44],49)+l(32,34,32,20)+r(23,10,23,13,F)+c(38,16,4),'巡检','相机')
add('ground','inspection-crawler','管道检测爬行器','Pipe inspection crawler',p('M9 17 Q32 7 55 17 M9 51 Q32 61 55 51')+r(16,25,30,16,F)+wheels([20,42],44,4)+p('M46 28 L55 23 55 41 46 37Z',F)+l(16,31,7,31),'管道')
add('ground','wall-climber','壁面爬行机器人','Wall-climbing robot',r(22,20,20,23,F,5)+p('M23 24 L13 14 M41 24 L51 14 M23 39 L13 50 M41 39 L51 50')+''.join(c(x,y,6,F) for x,y in [(12,12),(52,12),(12,52),(52,52)])+c(32,31,4),'壁面','攀爬')
add('ground','warehouse-cart','智能仓储小车','Warehouse cart',r(12,26,38,21,F)+r(18,13,12,13,F)+r(31,13,12,13,F)+wheels([20,42],51,4)+p('M50 32 L56 32 56 19'),'仓储')
add('ground','field-tractor','田间作业平台','Field robot',p('M11 31 L24 31 24 16 39 16 44 30 53 30 53 44 11 44Z',F)+c(21,46,9,F)+c(47,48,6,F)+p('M29 20 L35 20 39 29 29 29Z')+l(10,55,55,55),'农用','农业')
add('ground','road-sweeper','清扫机器人','Cleaning robot',r(15,23,34,24,F,6)+p('M20 23 L23 14 41 14 44 23Z',F)+wheels([20,44],48,4)+l(16,43,8,51)+l(48,43,56,51)+p('M7 47 L13 53 M51 53 L57 47'),'清扫')
add('ground','rail-inspector','轨道巡检车','Rail inspection platform',p('M11 56 L22 8 M53 56 L42 8 M17 45 L47 45 M20 33 L44 33 M23 21 L41 21')+r(17,27,30,15,F)+c(21,43,4,F)+c(43,43,4,F)+l(32,27,32,13)+r(27,9,10,7,F),'轨道')

# Underwater: non-operational concepts for architecture diagrams.
add('underwater','auv','自主水下平台','Autonomous underwater vehicle',p('M13 24 L44 24 Q58 32 44 40 L13 40 Q7 32 13 24Z',F)+p('M16 24 L20 15 25 24 M16 40 L20 49 25 40 M10 28 L6 23 M10 36 L6 41')+c(44,32,3),'AUV','自主')
add('underwater','rov','缆控水下机器人','Remotely operated vehicle',r(11,23,42,24,F)+r(19,28,26,13)+c(15,35,3)+c(49,35,3)+p('M29 23 L29 16 Q29 7 45 9')+p('M23 47 L20 54 M41 47 L44 54'),'ROV','缆控')
add('underwater','glider','水下滑翔机','Underwater glider',p('M32 8 Q38 13 36 48 L32 55 28 48 Q26 13 32 8Z',F)+p('M28 25 L7 39 28 35 M36 25 L57 39 36 35',F)+l(29,45,21,50)+l(35,45,43,50),'滑翔')
add('underwater','robotic-fish','仿生机器鱼','Robotic fish',p('M11 32 Q28 10 46 28 L57 20 54 32 57 44 46 36 Q28 54 11 32Z',F)+c(22,29,2)+p('M31 22 L37 15 42 25 M31 42 L37 49 42 39 M36 25 Q31 32 36 39'),'仿生')
add('underwater','submersible','观测潜器','Observation submersible',e(31,34,23,13,F)+r(23,14,15,8,F)+l(30,14,30,8)+l(30,8,38,8)+c(22,34,5)+c(38,34,5)+p('M53 28 L58 24 58 44 53 40'),'潜器')
add('underwater','crawler','海底爬行平台','Seabed crawler',r(11,34,42,17,F,8)+c(21,43,5)+c(42,43,5)+r(20,20,24,14,F)+l(32,20,32,13)+p('M8 57 L19 55 31 57 43 55 56 57'),'海底')
add('underwater','manipulator','水下作业机械臂','Underwater manipulator',r(15,48,26,8,F)+p('M28 48 L20 33 36 18 46 29')+c(20,33,5,F)+c(36,18,5,F)+p('M43 26 L43 35 52 35 M49 26 L55 21')+c(12,14,3)+c(8,24,2),'机械臂')
add('underwater','camera','水下摄像机','Underwater camera',r(10,20,40,28,F,6)+c(31,34,10)+c(31,34,5)+p('M50 27 L57 23 57 45 50 41Z',F)+l(16,25,19,25)+c(16,12,3)+c(23,7,2),'相机')
add('underwater','acoustic-node','水声观测节点','Acoustic observation node',r(24,26,16,18,F)+l(32,44,32,55)+l(20,55,44,55)+p('M18 25 Q9 34 18 44 M46 25 Q55 34 46 44 M23 18 Q32 11 41 18')+c(32,33,3),'声学')
add('underwater','seabed-station','海底观测站','Seabed station',p('M19 26 L45 26 51 49 13 49Z',F)+r(25,32,14,10)+l(24,26,24,15)+l(40,26,40,10)+c(24,12,3)+p('M7 57 L19 54 32 57 45 54 57 57'),'观测站')
add('underwater','ocean-profiler','剖面观测浮体','Ocean profiler',r(24,17,16,36,F,8)+l(32,17,32,8)+l(24,30,40,30)+l(24,42,40,42)+p('M12 19 L12 46 M8 24 L12 19 16 24 M48 46 L48 19 M44 41 L48 46 52 41'),'剖面','浮体')
add('underwater','sampling-rosette','水体采样器','Water sampling rosette',e(32,18,18,7,F)+e(32,48,18,7,F)+r(14,20,9,27,F)+r(28,22,9,27,F)+r(42,20,9,27,F)+l(32,11,32,6),'采样','采水')
add('underwater','docking-station','水下对接站','Underwater dock',p('M11 14 L53 14 53 51 11 51Z',F)+p('M19 22 L45 22 45 43 19 43Z')+p('M24 32 L40 32 M35 27 L40 32 35 37')+l(17,51,13,57)+l(47,51,51,57),'对接')
add('underwater','tether-reel','水下缆绳卷盘','Tether reel',c(30,31,21,F)+c(30,31,14)+c(30,31,6)+l(30,10,30,17)+l(30,45,30,52)+l(9,31,16,31)+l(44,31,51,31)+p('M51 31 L56 31 56 53'),'缆绳','卷盘')
add('underwater','diver','潜水作业人员','Diver',c(41,15,7,F)+r(37,12,10,5,F)+p('M34 25 L20 33 29 44 38 34Z',F)+p('M31 25 L24 20 17 23 M37 32 L47 36 55 32 M25 40 L18 48 7 49 11 54 22 54 31 45 M29 44 L37 51 48 51 50 56 36 56 26 49')+c(54,12,2),'潜水员')
add('underwater','observatory','水下观测舱','Underwater observatory',p('M12 30 Q12 13 32 13 Q52 13 52 30 L52 48 12 48Z',F)+c(23,31,6)+c(41,31,6)+l(19,48,15,56)+l(45,48,49,56)+l(32,13,32,6),'观测舱')

# Sensing and measurement: function symbols, not detection parameters.
add('sensing','camera','光学相机','Optical camera',r(8,21,48,31,F,5)+p('M20 21 L24 13 40 13 44 21Z',F)+c(32,36,11)+c(32,36,6)+l(14,27,18,27),'摄像头','光学','camera')
add('sensing','stereo-camera','双目相机','Stereo camera',r(7,23,50,24,F,5)+c(20,35,8)+c(44,35,8)+l(27,18,37,18)+l(32,18,32,23),'双目','stereo')
add('sensing','thermal-camera','热成像相机','Thermal camera',r(8,20,35,28,F)+c(25,34,9)+p('M43 27 L55 21 55 47 43 41Z',F)+p('M19 16 Q15 12 19 8 M27 16 Q23 12 27 8 M35 16 Q31 12 35 8'),'红外','热成像','infrared')
add('sensing','panoramic-camera','全景相机','Panoramic camera',c(32,30,17,F)+c(25,30,6)+c(43,30,3)+l(32,47,32,54)+p('M12 17 Q6 30 12 43 M52 17 Q58 30 52 43')+l(24,54,40,54),'全景')
add('sensing','lidar','激光测距仪','Lidar',r(20,25,24,25,F,4)+e(32,25,12,6,F)+l(22,39,42,39)+p('M12 21 L6 16 M18 15 L15 8 M46 15 L49 8 M52 21 L58 16')+c(32,42,3),'激光','雷达','lidar')
add('sensing','radar','扫描感知设备','Scanning sensor',p('M14 13 Q44 11 51 42 Q20 44 14 13Z',F)+l(19,18,40,38)+l(32,39,32,52)+l(20,55,44,55)+p('M40 9 Q53 12 57 25'),'扫描','雷达','radar')
add('sensing','sonar','声学探测器','Sonar',r(10,23,15,21,F)+c(18,33,4)+p('M32 25 Q41 33 32 41 M40 17 Q56 33 40 49 M48 9 Q70 33 48 57'),'声呐','sonar')
add('sensing','hydrophone','水听器','Hydrophone',r(25,24,14,24,F,7)+l(32,24,32,7)+p('M18 27 Q11 36 18 45 M46 27 Q53 36 46 45')+l(28,35,36,35)+l(28,41,36,41),'水听','hydrophone')
add('sensing','weather-station','气象站','Weather station',l(32,13,32,56)+l(18,56,46,56)+l(16,21,48,21)+c(15,21,6,F)+c(49,21,6,F)+r(25,33,14,14,F)+p('M32 13 L47 8 43 16Z',F),'气象','天气')
add('sensing','thermometer','温度传感器','Temperature sensor',p('M26 38 L26 13 A6 6 0 0 1 38 13 L38 38 A12 12 0 1 1 26 38Z',F)+l(32,20,32,44)+c(32,47,4)+l(43,17,50,17)+l(43,27,48,27),'温度')
add('sensing','pressure','压力传感器','Pressure sensor',c(32,29,21,F)+p('M17 29 A15 15 0 0 1 47 29')+l(32,29,41,20)+c(32,29,3)+r(26,50,12,8,F)+l(32,12,32,16),'压力')
add('sensing','humidity','湿度传感器','Humidity sensor',p('M32 7 Q14 28 14 39 A18 18 0 0 0 50 39 Q50 28 32 7Z',F)+c(26,32,4)+c(38,44,4)+l(24,46,40,30),'湿度')
add('sensing','gas','气体传感器','Gas sensor',r(18,26,28,28,F)+l(24,33,40,33)+l(24,39,40,39)+c(32,47,3)+c(18,15,6,F)+c(35,11,4,F)+c(47,18,5,F),'气体')
add('sensing','soil','土壤传感器','Soil sensor',r(24,8,16,17,F)+p('M28 25 L28 49 M36 25 L36 49 M8 35 Q20 29 32 35 T56 35')+l(10,45,17,45)+l(45,43,53,43)+l(14,54,22,54)+l(41,53,49,53),'土壤')
add('sensing','water-quality','水质传感器','Water quality sensor',r(26,8,12,34,F,5)+l(29,16,35,16)+p('M8 43 Q14 37 20 43 T32 43 T44 43 T56 43 M8 54 Q14 48 20 54 T32 54 T44 54 T56 54'),'水质')
add('sensing','positioning','定位接收器','Position receiver',p('M32 54 Q12 31 16 20 A17 17 0 0 1 48 20 Q52 31 32 54Z',F)+c(32,23,7)+p('M8 16 Q13 7 22 5 M42 5 Q51 7 56 16'),'定位','GNSS','GPS')

# Command and collaboration: generic human-readable diagram concepts.
def screen(content=''): return r(8,12,48,33,F)+content+l(32,45,32,54)+l(22,54,42,54)
add('command','control-center','控制中心','Control center',p('M8 25 L32 9 56 25 56 55 8 55Z',F)+r(15,29,14,10)+r(35,29,14,10)+r(25,44,14,11)+l(32,9,32,5),'中心','控制室')
add('command','workstation','操作工作站','Operator workstation',screen(r(15,19,18,18)+l(39,21,49,21)+l(39,28,49,28)+l(39,35,45,35)),'工作站')
add('command','tablet-console','平板控制终端','Tablet console',r(10,10,44,45,F,5)+r(16,16,32,28)+c(32,50,2)+p('M22 31 L29 24 35 34 42 27'),'平板')
add('command','handheld-controller','手持控制器','Handheld controller',p('M18 22 L46 22 Q53 22 56 46 Q55 55 47 50 L39 42 25 42 17 50 Q9 55 8 46 Q11 22 18 22Z',F)+p('M17 32 L27 32 M22 27 L22 37')+c(43,29,2)+c(49,35,2)+l(16,22,13,10)+l(48,22,51,10),'遥控器')
add('command','joystick','操纵杆','Joystick',p('M15 44 L49 44 55 55 9 55Z',F)+l(32,44,32,23)+e(32,16,9,10,F)+c(43,48,2),'操纵')
add('command','dashboard','状态仪表板','Status dashboard',screen(p('M15 34 A12 12 0 0 1 39 34')+l(27,34,33,25)+l(44,21,49,21)+l(44,28,49,28)+l(44,35,49,35)),'状态','仪表')
add('command','operations-room','协同会商室','Coordination room',r(9,8,46,18,F)+l(24,8,24,26)+l(40,8,40,26)+e(32,43,21,8,F)+c(12,33,4)+c(32,32,4)+c(52,33,4)+l(20,49,17,56)+l(44,49,47,56),'会商','会议')
add('command','planning-board','方案规划板','Planning board',r(9,9,46,36,F)+l(18,45,13,56)+l(46,45,51,56)+p('M17 18 L29 18 29 29 45 29 M40 24 L45 29 40 34')+r(14,32,10,7),'规划','方案')
add('command','task-board','任务看板','Task board',r(7,11,50,43,F)+l(23,11,23,54)+l(41,11,41,54)+l(7,21,57,21)+r(11,27,8,8,F)+r(27,27,10,8,F)+r(27,40,10,8,F)+r(45,27,8,8,F),'看板','任务')
add('command','checklist','检查清单','Checklist',r(15,12,34,44,F)+r(24,7,16,10,F)+p('M22 28 L25 31 30 25 M22 40 L25 43 30 37')+l(35,28,42,28)+l(35,40,42,40)+l(22,49,42,49),'检查','清单')
add('command','workflow','活动流程','Workflow',r(23,6,18,11,F)+p('M32 17 L32 24 M13 32 L13 45 M51 32 L51 45 M32 24 L51 24 51 28 M32 24 L13 24 13 28')+r(6,44,15,12,F)+r(43,44,15,12,F)+c(13,30,3)+c(51,30,3),'流程','活动')
add('command','decision-node','决策分支','Decision node',p('M32 7 L47 22 32 37 17 22Z',F)+p('M17 22 L8 22 8 49 20 49 M47 22 L56 22 56 49 44 49')+r(20,43,24,13,F)+p('M27 21 Q27 14 34 16 Q40 19 32 24')+c(32,29,0.7),'决策','分支')
add('command','schedule','活动日程','Activity schedule',r(9,13,46,43,F)+l(9,24,55,24)+l(21,8,21,18)+l(43,8,43,18)+r(17,31,8,7,F)+r(30,31,8,7,F)+r(17,43,8,7,F)+p('M35 46 L39 50 48 40'),'日程','时间')
add('command','map-view','区域视图','Area view',p('M8 16 L24 9 40 16 56 9 56 48 40 55 24 48 8 55Z',F)+l(24,9,24,48)+l(40,16,40,55)+p('M34 25 A6 6 0 0 1 46 25 Q46 32 40 37 Q34 32 34 25Z',F)+c(40,25,2),'区域','地图')
add('command','team-coordination','团队协同','Team coordination',c(32,12,6,F)+c(13,42,6,F)+c(51,42,6,F)+p('M23 27 Q23 19 32 19 Q41 19 41 27 M5 57 Q5 49 13 49 Q21 49 21 57 M43 57 Q43 49 51 49 Q59 49 59 57 M18 33 L24 29 M40 29 L46 33 M25 49 L39 49'),'团队','组织')
add('command','event-command','活动组织单元','Event coordination',r(16,20,32,25,F)+l(32,20,32,9)+l(32,45,32,55)+l(16,32,7,32)+l(48,32,57,32)+c(32,7,3,F)+c(32,57,3,F)+c(7,32,3,F)+c(57,32,3,F)+p('M24 31 L30 37 40 27'),'活动组织')

# Communications: abstract links only; no frequencies, ranges or deployment details.
add('communications','antenna','通信天线','Antenna',l(32,23,32,55)+l(21,55,43,55)+c(32,20,4,F)+p('M22 11 Q12 20 22 29 M42 11 Q52 20 42 29 M15 5 Q-1 20 15 36 M49 5 Q65 20 49 36'),'天线')
add('communications','tower','通信塔','Communications tower',p('M32 13 L15 56 49 56Z',F)+p('M24 34 L42 44 M40 34 L22 44 M19 48 L45 48')+p('M22 9 Q14 16 22 23 M42 9 Q50 16 42 23')+c(32,13,3),'塔')
add('communications','dish','卫星通信终端','Satellite dish',p('M13 15 Q16 42 44 45 Q49 28 32 11Z',F)+l(17,19,43,36)+l(43,36,52,24)+c(52,23,3,F)+p('M28 42 L24 55 48 55 M39 45 L44 55'),'碟形天线','卫通')
add('communications','satellite','通信卫星','Communications satellite',group(r(25,24,14,17,F)+r(6,24,15,17,F)+r(43,24,15,17,F)+l(21,32,25,32)+l(39,32,43,32)+l(13,24,13,41)+l(50,24,50,41)+p('M27 23 Q32 10 37 23')+l(32,18,32,9),transform='rotate(-30 32 32)'),'卫星')
add('communications','mesh','网状网络','Mesh network',p('M32 10 L10 29 19 52 45 52 54 29Z M32 10 L32 34 10 29 M54 29 L32 34 19 52 M32 34 L45 52')+''.join(c(x,y,5,F) for x,y in [(32,10),(10,29),(19,52),(45,52),(54,29),(32,34)]),'自组网','mesh')
add('communications','router','路由器','Router',r(9,30,46,20,F)+l(17,30,14,10)+l(47,30,50,10)+c(18,41,2)+c(26,41,2)+l(37,40,48,40),'路由')
add('communications','switch','网络交换机','Network switch',r(7,23,50,25,F)+''.join(r(x,30,7,10) for x in [12,23,34,45])+p('M16 16 L48 16 M43 11 L48 16 43 21'),'交换机')
add('communications','gateway','网关','Gateway',r(22,16,20,32,F)+c(32,25,3)+l(27,36,37,36)+p('M6 24 L22 24 M11 19 L6 24 11 29 M42 40 L58 40 M53 35 L58 40 53 45')+l(27,41,37,41),'网关')
add('communications','radio','手持电台','Handheld radio',r(21,20,25,35,F)+l(27,20,23,6)+r(27,26,13,10)+l(27,42,40,42)+l(27,47,40,47)+l(38,15,38,20),'电台','对讲')
add('communications','base-station','基站设备','Base station',r(11,28,25,28,F)+l(17,37,29,37)+l(17,45,29,45)+l(46,18,46,56)+r(40,8,12,18,F)+l(41,56,52,56)+l(36,42,46,42),'基站')
add('communications','wifi','无线接入','Wireless access',p('M7 22 Q32 2 57 22 M15 32 Q32 17 49 32 M23 42 Q32 34 41 42')+c(32,51,4,F),'Wi-Fi','WLAN','无线')
add('communications','fibre','光纤链路','Fibre link',r(7,23,13,20,F)+r(44,23,13,20,F)+p('M20 28 C29 28 35 38 44 38 M20 38 C29 38 35 28 44 28 M20 33 L44 33')+l(10,16,16,16)+l(48,50,54,50),'光纤')
add('communications','ethernet','有线网络接口','Ethernet port',p('M12 14 L52 14 52 45 41 45 41 53 23 53 23 45 12 45Z',F)+r(18,21,28,16)+''.join(l(x,21,x,29) for x in (23,29,35,41)),'以太网','有线')
add('communications','acoustic-modem','水声通信机','Acoustic modem',r(22,19,20,31,F,5)+l(26,25,38,25)+c(32,41,4)+p('M13 22 Q4 33 13 44 M51 22 Q60 33 51 44')+l(32,19,32,8),'水声','调制解调')
add('communications','relay','中继节点','Relay node',r(22,23,20,20,F)+p('M5 13 Q17 13 17 25 M5 21 Q9 21 9 25 M47 41 Q47 53 59 53 M55 41 Q55 45 59 45 M26 33 L37 33 M33 29 L37 33 33 37'),'中继')
add('communications','network-cloud','共享网络','Shared network',p('M17 39 Q7 39 7 30 Q7 21 17 20 Q20 6 34 12 Q45 8 49 21 Q58 22 58 31 Q58 39 49 39Z',F)+p('M32 39 L32 49 M14 49 L50 49 M14 49 L14 54 M50 49 L50 54')+c(14,56,3,F)+c(32,54,3,F)+c(50,56,3,F),'共享','云')

# Support: basic infrastructure, sustainment and field services.
add('support','battery','电池模块','Battery',r(11,18,39,30,F)+r(50,27,6,12,F)+p('M32 23 L23 35 32 35 28 43 40 30 31 30Z',F),'电池','能源')
add('support','charging-station','充电站','Charging station',r(12,9,29,47,F)+r(18,16,17,13)+p('M41 26 Q53 26 53 39 L53 47 Q53 54 46 50 L46 37')+p('M27 34 L21 43 29 43 24 51'),'充电')
add('support','solar-panel','光伏面板','Solar panel',p('M16 16 L49 16 56 42 9 42Z',F)+l(12,29,52,29)+l(27,16,24,42)+l(39,16,41,42)+l(26,42,26,53)+l(39,42,39,53)+l(18,55,47,55),'光伏','太阳能')
add('support','generator','便携电源','Portable power unit',r(10,22,44,29,F,5)+p('M22 22 L22 13 42 13 42 22')+c(22,37,6)+r(36,30,10,13)+l(17,51,17,56)+l(47,51,47,56),'电源','发电')
add('support','server','服务器','Server',r(17,7,30,50,F)+l(17,24,47,24)+l(17,41,47,41)+c(25,16,2)+c(25,33,2)+c(25,49,2)+l(33,16,40,16)+l(33,33,40,33)+l(33,49,40,49),'服务器')
add('support','database','数据库','Database',p('M12 17 L12 48 Q32 61 52 48 L52 17Z',F)+e(32,17,20,8,F)+p('M12 32 Q32 45 52 32 M12 44 Q32 57 52 44'),'数据')
add('support','edge-compute','边缘计算单元','Edge compute',r(18,18,28,28,F)+r(25,25,14,14)+''.join(l(x,9,x,18)+l(x,46,x,55)+l(9,x,18,x)+l(46,x,55,x) for x in (24,32,40)),'边缘','计算')
add('support','storage','存储单元','Storage unit',r(10,17,44,35,F,5)+c(32,34,11)+c(32,34,3)+p('M32 34 L44 44')+c(16,23,1)+c(48,46,1),'存储')
add('support','tool-kit','维护工具箱','Tool kit',r(9,23,46,31,F)+p('M22 23 L22 13 42 13 42 23 M9 36 L55 36')+r(28,32,8,10,F),'工具箱','工具')
add('support','maintenance','维护服务','Maintenance',p('M44 8 Q34 5 29 15 Q26 21 30 27 L10 47 Q6 51 12 56 Q15 59 19 54 L39 34 Q50 38 56 27 L47 29 40 22Z',F)+c(15,51,2),'维护','维修')
add('support','container','设备集装箱','Equipment container',r(7,18,50,35,F)+''.join(l(x,23,x,48) for x in (15,23,31,39,47))+l(7,25,57,25)+l(7,46,57,46),'集装箱')
add('support','warehouse','保障仓库','Warehouse',p('M7 23 L32 9 57 23 57 55 7 55Z',F)+r(18,29,28,26)+l(18,37,46,37)+l(18,45,46,45)+r(26,16,12,7),'仓库')
add('support','field-shelter','现场工作站','Field shelter',p('M8 30 L20 10 44 10 56 30 56 55 8 55Z',F)+p('M8 30 L56 30 M24 55 L24 35 40 35 40 55')+r(12,36,7,10)+r(45,36,7,10),'现场','帐篷')
add('support','laboratory','测试实验室','Test laboratory',p('M24 9 L40 9 M28 9 L28 25 12 49 Q9 55 17 55 L47 55 Q55 55 52 49 L36 25 36 9',F)+p('M20 38 L44 38')+c(28,46,3)+c(38,49,2),'实验','测试')
add('support','first-aid','应急医疗点','First-aid point',r(10,21,44,33,F,5)+p('M22 21 L22 12 42 12 42 21')+p('M28 28 L36 28 36 34 42 34 42 42 36 42 36 48 28 48 28 42 22 42 22 34 28 34Z',F),'急救','医疗','救援')
add('support','personnel','保障人员','Support personnel',c(32,18,10,F)+p('M13 55 L13 44 Q13 33 24 31 L32 39 40 31 Q51 33 51 44 L51 55Z',F)+l(23,41,23,55)+l(41,41,41,55)+p('M23 46 L41 46'),'人员')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    assert len(assets) == 128
    assert len({a['id'] for a in assets}) == len(assets)
    for cat, name, stroke, fill, keywords in CATEGORIES:
        selected = [a for a in assets if a['categoryId'] == cat]
        assert len(selected) == 16
        (OUT / cat).mkdir(exist_ok=True)
        for asset in selected:
            art = asset['art'].replace(F, fill)
            title = html.escape(asset['name'] + ' · ' + asset['englishName'])
            svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">\n'
                   '<title>%s</title>\n<desc>Original ZhiLi illustrative symbol. Not a technical specification.</desc>\n'
                   '<g stroke="%s" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" fill="none"><g transform="translate(2 2) scale(0.9375)">%s</g></g>\n</svg>\n') % (title, stroke, art)
            (OUT / asset['file']).write_text(svg, encoding='utf-8')
    catalog = {
        'format': 'zhili-symbol-catalog', 'version': 1, 'license': 'CC0-1.0',
        'source': 'Original project artwork generated by tools/generate_symbols.py; no external artwork.',
        'purpose': 'Illustrative diagram symbols only; not technical or operational specifications.',
        'categories': [{'id': c[0], 'name': c[1], 'keywords': c[4]} for c in CATEGORIES],
        'assets': [{k:v for k,v in a.items() if k != 'art'} for a in assets]
    }
    (OUT / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    resource = ['<RCC><qresource prefix="/symbols">', '  <file alias="catalog.json">symbols/catalog.json</file>']
    resource += ['  <file alias="%s">symbols/%s</file>' % (a['file'], a['file']) for a in assets]
    resource += ['</qresource></RCC>']
    (ROOT/'assets'/'symbols.qrc').write_text('\n'.join(resource)+'\n', encoding='utf-8')
    body = []
    for cat, name, *_ in CATEGORIES:
        body += ['<section><h2>%s <small>16</small></h2><div class="grid">' % name]
        for a in assets:
            if a['categoryId'] == cat:
                body += ['<figure><img width="64" height="64" src="%s" alt=""><figcaption>%s<small>%s</small></figcaption></figure>' % (a['file'],a['name'],a['id'])]
        body += ['</div></section>']
    gallery = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>枝理 · 离线素材索引</title><style>body{font-family:sans-serif;margin:32px;color:#24334b;background:#f4f6fa}h1{margin-bottom:8px}p{color:#536078}h2{font-size:21px;margin-top:36px}h2 small{font-size:14px;color:#78849a}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:12px}figure{margin:0;padding:20px 8px 15px;background:white;border:1px solid #dce2ed;border-radius:10px;text-align:center}figcaption{font-size:14px;margin-top:12px}figcaption small{display:block;overflow-wrap:anywhere;font-size:10px;color:#78849a;margin-top:5px}img{width:64px;height:64px}</style><h1>枝理 · 离线素材索引</h1><p>128 个原创矢量符号 · 8 类 · CC0-1.0 · 示意图标，不代表技术或运行参数。</p>''' + ''.join(body) + '</html>\n'
    (OUT/'index.html').write_text(gallery,encoding='utf-8')
    print('Generated %d SVGs in %d categories.' % (len(assets),len(CATEGORIES)))

if __name__ == '__main__': main()
