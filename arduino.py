#!/usr/bin/env python3

import time
import serial, serial.tools.list_ports
import traceback

class Arduino:
    def __init__(self, desc='arduino', bps=115200):
        self.desc = desc.lower()
        self.bps = bps
        self.port = None
        self.serial = None
    
    def get_port(self):
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if self.desc in p.description.lower():
                return p
        return None
    
    def is_available(self):
        return (self.get_port() is not None)
    
    def connect(self):
        if self.serial: return True
        
        self.port = self.get_port()
        if not self.port: return False
        
        print('Arduino encontrado: %s.' % (self.port.description))
        
        try:
            self.serial = serial.Serial(self.port.device, self.bps)
        except:
            traceback.print_exc()
            self.port = None
            return False
        
        time.sleep(2)
        
        return True
    
    def disconnect(self):
        if not self.serial: return
        
        self.serial.close()
        self.serial = None
        self.port = None
    
    def send(self, data):
        if not self.serial: return 0
        
        wr = 0
        
        try:
            wr = self.serial.write(data)
        except:
            traceback.print_exc()
        
        return wr
    
    def recv(self, size=1):
        if not self.serial: return None
        
        rd = None
        
        try:
            rd = self.serial.read(size)
            self.serial.flush()
        except:
            traceback.print_exc()
        
        return rd
    
    def recv_until(self, expected=serial.LF, size=None):
        if not self.serial: return None
        
        rd = None
        
        try:
            rd = self.serial.read_until(expected, size)
        except:
            traceback.print_exc()
        
        return rd
