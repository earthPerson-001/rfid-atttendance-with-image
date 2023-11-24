from http.server import HTTPServer, BaseHTTPRequestHandler
import socket
import numpy as np
import cv2
import fileinput
import sys
from pathlib import Path
import re
import uuid

module_path = Path(__file__).resolve()
include_folder = module_path.parents[1].joinpath(
    "include"
)  # the include folder of workspace
print(f"Module path: {module_path}")
print(f"Include folder: {include_folder.resolve()}")

class MyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.log_request()
        # send 200 response
        self.send_response(200)
        # add our own custom header
        self.send_header("mock_header_key", "mock_header_value")
        # send response headers
        self.end_headers()
        # send the body of the response
        self.wfile.write(bytes("Hello to Esp32 from server.", "utf-8"))

    def do_POST(self):
        self.log_request()
        rfid_serial_number = int(
            self.headers.get("rfid-serial-number", 0)
        )  # set rfid to 0 if fail
        content_type = self.headers.get("Content-Type").__str__()
        if rfid_serial_number == 0:
            response = 417
            self.log_message(f"Response {response}")
            self.send_response(response)  # expectation failed
            self.wfile.write(
                f"Expected key `rfid-serial-number` in the header".encode()
            )
        elif "image/jpeg" in self.headers.get("Content-Type") and "Content-Length" in self.headers:
                data = self.rfile.read(int(self.headers["Content-Length"]))
                self.log_message(f"Trying to decode the image.")
                np_arr = np.frombuffer(data, np.uint8)
                cv_img = cv2.imdecode(np_arr, cv2.IMREAD_UNCHANGED)

                response = 200
                self.log_message(f"Response {response}")
                self.send_response(response)
                self.end_headers()

                # respond with received file size and serial number
                self.wfile.write(
                    f"Got image for rfid tag {rfid_serial_number}".encode()
                )

                display_image_and_wait(cv_img, f"image{rfid_serial_number}")

        else:
            response = 415
            self.log_message(f"Response {response}")
            self.send_response(response)  # unsupported media type
            self.wfile.write(
                f"Unsupported meadia type {content_type}, expected image/jpeg along with Content-Length".encode()
            )


def get_ip():
    """
    Get the local ip
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0)
    try:
        # doesn't even have to be reachable
        s.connect(("10.254.254.254", 1))
        IP = s.getsockname()[0]
    except Exception:
        IP = "127.0.0.1"
    finally:
        s.close()
    return IP


def set_server_address(file, address):
    """
    Search for `#define SERVER_ADDRESS ` in the given file and replace the line with the provided address
    """
    string_to_find = "#define SERVER_ADDRESS "
    found = False
    msg = ""
    for line in fileinput.input(file, inplace=1):
        if line.find(string_to_find) > -1:
            found = True
            msg = f"found the string {string_to_find}\n Replacing {line} with "
            new_line = f'#define SERVER_ADDRESS "{address}" //testing locally \n'
            msg = msg + new_line + f"in file {file}"
            sys.stdout.write(new_line)

            if line == new_line:
                msg = ""
        else:
            sys.stdout.write(line)

    if not found:
        msg = f"Couldn't find the string {string_to_find}"

    print(msg)


def display_image_and_wait(img: np.ndarray, img_name: str):
    """
    Displays image waits for `n` or `q` key presses. \n
    n => close the current window \n
    q => close all the windows \n

    Parameters
    ---------
    img_name: The window name
    """
    while True:
        cv2.imshow(img_name, img)

        k = cv2.waitKey(1)

        if k == ord("n"):  # destroy only the current window on pressing key 'n'
            cv2.destroyWindow(img_name)
            break

        if k == ord("q"):  # destroy all the open window on pressing key 'q'
            cv2.destroyAllWindows()
            break


if __name__ == "__main__":
    """
    Opening on 0.0.0.0 and make it accessible across the local network.
    Log the local ip.
    """

    address = get_ip()
    port = 8000
    header_filename = "globals.h"

    # replacing the address in globals.h
    set_server_address(
        include_folder.joinpath(header_filename).resolve(), f"{address}:{port}"
    )
    print(f"Opening http server on {address}:{port}")
    httpd = HTTPServer(("0.0.0.0", port), MyHandler)
    httpd.serve_forever()

    # cleanup
    cv2.destroyAllWindows()
