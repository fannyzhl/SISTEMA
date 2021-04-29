#!/usr/bin/env python3

import os
import platform
import threading
import re
import traceback
import ctypes

import tkinter as tk
from tkinter import filedialog
from tkinter import messagebox
from tkinter import font

import PIL
from PIL import Image

from conversion import convertPngToSvg, convertSvgToGcode

from arduino import *

import mysql.connector

import time

MAX_WIDTH_MM = 30.0
MAX_HEIGHT_MM = 30.0

OFFSET_X_MM = 80.0
OFFSEY_Y_MM = 80.0

WINDOWS_SCALING_FACTOR = 96.0
SCALE = 1.0

WINDOW_WIDTH = 400
WINDOW_HEIGHT = 160

DB_HOST = "localhost"
DB_USER = "root"
DB_PASS = ""
DB_NAME = "png2gcode"


def arduinoSendGcode(gcode_path):
    global g_tkRoot
    global g_arduinoObj

    try:
        file = open(gcode_path, "r")
        lines = file.readlines()
        file.close()
    except:
        traceback.print_exc()
        messagebox.showerror(
            "Error",
            "¡Error al leer líneas del archivo G-code generado!",
            parent=g_tkRoot,
        )
        return False

    if not g_arduinoObj.connect():
        messagebox.showerror("Error", "¡Error de conexión al Arduino!", parent=g_tkRoot)
        return False

    for line in lines:
        print('Enviando: "%s"' % (line.strip()))

        if not g_arduinoObj.send(line.encode("utf-8")):
            messagebox.showerror(
                "Error", "¡Error de envío al Arduino!", parent=g_tkRoot
            )
            return False

        time.sleep(0.015)  # 15 ms

        """
        while g_arduinoObj.available():
            response = g_arduinoObj.recv_until()
            if not response:
                messagebox.showerror('Error', '¡Error de recepción desde el Arduino!', parent=g_tkRoot)
                return False

            print('Recibiendo: %s' % (response.decode('utf-8').strip()))

            time.sleep(0.05) # 50 ms
        """

    g_arduinoObj.disconnect()

    return True


def imageTracingThreadFunc(name, last_name, png_path, image):
    global g_tkRoot
    global g_tkCanvas
    global g_tkProgressText
    global g_dbConnection
    global g_dbCursor
    global g_arduinoObj

    success = False

    # Generar rutas de salida.
    png_filename = os.path.splitext(png_path)[0]
    svg_path = png_filename + ".svg"
    gcode_path = png_filename + ".gcode"

    # Realizar conversión PNG -> SVG -> G-code.
    success = (
        convertPngToSvg(
            image, svg_path, MAX_WIDTH_MM, MAX_HEIGHT_MM, OFFSET_X_MM, OFFSEY_Y_MM
        )
        == True
    ) and (convertSvgToGcode(svg_path, gcode_path) == True)

    # Cerrar imagen PNG.
    image.close()

    if success:
        # Enviar G-code al Arduino línea por línea.
        success = arduinoSendGcode(gcode_path)
    else:
        messagebox.showerror(
            "Error",
            "¡El proceso de conversión PNG -> SVG -> G-code falló!",
            parent=g_tkRoot,
        )

    # Remover texto de conversión.
    g_tkCanvas.itemconfigure(g_tkProgressText, state="hidden")

    # Rehabilitar elementos de la ventana.
    uiToggleElements(True)

    # Retornar inmediatamente de ser necesario.
    if not success:
        return

    # Guardar registro en base de datos.
    sql = "INSERT INTO `conversions` (`name`, `last_name`, `png_path`, `svg_path`) VALUES (%s, %s, %s, %s)"
    val = (name, last_name, png_path, svg_path)

    try:
        g_dbCursor.execute(sql, val)
        g_dbConnection.commit()
    except:
        traceback.print_exc()
        messagebox.showerror(
            "Error",
            "¡No se pudo guardar el registro de conversión en base de datos!",
            parent=g_tkRoot,
        )
        return

    messagebox.showinfo(
        "Información",
        '¡Conversión realizada exitosamente!\nSVG almacenado en "{}".\nG-code almacenado en "{}".'.format(
            svg_path, gcode_path
        ),
        parent=g_tkRoot,
    )


def uiHandleExitProtocol():
    global g_tkRoot

    if messagebox.askokcancel("Aviso", "¿Deseas salir del programa?", parent=g_tkRoot):
        g_tkRoot.destroy()


def uiHandleExitProtocolStub():
    pass


