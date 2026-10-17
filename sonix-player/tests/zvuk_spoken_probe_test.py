"""Normal local API responses: probe stays metadata-only and preserves session."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading

class Handler(BaseHTTPRequestHandler):
    mode = 200
    requests = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert self.headers["X-Auth-Token"] == "fixture-session-not-real"
        assert "flacdrm" not in request["query"]
        op = request["operationName"]
        self.requests.append(op)
        chapter = {"id": "101", "__typename": "Chapter", "title": "Normal chapter", "position": 1, "duration": 90}
        episode = {"id": "201", "__typename": "Episode", "title": "Normal episode", "duration": 180}
        data = {
            "getBookChapters": {"getBooks": [{"__typename": "Book", "title": "Normal book", "fullDuration": 90, "chapters": [chapter]}]},
            "getPodcastEpisodes": {"getPodcasts": [{"__typename": "Podcast", "title": "Normal podcast", "episodes": [episode]}]},
            "spokenTypes": {"mediaContents": [{"__typename": "Chapter", "stream": {"__typename": "Stream", "high": "https://fixture.invalid/private-audio"}}, {"__typename": "Episode", "stream": {"__typename": "Stream", "mid": "https://fixture.invalid/private-audio"}}]},
            "getEpisodes": {"getEpisodes": [{**episode, "podcast": {"id": "901", "title": "Normal show"}}]},
            "search": {"search": {"books": {"items": [{"id": "28850372", "title": "Normal book", "authorNames": ["Author"]}]}, "podcasts": {"items": [{"id": "12874277", "title": "Normal podcast", "image": {"src": "https://fixture.invalid/image"}, "authors": [{"name": "Host"}]}]}}},
        }[op]
        body = json.dumps({"data": data} if self.mode == 200 else {"error": "refused"}).encode()
        self.send_response(self.mode)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

server = HTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        config = root / "device_config.ini"
        config.write_text(f"[zvuk]\nbase_url=http://127.0.0.1:{server.server_port}\n")
        session = root / "zvuk_session.ini"
        session.write_text("token=fixture-session-not-real\nname=fixture\n")
        initial = session.read_bytes()
        for status in (200, 401, 418):
            Handler.mode = status
            Handler.requests = []
            result = subprocess.run(["/tmp/zvuk_spoken_probe_host", str(config)], capture_output=True, text=True, timeout=20)
            assert session.read_bytes() == initial
            assert "fixture-session-not-real" not in result.stdout + result.stderr
            assert "https://" not in result.stdout + result.stderr
            assert result.returncode == 0 if status == 200 else result.returncode != 0
            assert len(Handler.requests) == (3 if status == 200 else 1)
            if status == 200:
                assert "count=1" in result.stdout and "type=Chapter" in result.stdout and "type=Episode" in result.stdout
                assert "has_high=yes" in result.stdout and "has_mid=yes" in result.stdout
            print(f"PASS spoken probe HTTP{status}: metadata-only, session unchanged, bounded requests")
        for mode in ("bookapi", "podcastapi", "episodesapi", "searchbookapi", "searchpodcastapi"):
            Handler.mode = 200
            Handler.requests = []
            result = subprocess.run(["/tmp/zvuk_spoken_probe_host", str(config), mode], capture_output=True, text=True, timeout=20)
            assert result.returncode == 0 and len(Handler.requests) == 1, (mode, result.stdout)
            assert session.read_bytes() == initial
            assert "fixture-session-not-real" not in result.stdout + result.stderr and "https://" not in result.stdout + result.stderr
            print(f"PASS actual API probe {mode}: single request, no secret/URL output")
        Handler.mode = 401
        Handler.requests = []
        result = subprocess.run(["/tmp/zvuk_spoken_probe_host", str(config), "bookapi"], capture_output=True, text=True, timeout=20)
        assert result.returncode != 0 and len(Handler.requests) == 1 and session.read_bytes() == initial
        print("PASS actual API refusal: normal session invalidation cannot delete the on-disk session")
finally:
    server.shutdown()
    server.server_close()
