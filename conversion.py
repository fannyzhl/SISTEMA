import math
import traceback

from PIL import Image, ImageOps

from svg_to_gcode.svg_parser import parse_file
from svg_to_gcode.compiler import Compiler, interfaces

no_cv = False

try:
    import numpy as np
    import cv2
except:
    print('OpenCV no disponible.')
    no_cv = True

F_Blur = {
    (-2,-2):2,(-1,-2):4,(0,-2):5,(1,-2):4,(2,-2):2,
    (-2,-1):4,(-1,-1):9,(0,-1):12,(1,-1):9,(2,-1):4,
    (-2,0):5,(-1,0):12,(0,0):15,(1,0):12,(2,0):5,
    (-2,1):4,(-1,1):9,(0,1):12,(1,1):9,(2,1):4,
    (-2,2):2,(-1,2):4,(0,2):5,(1,2):4,(2,2):2,
}

F_SobelX = {(-1,-1):1,(0,-1):0,(1,-1):-1,(-1,0):2,(0,0):0,(1,0):-2,(-1,1):1,(0,1):0,(1,1):-1}

F_SobelY = {(-1,-1):1,(0,-1):2,(1,-1):1,(-1,0):0,(0,0):0,(1,0):0,(-1,1):-1,(0,1):-2,(1,1):-1}

def distsum(*args):
    return sum([ ((args[i][0]-args[i-1][0])**2 + (args[i][1]-args[i-1][1])**2)**0.5 for i in range(1,len(args))])

def appmask(IM, masks):
    PX = IM.load()
    w, h = IM.size
    NPX = {}
    for x in range(0,w):
        for y in range(0, h):
            a = [0] * len(masks)
            for i in range(len(masks)):
                for p in masks[i].keys():
                    if 0 < (x + p[0]) < w and 0 < (y + p[1]) < h:
                        a[i] += PX[x + p[0], y + p[1]] * masks[i][p]
                if sum(masks[i].values()) != 0:
                    a[i] = a[i] / sum(masks[i].values())
            NPX[x, y] = int(sum([v ** 2 for v in a]) ** 0.5)
    for x in range(0, w):
        for y in range(0, h):
            PX[x, y] = NPX[x, y]

def find_edges(image):
    if no_cv:
        #appmask(IM, [F_Blur])
        appmask(image, [F_SobelX, F_SobelY])
    else:
        im = np.array(image)
        im = cv2.GaussianBlur(im, (3, 3), 0)
        im = cv2.Canny(im, 100, 200)
        image = Image.fromarray(im)
    return image.point(lambda p: p > 128 and 255)

def getdots(IM):
    PX = IM.load()
    dots = []
    w, h = IM.size
    for y in range(h - 1):
        row = []
        for x in range(1, w):
            if PX[x, y] == 255:
                if len(row) > 0:
                    if x - row[-1][0] == row[-1][-1] + 1:
                        row[-1] = (row[-1][0], row[-1][-1] + 1)
                    else:
                        row.append((x, 0))
                else:
                    row.append((x, 0))
        dots.append(row)
    return dots

def connectdots(dots):
    contours = []
    for y in range(len(dots)):
        for x, v in dots[y]:
            if v > -1:
                if y == 0:
                    contours.append([(x, y)])
                else:
                    closest = -1
                    cdist = 100
                    for x0, v0 in dots[y-1]:
                        if abs(x0 - x) < cdist:
                            cdist = abs(x0 - x)
                            closest = x0
                    
                    if cdist > 3:
                        contours.append([(x, y)])
                    else:
                        found = 0
                        for i in range(len(contours)):
                            if contours[i][-1] == (closest, y - 1):
                                contours[i].append((x, y,))
                                found = 1
                                break
                        if found == 0:
                            contours.append([(x, y)])
        for c in contours:
            if c[-1][1] < y - 1 and len(c) < 4:
                contours.remove(c)
    return contours

def getcontours(image, draw_contours=2):
    image = find_edges(image)
    IM1 = image.copy()
    IM2 = image.rotate(-90, expand=True).transpose(Image.FLIP_LEFT_RIGHT)
    dots1 = getdots(IM1)
    contours1 = connectdots(dots1)
    dots2 = getdots(IM2)
    contours2 = connectdots(dots2)
    
    for i in range(len(contours2)):
        contours2[i] = [(c[1],c[0]) for c in contours2[i]]
    
    contours = contours1+contours2
    
    for i in range(len(contours)):
        for j in range(len(contours)):
            if len(contours[i]) > 0 and len(contours[j]) > 0:
                if distsum(contours[j][0], contours[i][-1]) < 8:
                    contours[i] = contours[i] + contours[j]
                    contours[j] = []
    
    for i in range(len(contours)):
        contours[i] = [contours[i][j] for j in range(0, len(contours[i]), 8)]
    
    contours = [c for c in contours if len(c) > 1]
    
    for i in range(0, len(contours)):
        contours[i] = [(v[0] * draw_contours, v[1] * draw_contours) for v in contours[i]]
    
    return contours