def uiToggleElements(enable):
    global g_tkRoot
    global g_tkNameText
    global g_tkLastNameText
    global g_tkOpenPngButton

    if enable == True:
        obj_state = "normal"
        protocol_ref = uiHandleExitProtocol
    else:
        obj_state = "disabled"
        protocol_ref = uiHandleExitProtocolStub

    g_tkNameText["state"] = g_tkLastNameText["state"] = g_tkOpenPngButton[
        "state"
    ] = obj_state

    g_tkRoot.protocol("WM_DELETE_WINDOW", protocol_ref)


def uiOpenPng():
    global g_tkRoot
    global g_tkCanvas
    global g_tkNameText
    global g_tkLastNameText
    global g_tkOpenPngButton
    global g_arduinoObj

    name = re.sub(
        r"\s+", " ", g_tkNameText.get("1.0", tk.END).strip(), flags=re.MULTILINE
    )
    # print(bytes(name, encoding='utf8'))

    last_name = re.sub(
        r"\s+", " ", g_tkLastNameText.get("1.0", tk.END).strip(), flags=re.MULTILINE
    )
    # print(bytes(last_name, encoding='utf8'))

    if (not name) or (not last_name):
        messagebox.showerror(
            "Error",
            "¡Los campos de nombre y apellido no pueden estar vacíos!",
            parent=g_tkRoot,
        )
        return

    # Verificar si hay un Arduino conectado.
    if not g_arduinoObj.is_available():
        messagebox.showerror(
            "Error", "¡Conecte un Arduino al sistema!", parent=g_tkRoot
        )
        return

    # Mostrar diálogo para abrir archivo PNG.
    png_path = filedialog.askopenfilename(
        parent=g_tkRoot,
        title="Seleccione una imagen PNG",
        initialdir=os.path.abspath(os.path.dirname(__file__)),
        filetypes=[("Imágenes PNG", ".png")],
    )
    if not png_path:
        return

    # Obtener ruta absoluta hacia el archivo PNG seleccionado.
    png_path = os.path.abspath(os.path.expanduser(os.path.expandvars(png_path)))

    # Abrir imagen PNG.
    try:
        image = Image.open(fp=png_path, formats=["PNG"])
    except PIL.UnidentifiedImageError:
        messagebox.showerror(
            "Error", "¡El archivo proporcionado no es una imagen PNG!", parent=g_tkRoot
        )
        return
    except:
        traceback.print_exc()
        messagebox.showerror(
            "Error", "No se pudo abrir la imagen PNG.", parent=g_tkRoot
        )
        return

    # Deshabilitar elementos de la ventana.
    uiToggleElements(False)

    # Mostrar texto de conversión.
    g_tkCanvas.itemconfigure(g_tkProgressText, state="normal")

    # Crear hilo secundario en el que llevaremos a cabo el proceso de conversión y retornar inmediatamente.
    conv_thread = threading.Thread(
        target=imageTracingThreadFunc,
        args=[name, last_name, png_path, image],
        daemon=False,
    )
    conv_thread.start()


def uiDefaultAction(event):
    uiOpenPng()
    return "break"


def uiHandleTabInput(event):
    event.widget.tk_focusNext().focus()
    return "break"


def uiHandleShiftTabInput(event):
    event.widget.tk_focusPrev().focus()
    return "break"


def uiScaleMeasure(measure):
    return round(float(measure) * SCALE)


