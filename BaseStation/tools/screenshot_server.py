#!/usr/bin/env python3
"""
Receives screenshots POSTed by lv_screenshot.c (send_screenshot_to_server()).

The ESP32 sends the raw BMP file as the POST body with
Content-Type: application/octet-stream to the path configured in
WEBSERVER (lv_screenshot.c), e.g. http://<this-host>/screenshot.

Usage:
    python screenshot_server.py [--port 80] [--path /screenshot] [--output-dir screenshots]
"""

import argparse
import datetime
import http.server
import os
import sys


class ScreenshotRequestHandler(http.server.BaseHTTPRequestHandler):
    upload_path = "/screenshot"
    output_dir = "screenshots"

    def do_POST(self):
        if self.path != self.upload_path:
            self.send_error(404, "Not found")
            return

        length = int(self.headers.get("Content-Length", 0))
        if length <= 0:
            self.send_error(400, "Empty body")
            return

        data = self.rfile.read(length)

        os.makedirs(self.output_dir, exist_ok=True)
        filename = datetime.datetime.now().strftime("screenshot_%Y%m%d_%H%M%S.bmp")
        filepath = os.path.join(self.output_dir, filename)

        with open(filepath, "wb") as f:
            f.write(data)

        print(f"Saved {filepath} ({length} bytes)")

        response_body = b"OK"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)

    def log_message(self, fmt, *args):
        # Keep the default logging (client address + request line) but via print()
        # so it interleaves cleanly with the "Saved ..." messages above.
        print(f"{self.client_address[0]} - {fmt % args}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080, help="Port to listen on (default: 80)")
    parser.add_argument("--path", default="/screenshot", help="Upload path to accept (default: /screenshot)")
    parser.add_argument("--output-dir", default="screenshots", help="Directory to save screenshots to")
    args = parser.parse_args()

    ScreenshotRequestHandler.upload_path = args.path
    ScreenshotRequestHandler.output_dir = args.output_dir

    server = http.server.HTTPServer(("0.0.0.0", args.port), ScreenshotRequestHandler)
    print(f"Listening on 0.0.0.0:{args.port}, accepting POST {args.path}, saving to '{args.output_dir}/'")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped")
        server.server_close()
        sys.exit(0)


if __name__ == "__main__":
    main()
