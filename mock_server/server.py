import fileinput
import os
import re
import socket
import ssl
import sys
import time
import uuid
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from threading import Thread

import cv2
import numpy as np

LOG_RECEIVED_DATA = False

module_path = Path(__file__).resolve()
include_folder = module_path.parents[1].joinpath(
    "include"
)  # the include folder of workspace
ota_folder = module_path.parents[0].joinpath("http_server_root")

# keeping a http daemon instance for closing
httpd = None

print(f"Module path: {module_path}")
print(f"Include folder: {include_folder.resolve()}")
print(f"Ota Folder: {ota_folder.resolve()}")

server_cert = "-----BEGIN CERTIFICATE-----\n"\
    "MIIGCTCCA/GgAwIBAgIUcKWns9NiXfLkpz7CYRvul08pXSAwDQYJKoZIhvcNAQEL\n"\
    "BQAwgZMxCzAJBgNVBAYTAk5QMRAwDgYDVQQIDAdCYWdtYXRpMRIwEAYDVQQHDAlE\n"\
    "aHVsaWtoZWwxEDAOBgNVBAoMB09tbmVjYWwxDzANBgNVBAsMBkludGVybjEPMA0G\n"\
    "A1UEAwwGQmlzaGFsMSowKAYJKoZIhvcNAQkBFhtuZXVwYW5lYmlzaGFsMjAwMUBn\n"\
    "bWFpbC5jb20wHhcNMjMxMTI1MTQ0NjAzWhcNMjQxMTI0MTQ0NjAzWjCBkzELMAkG\n"\
    "A1UEBhMCTlAxEDAOBgNVBAgMB0JhZ21hdGkxEjAQBgNVBAcMCURodWxpa2hlbDEQ\n"\
    "MA4GA1UECgwHT21uZWNhbDEPMA0GA1UECwwGSW50ZXJuMQ8wDQYDVQQDDAZCaXNo\n"\
    "YWwxKjAoBgkqhkiG9w0BCQEWG25ldXBhbmViaXNoYWwyMDAxQGdtYWlsLmNvbTCC\n"\
    "AiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAOpjjLslc4bgmP/DOUrfOg/s\n"\
    "aF47UiV/Vz0ydRFyKM/Rwq1+kpsY1E0mWco8Q5T5cmPcqhKJSkR6wMQAzoBrO3Sk\n"\
    "PKiabLTZhn6DIuMqgm49eF8ZnPoIgD8nxG4Yj+TXPiyBoUVqHxhCd0WlXBA8qnI+\n"\
    "8XjKnu3PtzQ/nnoMBE1jL9QPOr9Vtiywku3+CC9sT8UWnS4lIC8ZdUYJJTPRhvJU\n"\
    "RgOr/2pbBwd0F+Tm1Zbek3iz8oPHOCGTiqYUgPDhCSCHHFTSoJWvy1dmKEuzX8dm\n"\
    "ieONJ0AVE3BcXYgKTcHj0PhcExRV8TF+Oc7Q7gseHDU72vdeJ31e4kymoaLyHQ4H\n"\
    "5DWoPN3JJZDAJrRmbgVexcvkPasA9mJl28OzSXFinDM1QR3NDjVbL2qYdU7845MR\n"\
    "+qD+OxmKQRQVl4GJM6CnnlIHmSaUN24NmbIViLuH8KhLvHOd748j554cAoU23fyr\n"\
    "9uV/uCK9Nsm3ZNMrPpXAe/4xmVm351NbK6YBjYDFxIrBjXyXGSXvYfcU/AvH6j3h\n"\
    "qgbR0DiW6FhGFpJ5SaMvOl/3ekSV4iY6YsKuYOEUNn1x2DI5lIeRbEwSz9CTbzkA\n"\
    "LD8COlyIiwPrYHEMgpodzhxr2sg0/Vu6uzozKI5RuDEToCc4BxGnnyvkmSviyMNN\n"\
    "nxMmfXrG3Nh7bztxKTPRAgMBAAGjUzBRMB0GA1UdDgQWBBS0FYhj/IqXSOmiwEEw\n"\
    "woYO87RbvDAfBgNVHSMEGDAWgBS0FYhj/IqXSOmiwEEwwoYO87RbvDAPBgNVHRMB\n"\
    "Af8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4ICAQDMsR83QQ6nNlIPq4GBu2Tf9LvI\n"\
    "Fl+XPonEW3YpBYqxsZZSBBtLzALfw2BfL0tewENBUkBUz+Fi0GAjCMxhiOtk3SXK\n"\
    "VibZkfgtS2Jfe8EK9LMLccmP4L+92Mhfztjt91fJEJOnJhNmSnvQ+XlRFohvUds3\n"\
    "W4W1FC+plbn3XKr4//p/EGc1lfSMc2+NksFc0rCItZsqSImPo4hn8AQGrKEuKRs8\n"\
    "08sRwfyVJ+pLLOOu7hQPf9vdQHuErr1rK5wzhoW30l+XuuGHeHuea3osfhAKSUFF\n"\
    "y0FY6CovM6LyeXonWBw0+hcGumWRiqbelRZwpOz+E7ZKg83AQOTzQBATJqb7j48t\n"\
    "Vjo050kAvRZ344MIfCYH5DyLFjQthTLuxESqO4Mpv+I8+EdZokYY18/eFy22IuNW\n"\
    "bc7reBoNpmJ11Y98+7OppCdXfia0FsFcLC2sDiV9e6KpJrVTUDOc5qE+SVsBLx4Q\n"\
    "mVovBBs+3J24nwk8Gd5L9CGz8lMRzpCEdK9vf+CCNDxVee59tLpxLXWXht7+LS0L\n"\
    "QBwyHjuwpbWZ1Ql4eqoLb1Hl5nRKeqPMVv99E8A7UfB0ShCRVeo51lWXx0Ud3nik\n"\
    "qCXLhKklHc4ov5Jk3e/5KpbKIZxw3Gya1brNpOGWR6ES+fZ/HKyiwXozlNf7+NpY\n"\
    "8fcl+QPpLASFn/UO5Q==\n"\
    "-----END CERTIFICATE-----\n"