def main():
    global SCALE
    global g_tkRoot
    global g_tkCanvas
    global g_tkNameText
    global g_tkLastNameText
    global g_tkOpenPngButton
    global g_tkProgressText
    global g_dbConnection
    global g_dbCursor
    global g_arduinoObj

    g_arduinoObj = Arduino()

    # Obtener información sobre el sistema.
    os_type = platform.system()
    os_version = platform.version()

    # Determinar si estamos corriendo bajo Windows Vista o superior.
    # Procedimiento necesario para habilitar el uso de valores DPI altos.
    dpi_aware = False
    win_vista = (os_type == "Windows") and (
        int(os_version[: os_version.find(".")]) >= 6
    )
    if win_vista == True:
        try:
            # Habilitar el uso de DPI alto.
            dpi_aware = ctypes.windll.user32.SetProcessDPIAware() == 1
            if dpi_aware == False:
                dpi_aware = ctypes.windll.shcore.SetProcessDpiAwareness(1) == 0
        except:
            traceback.print_exc()

    # Crear objeto raíz Tkinter.
    g_tkRoot = tk.Tk()

    # Obtener resolución de pantalla.
    screen_width_px = g_tkRoot.winfo_screenwidth()
    screen_height_px = g_tkRoot.winfo_screenheight()

    # Obtener densidad de píxeles por pulgada (DPI / PPI).
    screen_dpi = round(g_tkRoot.winfo_fpixels("1i"))

    # Actualizar escala de redimensionado (de ser necesario).
    if (win_vista == True) and (dpi_aware == True):
        SCALE = float(screen_dpi) / WINDOWS_SCALING_FACTOR

    print(
        "Anchura: %u px. Altura: %u px. DPI: %u. Escala: %.2f%%."
        % (screen_width_px, screen_height_px, screen_dpi, SCALE * 100.0)
    )

    # Conectar a la base de datos MySQL.
    try:
        g_dbConnection = mysql.connector.connect(
            host=DB_HOST, user=DB_USER, password=DB_PASS, database=DB_NAME
        )
        g_dbCursor = g_dbConnection.cursor()
    except:
        traceback.print_exc()
        g_tkRoot.withdraw()
        messagebox.showerror(
            "Error",
            '¡No se pudo conectar a la base de datos MySQL "{}" en "{}"!'.format(
                DB_NAME, DB_HOST
            ),
            parent=g_tkRoot,
        )
        g_tkRoot.destroy()
        return

    g_tkRoot.resizable(False, False)  # La ventana no será redimensionable.
    g_tkRoot.title("Sistema de vectorización de imágenes")  # Título de la ventana.
    g_tkRoot.protocol(
        "WM_DELETE_WINDOW", uiHandleExitProtocol
    )  # Interceptar el protocolo de salida de la ventana.

    # Determinar medidas de la ventana.
    window_width_px = uiScaleMeasure(WINDOW_WIDTH)
    window_height_px = uiScaleMeasure(WINDOW_HEIGHT)

    # Centrar ventana.
    pos_hor = int((screen_width_px / 2) - (window_width_px / 2))
    pos_ver = int((screen_height_px / 2) - (window_height_px / 2))
    g_tkRoot.geometry("+{}+{}".format(pos_hor, pos_ver))

    # Crear canvas con elementos a mostrar en la ventana.
    g_tkCanvas = tk.Canvas(g_tkRoot, width=window_width_px, height=window_height_px)
    g_tkCanvas.pack()

    g_tkCanvas.create_text(
        uiScaleMeasure(50), uiScaleMeasure(30), text="Nombre:", anchor=tk.CENTER
    )

    g_tkNameText = tk.Text(
        g_tkRoot, height=1, width=40, font=font.nametofont("TkDefaultFont"), wrap="none"
    )
    g_tkNameText.bind("<Return>", uiDefaultAction)
    g_tkNameText.bind("<Tab>", uiHandleTabInput)
    g_tkNameText.bind("<Shift-Tab>", uiHandleShiftTabInput)
    g_tkCanvas.create_window(
        uiScaleMeasure(230), uiScaleMeasure(30), window=g_tkNameText, anchor=tk.CENTER
    )

    g_tkCanvas.create_text(
        uiScaleMeasure(50), uiScaleMeasure(60), text="Apellido:", anchor=tk.CENTER
    )

    g_tkLastNameText = tk.Text(
        g_tkRoot, height=1, width=40, font=font.nametofont("TkDefaultFont"), wrap="none"
    )
    g_tkLastNameText.bind("<Return>", uiDefaultAction)
    g_tkLastNameText.bind("<Tab>", uiHandleTabInput)
    g_tkLastNameText.bind("<Shift-Tab>", uiHandleShiftTabInput)
    g_tkCanvas.create_window(
        uiScaleMeasure(230),
        uiScaleMeasure(60),
        window=g_tkLastNameText,
        anchor=tk.CENTER,
    )

    g_tkOpenPngButton = tk.Button(text="Abrir PNG", command=uiOpenPng, width=10)
    g_tkCanvas.create_window(
        uiScaleMeasure(200),
        uiScaleMeasure(100),
        window=g_tkOpenPngButton,
        anchor=tk.CENTER,
    )

    g_tkProgressText = g_tkCanvas.create_text(
        uiScaleMeasure(200),
        uiScaleMeasure(140),
        text="Conversión en progreso. Por favor, espere.",
        anchor=tk.CENTER,
    )
    g_tkCanvas.itemconfigure(g_tkProgressText, state="hidden")

    g_tkRoot.bind("<Return>", uiDefaultAction)

    g_tkRoot.mainloop()

    # Desconectar de la base de datos
    g_dbCursor.close()
    g_dbConnection.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
