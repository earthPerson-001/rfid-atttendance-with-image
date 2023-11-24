from http.server import HTTPServer, BaseHTTPRequestHandler
import socket
import numpy as np
import cv2
import fileinput
import sys
from pathlib import Path
import re
import uuid

LOG_RECEIVED_DATA = False

module_path = Path(__file__).resolve()
include_folder = module_path.parents[1].joinpath(
    "include"
)  # the include folder of workspace
print(f"Module path: {module_path}")
print(f"Include folder: {include_folder.resolve()}")


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
