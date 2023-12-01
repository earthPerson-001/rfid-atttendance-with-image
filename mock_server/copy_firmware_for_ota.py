import subprocess
import json
import pathlib
import os
from typing import Dict, List

Import("env")

board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
board_name = env["BOARD"]


def get_firmware_version() -> str:
    ret = subprocess.run(
        ["git", "describe"], stdout=subprocess.PIPE, text=True
    )  # Uses only annotated tags
    # ret = subprocess.run(["git", "describe", "--tags"], stdout=subprocess.PIPE, text=True) #Uses any tags
    build_version = ret.stdout.strip()
    return build_version


# this doesn't change for a particular build so no point in calling it multiple times
firmware_version = get_firmware_version()
project_dir = pathlib.Path(env["PROJECT_DIR"])


def get_manifest() -> List[Dict[str, object]] | None:
    server_dir = project_dir.joinpath("mock_server")
    http_server_root_dir = server_dir.joinpath("http_server_root")
    manifest_file = http_server_root_dir.joinpath("manifest.json")
    loaded_json = None
    with open(manifest_file, "r") as f:
        loaded_json = json.load(f)
    return loaded_json


def display_manifest(source, target, env):
    print(get_manifest())


def update_manifest(source, target, env):
    """
    JSON Example:
    .. highlight:: python
    .. code-block:: json
        [
            {
                "name": "rfid-attendance-with-image",
                "build-type": "alpha",
                "board": "esp32cam",
                "firmware-url": "esp32cam/esp32cam_esp32_firmware_v0.1.0-alpha-1-g70c0330.bin",
                "version-short": 0.1,
                "version": "0.1.0",
                "version-long": "v0.1.0-alpha-1-g70c0330",
                "criticality": 10
            }
        ]
    """
    name = "rfid-attendance-with-image"
    # fixme: add support for release build
    build_type = "alpha" if "alpha" in firmware_version else "beta"
    firmware_url = f"{board_name}/{board_name}_{mcu}_firmware_{firmware_version}.bin"
    manifest_file = (
        project_dir.joinpath("mock_server")
        .joinpath("http_server_root")
        .joinpath("manifest.json")
    )

    # todo: do this properly
    splitted_version = firmware_version.rsplit(".")
    version_short = float(f"{splitted_version[0][1]}.{splitted_version[1]}")
    version = str(version_short) + "." + splitted_version[2][0]

    # make it very low critical unless specified otherwise
    # higher number meaning less critical
    criticality = 10

    json_list_object = get_manifest()
    found_similar = False
    print("Retriving previous data.")
    if json_list_object is not None:
        for obj in json_list_object:
            if (
                obj.get("name", "") == name
                and obj.get("version-long", "") == firmware_version
                and obj.get("firmware-url", "") == firmware_url
            ):
                found_similar = True
                print("Found an existing build with same information. \n", obj, "\n .")
                break
    else:
        json_list_object = []

    # adding only if the newly generated firmware is unique
    if not found_similar:
        new_entry = {
            "name": name,
            "build-type": build_type,
            "board": board_name,
            "firmware-url": firmware_url,
            "version-short": version_short,
            "version": version,
            "version-long": firmware_version,
            "criticality": criticality,
        }
        print(f"Appending {new_entry} to {manifest_file}.")
        with open(manifest_file, "w") as manifest_json:
            json_list_object.append(new_entry),
            json.dump(
                json_list_object,
                fp=manifest_json,
                indent=3
            )


def create_board_directory(source, target, env):
    os.makedirs(
        f'{env["PROJECT_DIR"]}/mock_server/http_server_root/{board_name}', exist_ok=True
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", create_board_directory)


env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(
        " ".join(
            [
                '"$PYTHONEXE" "$OBJCOPY"',
                "--chip",
                mcu,
                "elf2image",
                "--flash_mode",
                "${__get_board_flash_mode(__env__)}",
                "--flash_freq",
                "${__get_board_f_flash(__env__)}",
                "--flash_size",
                board.get("upload.flash_size", "4MB"),
                "-o",
                f"$PROJECT_DIR/mock_server/http_server_root/$BOARD/{board_name}_{mcu}_firmware_{get_firmware_version()}.bin",
                "$BUILD_DIR/${PROGNAME}.elf",
            ]
        ),
        f"Building $PROJECT_DIR/mock_server/http_server_root/$BOARD/{board_name}_{mcu}_firmware_{get_firmware_version()}.bin",
    ),
)

env.AddPostAction("checkprogsize", display_manifest)

env.AddPostAction("checkprogsize", update_manifest)