def hatch(image, draw_hatch=16):
    pixels = image.load()
    w, h = image.size
    lg1 = []
    lg2 = []
    
    for x0 in range(w):
        for y0 in range(h):
            x = x0 * draw_hatch
            y = y0 * draw_hatch
            
            if pixels[x0, y0] > 144:
                pass
            elif pixels[x0, y0] > 64:
                lg1.append([(x, y + draw_hatch / 4), (x + draw_hatch, y + draw_hatch / 4)])
            elif pixels[x0, y0] > 16:
                lg1.append([(x, y + draw_hatch / 4), (x + draw_hatch, y + draw_hatch / 4)])
                lg2.append([(x + draw_hatch, y), (x, y + draw_hatch)])
            else:
                lg1.append([(x, y + draw_hatch / 4), (x + draw_hatch, y + draw_hatch / 4)])
                lg1.append([(x, y + draw_hatch / 2 + draw_hatch / 4), (x + draw_hatch, y + draw_hatch / 2 + draw_hatch / 4)])
                lg2.append([(x + draw_hatch, y), (x, y + draw_hatch)])
    
    line_groups = [lg1, lg2]
    
    for line_group in line_groups:
        for lines in line_group:
            for lines2 in line_group:
                if lines and lines2:
                    if lines[-1] == lines2[0]:
                        lines.extend(lines2[1:])
                        lines2.clear()
        
        saved_lines = [[line[0], line[-1]] for line in line_group if line]
        line_group.clear()
        line_group.extend(saved_lines)
    
    lines = [item for group in line_groups for item in group]
    
    return lines

def sortlines(lines):
    clines = lines[:]
    slines = [clines.pop(0)]
    
    while clines != []:
        x, s, r = None, 1000000, False
        
        for l in clines:
            d = distsum(l[0], slines[-1][-1])
            dr = distsum(l[-1], slines[-1][-1])
            if d < s:
                x, s, r = l[:], d, False
            if dr < s:
                x, s, r = l[:], s, True
        
        clines.remove(x)
        
        if r == True:
            x = x[::-1]
        
        slines.append(x)
    
    return slines

def vectorise(image, resolution=1024, draw_contours=False, repeat_contours=1, draw_hatch=False, repeat_hatch=1):
    w, h = image.size
    mod_image = image.convert('L')
    mod_image = ImageOps.autocontrast(mod_image, 10)
    
    lines = []
    
    if draw_contours and repeat_contours:
        contours = sortlines(getcontours(mod_image.resize((int(resolution / draw_contours), int(resolution / draw_contours * h / w))), draw_contours))
        for r in range(repeat_contours):
            lines += contours
    
    if draw_hatch and repeat_hatch:
        hatches = sortlines(hatch(mod_image.resize((int(resolution / draw_hatch), int(resolution / draw_hatch * h / w))), draw_hatch))
        for r in range(repeat_hatch):
            lines += hatches
    
    return lines

def valmap(x, in_min, in_max, out_min, out_max):
    x = float(x)
    in_min = float(in_min)
    in_max = float(in_max)
    out_min = float(out_min)
    out_max = float(out_max)
    return ((x - in_min) * (out_max - out_min)) / ((in_max - in_min) + out_min)

def makesvg(lines, max_width_mm, max_height_mm, offset_x_mm, offset_y_mm):
    width = math.ceil(max([max([p[0] for p in l]) for l in lines]))
    height = math.ceil(max([max([p[1] for p in l]) for l in lines]))
    
    max_width = float(offset_x_mm + max_width_mm)
    max_height = float(offset_y_mm + max_height_mm)
    
    out = '<svg xmlns="http://www.w3.org/2000/svg" height="%.1fmm" width="%.1fmm" version="1.1">\n' % (max_height, max_width)
    
    for l in lines:
        cur_line = []
        
        for i in range(len(l)):
            p = l[i]
            x = offset_x_mm + valmap(p[0], 0, width, 0, max_width_mm)
            y = offset_y_mm + valmap(p[1], 0, height, 0, max_height_mm)
            cur_line.append(('M' if (i == 0) else 'L') + ("%.1f %.1f" % (round(x, 1), round(y, 1))))
        
        l = " ".join(cur_line)
        out += '<path d="' + l + '" stroke="black" stroke-width="1" fill="none" />\n'
    
    out += '</svg>'
    
    return out

def convertPngToSvg(image, svg_path, max_width_mm, max_height_mm, offset_x_mm, offset_y_mm):
    try:
        if (image.mode in ('RGBA', 'LA')) or ((image.mode == 'P') and ('transparency' in image.info)):
            alpha = image.convert('RGBA').getchannel('A')
            bg = Image.new('RGBA', image.size, (255, 255, 255, 255))
            bg.paste(image, mask=alpha)
            new_image = bg
        else:
            new_image = image
        
        lines = vectorise(new_image.convert('RGB'), draw_contours=1, repeat_contours=5, draw_hatch=0, repeat_hatch=0)
        svg_data = makesvg(lines, max_width_mm, max_height_mm, offset_x_mm, offset_y_mm)
        
        with open(svg_path, "w") as svg_file:
            svg_file.write(svg_data)
    except:
        traceback.print_exc()
        return False
    
    return True

def convertSvgToGcode(svg_path, gcode_path):
    try:
        gcode_compiler = Compiler(interfaces.Gcode, movement_speed=1000, cutting_speed=300, pass_depth=0, unit='mm')
        curves = parse_file(svg_path, transform_origin=False)
        gcode_compiler.append_curves(curves)
        gcode_compiler.compile_to_file(gcode_path)
    except:
        traceback.print_exc()
        return False
    
    return True
