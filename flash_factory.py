#!/usr/bin/env python3
import subprocess
import sys
import os

BUILD_DIR = os.path.join(os.path.dirname(__file__), 'build')
BIN_FILE = os.path.join(BUILD_DIR, 'CentralHome_factory.bin')  # Renomeado
FACTORY_OFFSET = '0xB30000'
PORT = 'COM3'
BAUD = '921600'

if not os.path.exists(BIN_FILE):
    print(f"Erro: {BIN_FILE} não encontrado.")
    print(f"Copie build/CentralHome.bin para build/CentralHome_factory.bin antes de executar.")
    sys.exit(1)

print(f"Gravando {BIN_FILE} na partição factory...")

cmd = [
    sys.executable, '-m', 'esptool',
    '--chip', 'esp32s3',
    '--port', PORT,
    '--baud', BAUD,
    'write_flash',
    FACTORY_OFFSET, BIN_FILE
]

result = subprocess.run(cmd)
sys.exit(result.returncode)
