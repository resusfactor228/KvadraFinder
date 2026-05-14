# KvadraFinder

A C++17 daemon that periodically scans a directory for multimedia files (audio, video, images) and:

1. Writes the results to the file `$HOME/.media_files` in JSON format.
2. Returns the same JSON via HTTP: `GET http://localhost:1234/media_files`.

---

## Requirements

| Tool | Minimum Version |
|------------|-------------------|
| GCC / Clang | C++17 support (GCC ≥ 8, Clang ≥ 7) |
| CMake | 3.16 |
| Linux | any modern distribution |

---

## Build

```bash
# Clone / unzip the project
cd KvadraFinder

# Create a build directory and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The executable will appear at `build/kvadra_finder`.

---

## Launch

### Simple launch (default settings)

```bash
./build/kvadra_finder
```

Default parameters:
- directory to scan — `$HOME`
- scan interval — 60 seconds
- HTTP port — 1234
- output file — `$HOME/.media_files`

### With custom parameters

```bash
./build/kvadra_finder \
--dir /home/user/Media \
--interval 30 \
--port 8080 \
--output /tmp/media.json
```

| Flag | Description | Default |
|------|-----------|-------------|
| `--dir <path>` | Directory to scan | `$HOME` |
| `--interval <sec>` | Scan period, seconds | `60` |
| `--port <num>` | HTTP server port | `1234` |
| `--output <path>` | Path to the output JSON file | `$HOME/.media_files` |
| `-h`, `--help` | Help | — |

---

## Getting results

### Via file

```bash
cat ~/.media_files
```

### Via HTTP

```bash
curl http://localhost:1234/media_files
```

### Sample Response

```json
{
"audio": [
"111.mp3",
"222.wav"
],
"video": [
"333.mpg"
],
"images": [
"444.jpeg",
"555.png"
]
}
```

---

## Supported Formats

| Category | Extensions |
|-----------|------------|
| **audio** | `.mp3` `.wav` `.flac` `.aac` `.ogg` `.m4a` `.wma` `.opus` `.ape` `.alac` |
| **video** | `.mp4` `.avi` `.mkv` `.mov` `.wmv` `.flv` `.mpg` `.mpeg` `.m4v` `.webm` `.3gp` `.vob` `.ogv` |
| **images** | `.jpg` `.jpeg` `.png` `.gif` `.bmp` `.tiff` `.tif` `.webp` `.svg` `.ico` `.heic` `.heif` `.raw` `.cr2` `.nef` |

---
