# Café-Xplorer

A multi-purpose file manager for the Nintendo Wii U.

## Features

### File Management
- Browse full Wii U filesystem
- Browse Fat32/exFat drives
- Navigate with D-Pad
- View file information (name, size, type)
- Create new files and folders
- Copy/Paste/Move/Rename/Delete files/folders
- Select multiple files for file operations
- Side panel menu for quick access to operations

### Text Editor
- View and edit text files (.txt, .json, .log, .ini, .cfg, .xml, .md)
- Insert and delete lines
- View and edit modes
- Save dialog on close when modified (Save, Save As, or Discard)

### Image Viewer
- View PNG/JPG/JPEG images
- Zoom in/out controls
- Pan when zoomed
- Reset zoom and position

### GIF Viewer
- View animated GIFs
- Play/Pause toggle
- Zoom in/out (right stick)
- Pan with left stick
- Reset zoom and position

### PDF Viewer
- View PDF documents
- Page navigation (L/R)
- Zoom in/out (right stick)
- Pan when zoomed (left stick)
- Rotate pages (ZL/ZR)
- Reset zoom

### Video Player
- Play MP4, AVI, MKV, and MOV video files
- Playback controls (Play/Pause)
- Seek forward/backward (10 seconds)
- Progress bar with time display

### Audio Player
- Play MP3, WAV, OGG, and FLAC audio files
- Playback controls (Play/Pause)
- Seek forward/backward (10 seconds)
- Progress bar with time display

## Screenshots

### File Browser
![File Browser](Screenshots/Screenshot_1.jpg)

### Context Menu
![Context Menu](Screenshots/Screenshot_2.jpg)

### Text Editor
![Text Editor](Screenshots/Screenshot_3.jpg)
![Audio Player](Screenshots/Screenshot_7.jpg)

### Image Viewer
![Text Editor with Keyboard](Screenshots/Screenshot_4.jpg)

### Video Player
![Image Viewer](Screenshots/Screenshot_5.jpg)

### Music Player
![Video Player](Screenshots/Screenshot_6.jpg)

## Requirements
- devkitPro toolchain with `DEVKITPRO` environment variable set
- Libraries:
  - [wut](https://github.com/devkitPro/wut)
  - [wiiu-sdl2](https://github.com/yawut/SDL) (wiiu-sdl2_ttf, wiiu-sdl2_image)
  - [FFmpeg](https://github.com/GaryOderNichts/FFmpeg-wiiu) (libavformat, libavcodec, libavutil, libswscale, libswresample)
  - [libmocha](https://github.com/wiiu-env/libmocha)

## Build
```bash
make
```

## Installation
Copy `Café-Xplorer.wuhb` or `.rpx` to `sd:/wiiu/apps/`


## Credits

- [@GaryOderNichts](https://github.com/GaryOderNichts) for [FFmpeg-wiiu](https://github.com/GaryOderNichts/FFmpeg-wiiu) config
- [@WiiUIdent](https://github.com/GaryOderNichts/WiiUIdent) UI used as the framework
- [@FFmpeg](https://github.com/FFmpeg/FFmpeg) for audio/video
- UI/Name design inspired by [N-Xplorer](https://github.com/CompSciOrBust/N-Xplorer) by [@CompSciOrBust](https://github.com/CompSciOrBust)
- [@wiiu-env](https://github.com/wiiu-env) for [libmocha](https://github.com/wiiu-env/libmocha) filesystem access
- [@wiiu-env](https://github.com/wiiu-env) for [librpxloader](https://github.com/wiiu-env/librpxloader) for loading .rpx and .wuhb files
- [@hito16](https://github.com/hito16) for [mupdf-devkitppc](https://github.com/hito16/mupdf-devkitppc) for viewing PDFs