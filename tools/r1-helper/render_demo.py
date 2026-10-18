"""Build a captioned publication demo from genuine R1 capture files, offline."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from screencast import export_frames

SCENES = [
    ("00-main-idle", 2.0, "Sonix Player rekkod-mod · Graphite 2\nРеальный HiBy R1 · alpha19"),
    ("03-hold-native", 4.5, "Удержание плитки — меню\nБыстрый доступ к настройке главной"),
    ("04-open-editor", 3.5, "Главная под себя\nСвои ярлыки и их порядок"),
    ("05-edge-back", 3.5, "Свайп от левого края — назад"),
    ("06-shade-down", 3.5, "Потянуть сверху — шторка\nБыстрые настройки в общей палитре"),
    ("07-player-open", 3.5, "Тап по мини-плееру\nПолный экран воспроизведения"),
    ("09-player-down", 3.5, "Потянуть плеер вниз\nВернуться к открытой странице"),
    ("08-player-up-home", 3.5, "Свайп снизу вверх\nИз полного плеера — на главную"),
    ("10-row-next-preview", 3.5, "Вправо по строке — следующим\nПредпросмотр без изменения очереди"),
    ("11-row-favourite", 2.2, "Влево по строке — избранное\nПоставить или снять лайк"),
    ("00-main-idle", 1.4, "Графит 2\nМузыка, книги и управление жестами"),
]


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def build(source: Path, output: Path, font: Path):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg: raise ValueError("FFmpeg is required")
    if not font.is_file(): raise ValueError("Provide a font with Cyrillic support using --font")
    output.mkdir(parents=True, exist_ok=True)
    # The installed font is used only in this local build directory, never shipped.
    shutil.copyfile(font, output / "font.ttf")
    manifest = {"device": "HiBy R1", "firmware": "alpha19", "source": "native framebuffer captures only",
                "audio": False, "edited": "Individual real clips concatenated; explanatory captions outside the 480x800 screen. Original recordings preserved.",
                "scenes": []}
    for index, (name, duration, caption) in enumerate(SCENES):
        capture = source / name / "capture.snx"
        if not capture.exists(): capture = source / (name + ".snx")
        folder = output / name
        folder.mkdir(exist_ok=True)
        count, _, _ = export_frames(capture.read_bytes(), folder)
        filters = "fps=10,pad=480:896:0:0:color=0x0c1116"
        lines = caption.splitlines()
        for line_number, line in enumerate(lines):
            caption_file = f"caption-{index:02}-{line_number}.txt"
            (output / caption_file).write_text(line, encoding="utf-8")
            y = 832 if len(lines) == 1 else (818 if line_number == 0 else 854)
            size = 22 if line_number == 0 else 20
            color = "white" if line_number == 0 else "0xb8c3cc"
            filters += (f",drawtext=fontfile=font.ttf:textfile={caption_file}:fontsize={size}:"
                        f"fontcolor={color}:x=(w-tw)/2:y={y}")
        target = f"scene-{index:02}.mp4"
        run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-threads", "1", "-filter_threads", "1", "-f", "concat", "-safe", "0", "-i",
            folder.resolve() / "frames.ffconcat", "-t", str(duration), "-vf", filters,
            "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-threads", "2",
            "-pix_fmt", "yuv420p", "-movflags", "+faststart", target, cwd=output)
        entry = {"name": name, "caption": caption, "duration": duration, "frames": count}
        manifest["scenes"].append(entry)
        print("Rendered", name, flush=True)
    (output / "scenes.ffconcat").write_text("ffconcat version 1.0\n" + "".join(
        f"file 'scene-{i:02}.mp4'\n" for i in range(len(SCENES))), encoding="utf-8")
    stem = "r1-graphite2-gestures-alpha19"
    run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i",
        "scenes.ffconcat", "-c", "copy", "-movflags", "+faststart", stem + ".mp4", cwd=output)
    run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-threads", "1", "-filter_threads", "1", "-i", stem + ".mp4", "-vf",
        "fps=8,scale=360:-2:flags=lanczos", "-an", "-c:v", "libwebp_anim", "-quality", "75",
        "-compression_level", "4", "-loop", "0", "-threads", "2", stem + ".webp", cwd=output)
    run(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-threads", "1", "-filter_complex_threads", "1", "-i", stem + ".mp4", "-filter_complex",
        "fps=8,scale=320:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];"
        "[b][p]paletteuse=dither=bayer:bayer_scale=3", "-an", "-loop", "0", "-threads", "2", stem + ".gif", cwd=output)
    (output / "demo.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("READY", output / (stem + ".mp4"), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/segoeui.ttf"))
    args = parser.parse_args()
    build(args.source.resolve(), args.output.resolve(), args.font)