server_key = "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"\
    "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\n"\
    "4oKkBzLTwgfz0uOvv+KjTfgeFRXzFLkW\n"\
    "-----END ENCRYPTED PRIVATE KEY-----\n"



def sanitize_filename(filename: str) -> str:
    """
    Replaces all forbidden chars with '' and removes unnecessary whitespaces
    If, after sanitization, the given filename is empty, the function will return 'file_[UUID][ext]'

    :param filename: filename to be sanitized
    :return: sanitized filename
    """
    chars = ["\\", "/", ":", "*", "?", '"', "<", ">", "|"]

    filename = filename.translate({ord(x): "" for x in chars}).strip()
    name = re.sub(r"\.[^.]+$", "", filename)
    extension = re.search(r"(\.[^.]+$)", filename)
    extension = extension.group(1) if extension else ""

    return filename if name else f"file_{uuid.uuid4().hex}{extension}"


class MyHandler(SimpleHTTPRequestHandler):
    def do_POST(self):
        self.log_request()

        # common across all paths

        images: list[np.ndarray[np.uint8]] = []
        response = 400
        response_msg = ""

        rfid_serial_number = int(
            self.headers.get("rfid-serial-number", 0)
        )  # set rfid to 0 if fail
        content_type = self.headers.get("Content-Type").__str__()

        if rfid_serial_number == 0:
            response = 417
            response_msg = f"Expected key `rfid-serial-number` in the header"
        elif (
            "image/jpeg" in self.headers.get("Content-Type")
            and "Content-Length" in self.headers
        ):
            data = self.rfile.read(int(self.headers["Content-Length"]))
            if LOG_RECEIVED_DATA:
                self.log_message(f"Trying to decode the image.")
            np_arr = np.frombuffer(data, np.uint8)
            images.append(cv2.imdecode(np_arr, cv2.IMREAD_UNCHANGED))

            response = 200

            # respond with received file size and serial number
            response_msg = f"Got image for rfid tag {rfid_serial_number}"
        elif content_type.find("multipart/form-data") > -1:
            response = 200
            if LOG_RECEIVED_DATA:
                self.log_message(f"Response {response}")

            # extract boundary from headers
            boundary = re.search(
                f"boundary=([^;]+)", self.headers["Content-Type"]
            ).group(1)

            # handling post requests sent all at once
            if "Content-Length" in self.headers:
                # read all bytes (headers included)
                # 'readlines()' hangs the script because it needs the EOF character to stop,
                # even if you specify how many bytes to read
                # 'file.read(nbytes).splitlines(True)' does the trick because 'read()' reads 'nbytes' bytes
                # and 'splitlines(True)' splits the file into lines and retains the newline character
                data = self.rfile.read(int(self.headers["Content-Length"])).splitlines(
                    True
                )
            # handling post request sent in chunks
            elif "chunked" in self.headers.get("Transfer-Encoding", ""):
                # the chunk buffer
                data = "".encode()
                while True:
                    line = self.rfile.readline()
                    if LOG_RECEIVED_DATA:
                        self.log_message(str(line))
                    line = line.strip().strip()  # in the form <length-hex>\r\n

                    # skip if only line endings are provided
                    if len(line) == 0:
                        continue

                    chunk_length = int(line, 16)  # in the hexadecimal format

                    if chunk_length != 0:
                        chunk = self.rfile.read(chunk_length)
                        # print(str(chunk))  # logging causes error
                        data += chunk

                    # Each chunk is followed by an additional empty newline
                    # that we have to consume.
                    if LOG_RECEIVED_DATA:
                        self.log_message(str(self.rfile.readline()))
                    else:
                        self.rfile.readline()

                    # Finally, a chunk size of 0 is an end indication
                    if chunk_length == 0:
                        break
                data = data.splitlines(True)

            if LOG_RECEIVED_DATA:
                self.log_message("Received data: \n %s", data)
            # find all filenames
            filenames = re.findall(f'{boundary}.+?filename="(.+?)"', str(data))

            if not filenames:
                return False, "couldn't find file name(s)."

            filenames = [sanitize_filename(filename) for filename in filenames]

            # find all boundary occurrences in data
            boundary_indices = list(
                (i for i, line in enumerate(data) if re.search(boundary, str(line)))
            )

            # append images
            for i in range(len(filenames)):
                # remove file headers
                file_data = data[(boundary_indices[i] + 3) : boundary_indices[i + 1]]

                # join list of bytes into bytestring
                file_data = b"".join(file_data)

                if LOG_RECEIVED_DATA:
                    self.log_message(f"Trying to decode the image.")
                np_arr = np.frombuffer(file_data, np.uint8)
                images.append(cv2.imdecode(np_arr, cv2.IMREAD_UNCHANGED))

                # respond with received file size and serial number
                response_msg = f"Got image for rfid tag {rfid_serial_number}"

        else:
            response = 415
            response_msg = f"Unsupported meadia type {content_type}, expected image/jpeg along with Content-Length or multipart/form-data"

        if LOG_RECEIVED_DATA:
            self.log_message(f"Response {response}")
        self.send_response(response)
        self.end_headers()
        self.wfile.write(response_msg.encode())

        if len(images) > 0:
            for i, image in enumerate(images):
                display_image_and_wait(image, f"{rfid_serial_number}_{i}")


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


