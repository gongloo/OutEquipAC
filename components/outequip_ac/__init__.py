import os
import gzip
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

import esphome.final_validate as fv

CODEOWNERS = ["@gongloo"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

outequip_ac_ns = cg.esphome_ns.namespace("outequip_ac")
OutEquipAC = outequip_ac_ns.class_("OutEquipAC", cg.Component, uart.UARTDevice)

CONF_OUTEQUIP_AC_ID = "outequip_ac_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OutEquipAC),
}).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)

def final_validate(config):
    full_config = fv.full_config.get()
    if "web_server" in full_config:
        web_server_config = full_config["web_server"]
        if isinstance(web_server_config, list):
            for ws in web_server_config:
                ws["log"] = False
        elif isinstance(web_server_config, dict):
            web_server_config["log"] = False
    return config

FINAL_VALIDATE_SCHEMA = final_validate

def get_mdi_path(icon_name):
    # Dynamic compile-time MDI SVG path downloader.
    # Natively calls ESPHome's internal download_gh_svg utility to pull, cache, and resolve
    # icons inside the .esphome workspace, eliminating code duplication!
    from esphome.components.image import download_gh_svg, SOURCE_MDI
    import re
    
    icon_name = "".join(c for c in icon_name.lower() if c.isalnum() or c in "-_")
    
    try:
        # Call ESPHome's native downloader/cacher!
        # This automatically resolves the path, downloads it if missing, and returns the absolute local path.
        svg_file_path = download_gh_svg(icon_name, SOURCE_MDI)
        
        with open(svg_file_path, "r", encoding="utf-8") as f:
            svg_content = f.read()
            match = re.search(r'<path\s+d="([^"]+)"', svg_content)
            if match:
                return match.group(1)
    except Exception as e:
        raise RuntimeError(
            f"Failed to fetch or parse MDI icon '{icon_name}' natively. "
            "Please ensure you have an active internet connection during initial compilation."
        ) from e

    raise RuntimeError(f"Icon path not found in fetched SVG for '{icon_name}'")


def generate_html_header():
    from esphome.core import CORE
    
    current_dir = os.path.dirname(__file__)
    html_path = os.path.join(current_dir, "thermostat.html")
    
    # Generate directly in the .esphome build workspace's src root!
    build_dir = os.path.join(CORE.build_path, "src")
    os.makedirs(build_dir, exist_ok=True)
    output_header_path = os.path.join(build_dir, "outequip_ac_thermostat_html.h")
    
    if not os.path.exists(html_path):
        return

    with open(html_path, "r", encoding="utf-8") as f:
        html_content = f.read()

    # Preprocess all <svg ... data-mdi="icon_name">...</svg> elements
    import re
    pattern = r'(<svg\b[^>]*data-mdi="([^"]+)"[^>]*>)(.*?)(</svg>)'
    
    def mdi_replacer(match):
        opening_tag = match.group(1)
        icon_name = match.group(2)
        path_data = get_mdi_path(icon_name)
        return f'{opening_tag}<path d="{path_data}"/></svg>'
        
    html_content = re.sub(pattern, mdi_replacer, html_content, flags=re.DOTALL)

    gzipped_bytes = gzip.compress(html_content.encode("utf-8"))
    hex_bytes_str = ", ".join(f"0x{b:02x}" for b in gzipped_bytes)

    header_content = f"""#pragma once
#include "esphome/core/progmem.h"
#include <stddef.h>

// Automatically generated during ESPHome compile phase. Do not edit.
constexpr uint8_t OUTEQUIP_AC_HTML_GZ[] PROGMEM = {{{hex_bytes_str}}};
constexpr size_t OUTEQUIP_AC_HTML_GZ_SIZE = {len(gzipped_bytes)};
"""

    with open(output_header_path, "w", encoding="utf-8") as f:
        f.write(header_content)


async def to_code(config):
    # Dynamically generate custom_index.h inside the build directory instead of the source directory!
    generate_html_header()
    
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)


