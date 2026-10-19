"""Bounded read-only format probe using the owner's connected R1 session.

No playback, token refresh, audio download, or device writes. Output contains
only allowlisted metadata. Application protocol constants are read as AST
literals from the referenced public client, never executed or persisted.
"""

import argparse
import ast
import base64
import hashlib
import hmac
import json
from pathlib import Path
import re
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request


PROTOCOL_URL = (
    "https://raw.githubusercontent.com/MarshalX/yandex-music-api/main/"
    "yandex_music/utils/sign_request.py"
)
API = "https://api.music.yandex.net"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def public_protocol():
    with urllib.request.urlopen(PROTOCOL_URL, timeout=20) as response:
        source = response.read(65536)
    values = {}
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in (
                    "DESKTOP_SIGN_KEY", "DESKTOP_CLIENT"
                ):
                    values[target.id] = node.value.value
    key, client = values["DESKTOP_SIGN_KEY"], values["DESKTOP_CLIENT"]
    if not isinstance(key, str) or not re.fullmatch(r"[A-Za-z0-9]{10,128}", key):
        raise ValueError("Unexpected public signing-key format")
    if not isinstance(client, str) or not re.fullmatch(
        r"YandexMusicDesktopAppWindows/[0-9.]+", client
    ):
        raise ValueError("Unexpected public client format")
    return key, client, hashlib.sha256(source).hexdigest()


def device_token(adb, serial):
    result = subprocess.run(
        [adb, "-s", serial, "shell", "cat /data/yandex_session.ini"],
        capture_output=True, timeout=20,
    )
    token = next(
        (line.split("=", 1)[1] for line in result.stdout.decode("utf-8").splitlines()
         if line.startswith("access_token=")), "",
    )
    if not token or len(token) > 4096 or "\r" in token or "\n" in token:
        raise ValueError("No usable device access token")
    return token


def request(opener, token, client, endpoint, params=None):
    if endpoint not in ("/account/status", "/get-file-info", "/search") and not (
        re.fullmatch(r"/tracks/[0-9]+(?::[0-9]+)?/download-info", endpoint)
        or re.fullmatch(r"/users/yandexmusic/playlists/[0-9]+", endpoint)
    ):
        raise ValueError("Endpoint not allowlisted")
    url = API + endpoint
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={
        "Authorization": "OAuth " + token,
        "X-Yandex-Music-Client": client,
        "User-Agent": "Yandex-Music-API",
    })
    try:
        with opener.open(req, timeout=25) as response:
            body = response.read(2 * 1024 * 1024)
            return response.status, json.loads(body)
    except urllib.error.HTTPError as error:
        status = error.code
        error.close()
        return status, {}
    except (urllib.error.URLError, TimeoutError, ValueError):
        return 0, {}


def probe(args):
    key, client, source_hash = public_protocol()
    token = device_token(args.adb, args.serial)
    opener = urllib.request.build_opener(NoRedirect())
    status, account = request(opener, token, client, "/account/status")
    plus = account.get("result", {}).get("plus", {}).get("hasPlus")
    summary = {
        "protocol_source_sha256": source_hash,
        "client": client,
        "track_sha256": hashlib.sha256(args.track.encode()).hexdigest(),
        "account_status_http": status,
        "has_plus": plus if isinstance(plus, bool) else None,
        "requests": [],
    }
    if status != 200:
        return summary
    variants = [
        ("firmware-codecs-encraw", "flac,flac-mp4,aac-mp4,he-aac-mp4,mp3", "encraw"),
        ("flac-only-encraw", "flac,flac-mp4", "encraw"),
        ("firmware-codecs-raw", "flac,flac-mp4,aac-mp4,he-aac-mp4,mp3", "raw"),
    ]
    for name, codecs, transport in variants:
        time.sleep(1.2)
        timestamp = str(int(time.time()))
        message = timestamp + args.track + "lossless" + codecs.replace(",", "") + transport
        signature = base64.b64encode(hmac.new(
            key.encode(), message.encode(), hashlib.sha256
        ).digest()).decode().rstrip("=")
        status, body = request(opener, token, client, "/get-file-info", {
            "ts": timestamp, "trackId": args.track, "quality": "lossless",
            "codecs": codecs, "transports": transport, "sign": signature,
        })
        info = body.get("result", {}).get("downloadInfo", {})
        record = {"variant": name, "http_status": status}
        for field in ("codec", "quality", "transport"):
            value = info.get(field)
            record[field] = value if isinstance(value, str) and re.fullmatch(
                r"[A-Za-z0-9_?+.-]{1,32}", value
            ) else None
        bitrate = info.get("bitrate")
        record["bitrate_kbps"] = bitrate if isinstance(bitrate, int) else None
        record["file_url_present"] = bool(info.get("url") or info.get("urls"))
        record["file_key_present"] = bool(info.get("key"))
        summary["requests"].append(record)
        if status in (0, 401, 403, 429) or 300 <= status < 400:
            break
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", default="ingenic_")
    parser.add_argument("--track", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+(?::[0-9]+)?", args.track):
        parser.error("Track must be a numeric service ID")
    try:
        summary = probe(args)
    except Exception as error:
        # Exceptions may contain secret URLs or values; print only their class.
        summary = {"probe_error_type": type(error).__name__}
    text = json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    if "probe_error_type" in summary or summary.get("account_status_http") != 200:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
