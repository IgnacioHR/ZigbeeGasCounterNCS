#!/usr/bin/env python3

import argparse
import json

def parse_int(value):
    if isinstance(value, int):
        return value
    return int(str(value), 0)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--out-conf", required=True)
    parser.add_argument("--out-header", required=True)
    args = parser.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        data = json.load(f)

    manufacturer_id = parse_int(data["manufacturer_id"])
    image_type = parse_int(data["image_type"])

    hw_version = parse_int(data["hardware"]["version"])
    ota_hw_version = parse_int(data["hardware"]["ota_hw_version"])

    fota_endpoint = parse_int(data["zigbee"]["fota_endpoint"])

    sign_version = str(data["software"]["imgtool_sign_version"])
    app_version = parse_int(data["software"]["app_version"])
    app_build = parse_int(data["software"]["app_build"])
    stack_version = parse_int(data["software"]["stack_version"])
    stack_build = parse_int(data["software"]["stack_build"])
    sw_build_id = str(data["software"]["sw_build_id"])

    product_label = str(data["product"]["label"])

    with open(args.out_conf, "w", encoding="utf-8") as f:
        f.write("# Generated file. Do not edit.\n")
        f.write(f'CONFIG_ZIGBEE_FOTA_HW_VERSION={hw_version}\n')
        f.write(f'CONFIG_ZIGBEE_FOTA_ENDPOINT={fota_endpoint}\n')
        f.write(f'CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="{sign_version}"\n')
        f.write(f'CONFIG_ZIGBEE_FOTA_MANUFACTURER_ID=0x{manufacturer_id:04x}\n')
        f.write(f'CONFIG_ZIGBEE_FOTA_IMAGE_TYPE=0x{image_type:04x}\n')

    with open(args.out_header, "w", encoding="utf-8") as f:
        f.write("/* Generated file. Do not edit. */\n")
        f.write("#pragma once\n\n")
        f.write(f"#define OTA_UPGRADE_HW_VERSION          0x{ota_hw_version:04x}\n")
        f.write(f"#define OTA_UPGRADE_IMAGE_TYPE          0x{image_type:04x}\n\n")
        f.write(f"#define HARDWARE_VERSION                0x{hw_version:02x}\n\n")
        f.write(f"#define APP_BUILD                       {app_build}\n")
        f.write(f"#define APP_VERSION                     {app_version}\n")
        f.write(f"#define STACK_VERSION                   {stack_version}\n")
        f.write(f"#define STACK_BUILD                     {stack_build}\n\n")
        f.write(f"#define PRODUCT_LABEL                   {json.dumps(product_label)}\n")
        f.write(f"#define SW_BUILD_ID                     {json.dumps(sw_build_id)}\n\n")
        f.write("#define OTA_FILE_VERSION (((APP_VERSION) << 24) | \\\n")
        f.write("                          ((APP_BUILD) << 16) | \\\n")
        f.write("                          ((STACK_VERSION) << 8) | \\\n")
        f.write("                          (STACK_BUILD))\n")


if __name__ == "__main__":
    main()