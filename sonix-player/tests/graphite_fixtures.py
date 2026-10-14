#!/usr/bin/env python3
"""Generate disposable audio and artwork for the native UI runner."""
from pathlib import Path
import subprocess
import sys
import wave
import shutil
import zipfile
from PIL import Image, ImageDraw

root = Path(sys.argv[1])
card = root / "card" / "Graphite"
card.mkdir(parents=True, exist_ok=True)
image = Image.new("RGB", (320, 320), (33, 37, 46))
draw = ImageDraw.Draw(image)
draw.rectangle((32, 32, 144, 288), fill=(183, 117, 78))
draw.rectangle((160, 32, 288, 288), fill=(67, 101, 122))
image.save(root / "cover.jpg")
wave_path = root / "silence.wav"
with wave.open(str(wave_path), "wb") as audio:
    audio.setnchannels(2)
    audio.setsampwidth(2)
    audio.setframerate(44100)
    audio.writeframes(bytes(44100 * 4 * 60))
for filename, title, album, year in [
    ("Time", "Time", "The Dark Side of the Moon", "1973"),
    ("Echoes", "Echoes - Live at Wembley, 1974", "Meddle", "1971"),
]:
    output = card / (filename + ".flac")
    subprocess.run([
        "flac", "--silent", "--force", f"--tag=TITLE={title}",
        "--tag=ARTIST=Pink Floyd", f"--tag=ALBUM={album}",
        f"--tag=DATE={year}", "--tag=GENRE=Rock", f"--output-name={output}", str(wave_path),
    ], check=True)

books = root / "card" / "Ebook"
books.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(books / "Graphite.epub", "w") as book:
    book.writestr("mimetype", "application/epub+zip")
    book.writestr("META-INF/container.xml", '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    book.writestr("book.opf", '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Graphite reading check</dc:title><dc:creator>UI audit</dc:creator><dc:identifier id="id">graphite-test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="chapter"/></spine></package>')
    book.writestr("chapter.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Reading check</title></head><body><h1>One quiet evening</h1>' + '<p>The music stopped, and the room became quiet. This short fixture checks reading controls, line wrapping and page navigation.</p>' * 20 + '</body></html>')
subprocess.run(["metaflac", f"--import-picture-from={root / 'cover.jpg'}", str(card / "Echoes.flac")], check=True)
for number in range(1, 13):
    track = card / f"Audit-{number:02}.flac"
    shutil.copy2(card / "Echoes.flac", track)
    subprocess.run([
        "metaflac", "--remove-tag=TITLE", "--remove-tag=ARTIST", "--remove-tag=ALBUM",
        f"--set-tag=TITLE=Artwork check {number:02}", "--set-tag=ARTIST=UI audit",
        "--set-tag=ALBUM=Artwork audit", f"--set-tag=TRACKNUMBER={number}", str(track)
    ], check=True)
