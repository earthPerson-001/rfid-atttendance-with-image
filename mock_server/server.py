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
# don't expose to LAN and setup for http connection instead of https
# this is done to use some other services like ngrok to expose it as https
# and self signing certificates causes problem with FQDN(Fully Qualified Domain Name) or CN(Common Name)
SERVE_ON_LOCALHOST = False

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

if not SERVE_ON_LOCALHOST:
    server_cert = (
        "-----BEGIN CERTIFICATE-----\n"
        "MIIGKTCCBBGgAwIBAgIUEh1Sn1zXu1nk3KVH/E4CRQep7EswDQYJKoZIhvcNAQEL\n"
        "BQAwgaMxCzAJBgNVBAYTAk5QMRAwDgYDVQQIDAdCYWdtYXRpMRIwEAYDVQQHDAlE\n"
        "aHVsaWtoZWwxGTAXBgNVBAoMEENvcHkgUGFzdGVycyBMdGQxEDAOBgNVBAsMB0lu\n"
        "dGVybnMxHTAbBgNVBAMMFGVzcDMybG9jYWxzZXJ2ZXIuY29tMSIwIAYJKoZIhvcN\n"
        "AQkBFhNub3RhdmFsaWRAZW1haWwuY29tMB4XDTIzMTEyNTIzMjUwMloXDTI0MTEy\n"
        "NDIzMjUwMlowgaMxCzAJBgNVBAYTAk5QMRAwDgYDVQQIDAdCYWdtYXRpMRIwEAYD\n"
        "VQQHDAlEaHVsaWtoZWwxGTAXBgNVBAoMEENvcHkgUGFzdGVycyBMdGQxEDAOBgNV\n"
        "BAsMB0ludGVybnMxHTAbBgNVBAMMFGVzcDMybG9jYWxzZXJ2ZXIuY29tMSIwIAYJ\n"
        "KoZIhvcNAQkBFhNub3RhdmFsaWRAZW1haWwuY29tMIICIjANBgkqhkiG9w0BAQEF\n"
        "AAOCAg8AMIICCgKCAgEArfN5hNlndpuODdxRX0UhlsnlGY9pFBtf14AnfSavwCd3\n"
        "gzwx0T/i7Ou/OnuPqp2QbxWZE9IRss2ayP+bblTvXfzYncvAWhi8++AOwgYeNBTq\n"
        "yohS6gmcAO4BrHX2z2IwGOYRyrGDZp+/VyFPcdbEbDDc/x9b5IcbZKc+G8ADpxpL\n"
        "Mio8NfNX2g2TecYS2lv3VCwWKh2C9GXbaVld2nclPXl7airWa5C9wRvuCvwAATU5\n"
        "CyCcV7Rup7JBhxGcp6CjVjiIK8nxnwSmxH7CoT/+bimXBJhdNyroIRO2qeXGUKQe\n"
        "WU/zDUjPKeWtrlXVpp6M9qjYUlzjJam1atBHgwAU4XfIQ3LzZ2OU9e71PjWQJ9Ue\n"
        "/VL8pgQTRk4PMyXx6WcJ0JZYkbMTWpFLrpgO1gm0A8qpJ6nhvSWKEFt0Jmo+L0gc\n"
        "lZWVE8VL3jNSi32UB/4yTfU306FX2pZfCwic4kuK4RX0IPQzcnbXe+p3liIrizkp\n"
        "GQ8N5vqAGeLYyZOrO6fn9o2Byvvi0jImAe64ZjxRAt7P+mSEAalHtkeREjTEmC3W\n"
        "Pq5E2mYlMPUGcU9aVh964yY9EvPGuhWtTW02IryOTIvGoBEBVqUAhX8sLea6AvNt\n"
        "WhgvhfqIcB9thbMACA/pKOAcEeMzPGv8eupbiT8J7yCnBXYFjg09beU4eW/hFzkC\n"
        "AwEAAaNTMFEwHQYDVR0OBBYEFK1upvP1QwLK4ykcWnhTkyl0JhdrMB8GA1UdIwQY\n"
        "MBaAFK1upvP1QwLK4ykcWnhTkyl0JhdrMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZI\n"
        "hvcNAQELBQADggIBAIPaIubLaxxn5tiiZ4Ho8daM7Y35rxummVh6tRhq/QnnzZrm\n"
        "GXu69PMfKIRTkxvRWqog0LlqdsCuolSKJkQkgByKux2qi+0I1oPBgqYi9E0WNe3u\n"
        "2v9lKIFMuMy2ZRSzaGwCUxyTOtnObyWfeJA/Vv4PE+WBmGaP+k0OsEbncX/DtN88\n"
        "y4qWuI5g0FnHNYg51Y8FPXyp5b+cD04cNXUtT1XeaTMcopzduhLFZyFXU1d0i5b9\n"
        "CoHZ6UH3VdO3rKqr4tmTMmRYNkq6LUUv0wFmdK9dx/0+giqILYAt1gktncVOJxxB\n"
        "Czgb+kw2WkIsdiqfcSMlbH/GeEKRnCe4C/uOruUQi5lgvB8sbfeBNtcmbuOWS4f6\n"
        "MhTZ2tcdXIepU6fooN06L+8iNg5uus/C5x0sbZcp+w3GfQve5nI2FKcVdG6MYb/k\n"
        "a57zBUfJhnerppnhiODrpQJUnjsvqvIoavweWXJoHqDH4fhzMkZReSRa+t4+eCFT\n"
        "gkBlngHyuPrmmfcx79NUq9X5z2Z+jO5gfl25h9aU5MT714xvVh2QgCKLn7dS9vaV\n"
        "1Di3G/Wahbhynbef04FECOF4hwA8AcA6Auu5aPGL5Ki6wHUtNReM6x8BG2QwOIP8\n"
        "yraUfiF7dJ6Y5emzf7z8EWidqsvy/9v+Rmk5S7DVdRAAaVt2K8oLPIg0CDSU\n"
        "-----END CERTIFICATE-----\n"
    )

    server_key = (
        "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
        "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\n"
        "DR3uPh0lqd9WNnMimJrkUg==\n"
        "-----END ENCRYPTED PRIVATE KEY-----\n"
    )

    SSL_PASSWORD = "esp32password"


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
    def do_GET(self):
        super().do_GET()

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
    if not SERVE_ON_LOCALHOST:
        (cert_file, key_file) = setup_certificate_and_key(ota_folder)
        ssl_ctx = ssl.SSLContext(protocol=ssl.PROTOCOL_TLS_SERVER)
        ssl_ctx.load_cert_chain(
            certfile=cert_file, keyfile=key_file, password=SSL_PASSWORD
        )

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

    if SERVE_ON_LOCALHOST:
        address = "localhost"
    else:
        address = get_ip()
    port = 8000
    header_filename = "globals.h"

    if not SERVE_ON_LOCALHOST:
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
    except KeyboardInterrupt:
        if httpd is not None:
            httpd.shutdown()
        print("Exiting because of KeyboardInterrupt:")
        cv2.destroyAllWindows()