def initialize_for_ota():
    os.chdir(ota_folder.resolve())
    (cert_file, key_file) = setup_certificate_and_key(ota_folder)
    ssl_ctx = ssl.SSLContext(protocol=ssl.PROTOCOL_TLS_SERVER)
    ssl_ctx.load_cert_chain(certfile=cert_file, keyfile=key_file)

    if httpd is None:
        print("Http daemon hasn't been initialized")
        return

    httpd.socket = ssl_ctx.wrap_socket(httpd.socket, server_side=True)


def setup_certificate_and_key(ota_img_dir):
    """
    Returns
    -------
    (server_cert_file, server_key_file)

    """
    cert_file = os.path.join(ota_img_dir, "server_cert.pem")
    with open(cert_file, "w+") as sf:
        sf.write(server_cert)

    key_file = os.path.join(ota_img_dir, "server_key.pem")
    with open(key_file, "w+") as kf:
        kf.write(server_key)

    return (cert_file, key_file)


def start_server(address: str = "0.0.0.0", port: int = 8000):
    global httpd

    print(f"Opening http server on {address}:{port}")
    print(f"Setting up ota dir on {ota_folder.resolve()}")
    httpd = HTTPServer((address, port), MyHandler)
    initialize_for_ota()
    print(f"Serving Forever. Press Ctrl + C inorder to exit.")
    httpd.serve_forever()


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

    other_thread = Thread(target=start_server, args=(address, port))
    other_thread.daemon = True
    other_thread.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt as ki:
        if httpd is not None:
            httpd.shutdown()
        print("Exiting because of KeyboardInterrupt: " + str(ki))
        cv2.destroyAllWindows()
