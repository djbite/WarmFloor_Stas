import os
import re

VERSION_FILE = "src/version.h"

def increment_version():
    if not os.path.exists(VERSION_FILE):
        return

    with open(VERSION_FILE, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Ищем текущую версию
    match = re.search(r'#define FIRMWARE_VERSION "(.*?)"', content)
    if not match:
        return
    
    version = match.group(1).split(".")
    version[-1] = str(int(version[-1]) + 1)
    new_version = ".".join(version)
    
    new_content = re.sub(
        r'#define FIRMWARE_VERSION ".*?"',
        f'#define FIRMWARE_VERSION "{new_version}"',
        content
    )
    
    with open(VERSION_FILE, "w", encoding="utf-8") as f:
        f.write(new_content)

increment_version()
