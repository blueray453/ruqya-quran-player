# Ruqya Quran Player

A desktop Quran player built with **Qt 6 (C++)** that focuses on a curated Ruqya playlist. The application downloads Quran recitation and metadata on demand, caches them locally, and provides a simple interface for browsing and listening.

## Features

* Curated Ruqya playlist
* Automatic download of Quran metadata
* Automatic download and caching of audio
* Offline playback after files have been cached
* Background loading (UI remains responsive)
* Arabic text display
* Play, Pause, Stop, Previous, and Next controls
* Auto-play through the playlist
* Optional custom playlist for local audio files (duas, lectures, etc.)
* Automatic creation of cache directories

## Screenshot

> Add a screenshot here.

```
README.md
docs/
└── screenshot.png
```

```md
![Screenshot](docs/screenshot.png)
```

---

## Requirements

* C++17
* CMake 3.16+
* Qt 6

  * Widgets
  * Network
  * Multimedia

Example packages (Ubuntu/Debian):

```bash
sudo apt install \
    cmake \
    g++ \
    qt6-base-dev \
    qt6-multimedia-dev
```

---

## Building

Clone the repository:

```bash
git clone https://github.com/<your-username>/ruqya-quran-player.git
cd ruqya-quran-player
```

Run:

```bash
chmod +x run.sh
./run.sh
```

The script automatically:

1. Creates the `build/` directory if needed.
2. Configures CMake (first run only).
3. Builds the application.
4. Launches the player.

You can also build manually:

```bash
mkdir -p build
cd build

cmake ..
cmake --build . -j
./ruqya_quran_player
```

---

## Project Structure

```
.
├── CMakeLists.txt
├── main.cpp
├── run.sh
├── build/
└── .cache/
    ├── audio/
    └── metadata/
```

The `.cache` directory is created automatically.

---

## Audio Cache

Downloaded Quran audio is stored locally.

```
.cache/
├── audio/
│   ├── 1_1.mp3
│   ├── 2_255.mp3
│   └── ...
└── metadata/
```

After an ayah has been downloaded once, it can be played offline.

---

## Custom Playlist

Additional local audio files can be appended to the end of the playlist using `custom_playlist.json`.

Example:

```json
[
  {
    "path": "extras/dua1.mp3",
    "title": "Morning Dua",
    "body": "اللهم بك أصبحنا..."
  },
  {
    "path": "/home/user/audio/ruqya.m4a",
    "title": "Ruqya Recording"
  }
]
```

* Relative paths are resolved relative to the executable.
* Absolute paths are also supported.
* Missing files are skipped gracefully.

---

## Playlist

The application contains a predefined Ruqya playlist including:

* Surah Al-Fatihah
* Ayatul Kursi
* Selected verses from Al-Baqarah
* Selected verses from Aal-Imran
* Surah Al-Jinn
* Surah Al-Ikhlas
* Surah Al-Falaq
* Surah An-Nas
* Additional Ruqya-related verses

The playlist is defined directly in the source code and can be customized.

---

## Font

The application uses the **Amiri Quran** font when available.

If the font is not installed, it falls back to the system default.

---

## Network

Metadata is retrieved from:

* https://api.alquran.cloud

Audio is downloaded automatically and cached for future playback.

---

## License

This project is released under the MIT License.

Feel free to modify and distribute it.

---

## Acknowledgements

* Qt Framework
* AlQuran Cloud API
* Amiri Quran Font
