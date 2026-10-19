"""One-shot loopback OAuth-code control; never writes the player's session.

The temporary code is entered into a local password field. Tokens remain in
memory and are discarded after the comparison. No candidate credentials
are persisted, even when a different format is returned.
"""

import argparse
import base64
import configparser
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, HTTPServer
import html
import json
from pathlib import Path
import secrets
import time
import urllib.error
import urllib.parse
import urllib.request

import probe_lossless as probe


def identity(opener, token):
    req = urllib.request.Request("https://login.yandex.ru/info?format=json", headers={
        "Authorization": "OAuth " + token,
    })
    with opener.open(req, timeout=20) as response:
        return json.loads(response.read(256 * 1024))


def file_info(opener, token, client, key, track):
    timestamp = str(int(time.time()))
    codecs = "flac,flac-mp4,aac-mp4,he-aac-mp4,mp3"
    transport = "encraw"
    message = timestamp + track + "lossless" + codecs.replace(",", "") + transport
    sign = base64.b64encode(hmac.new(
        key.encode(), message.encode(), hashlib.sha256
    ).digest()).decode().rstrip("=")
    status, body = probe.request(opener, token, client, "/get-file-info", {
        "ts": timestamp, "trackId": track, "quality": "lossless",
        "codecs": codecs, "transports": transport, "sign": sign,
    })
    info = body.get("result", {}).get("downloadInfo", {})
    return {"http_status": status, **{
        field: info.get(field) if info.get(field) in (
            "flac", "flac-mp4", "aac-mp4", "mp3", "he-aac-mp4",
            "lossless", "hq", "nq", "raw", "encraw"
        ) else None for field in ("codec", "quality", "transport")
    }}


def parse_code(body):
    fields = urllib.parse.parse_qs(body.decode("utf-8"), strict_parsing=True,
                                  max_num_fields=4)
    values = fields.get("code", [])
    if len(values) != 1 or not values[0].strip() or len(values[0]) > 2048:
        raise ValueError("Expected one nonempty code")
    return values[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", default="ingenic_")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[2] / "sonix-player/streaming-keys.ini.example")
    app_id = config.get("yandex", "client_id")
    app_secret = config.get("yandex", "client_secret")
    nonce = secrets.token_urlsafe(24)
    deadline = time.monotonic() + 300
    done = False

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def reply(self, text, status=200):
            data = text.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Frame-Options", "DENY")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path != "/" + nonce:
                self.reply("Not found", 404)
                return
            self.reply("<h1>Проверка входа Яндекс Музыки</h1>"
                       "<p>Только сравнение. Сессия R1 не заменяется.</p>"
                       "<form method='post'><label>Временный код авторизации "
                       "<input name='code' type='password' autocomplete='off' required>"
                       "</label><button>Проверить</button></form>")

        def do_POST(self):
            nonlocal done
            origin = "http://127.0.0.1:" + str(self.server.server_port)
            if self.path != "/" + nonce or self.headers.get("Origin") != origin:
                self.reply("Forbidden", 403)
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self.reply("Invalid size", 400)
                return
            if not 0 < size <= 4096:
                self.reply("Invalid size", 400)
                return
            try:
                code = parse_code(self.rfile.read(size))
            except (ValueError, UnicodeError):
                self.reply("Invalid code form", 400)
                return
            result = {"player_session_replaced": False, "candidate_saved": False}
            try:
                opener = urllib.request.build_opener(probe.NoRedirect())
                old = probe.device_token(args.adb, args.serial)
                old_identity = identity(opener, old)
                if old_identity.get("client_id") != app_id:
                    raise ValueError("Unexpected original OAuth client")
                req = urllib.request.Request("https://oauth.yandex.ru/token", data=
                    urllib.parse.urlencode({"grant_type": "authorization_code",
                        "code": code, "client_id": app_id,
                        "client_secret": app_secret}).encode(),
                    headers={"Content-Type": "application/x-www-form-urlencoded"})
                with opener.open(req, timeout=20) as response:
                    tokens = json.loads(response.read(256 * 1024))
                new = tokens["access_token"]
                new_identity = identity(opener, new)
                same = all(old_identity.get(k) == new_identity.get(k) for k in ("id", "client_id"))
                result["same_account_and_application"] = same
                result["access_token_changed"] = old != new
                if not same:
                    raise ValueError("Replacement identity differs")
                key, client, _ = probe.public_protocol()
                # Public reference from the official Lossless playlist.
                track = "38634573"
                result["original"] = file_info(opener, old, client, key, track)
                time.sleep(1.2)
                result["fresh"] = file_info(opener, new, client, key, track)
                result["original_session_still_valid"] = probe.request(
                    opener, old, client, "/account/status"
                )[0] == 200
            except urllib.error.HTTPError as error:
                result["error_http_status"] = error.code
                error.close()
            except Exception as error:
                result["error_type"] = type(error).__name__
            text = json.dumps(result, ensure_ascii=False, indent=2)
            (args.output / "oauth-code-control.json").write_text(text + "\n", encoding="utf-8")
            self.reply("<h1>Результат проверки</h1><pre>" + html.escape(text) + "</pre>")
            done = True

    server = HTTPServer(("127.0.0.1", 0), Handler)
    server.timeout = 1
    ready = {"local_url": "http://127.0.0.1:" + str(server.server_port) + "/" + nonce,
             "authorize_url": "https://oauth.yandex.ru/authorize?" + urllib.parse.urlencode({
                 "response_type": "code", "client_id": app_id, "state": nonce,
                 "scope": "music:content music:read music:write"})}
    (args.output / "oauth-control-ready.json").write_text(json.dumps(ready), encoding="utf-8")
    try:
        while not done and time.monotonic() < deadline:
            server.handle_request()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
