# PDF Editor

A professional, open-source PDF editor for Windows — built with C++, Qt 6 and MuPDF.

![PDF Editor Screenshot](docs/screenshot.png)

## Features

- **View & navigate** PDFs — smooth zoom, page thumbnails, keyboard shortcuts
- **Annotate** — highlight text, draw freehand with pen, insert text
- **Eraser** — remove any annotation or ink stroke
- **Signatures** — create, save, and reuse multiple signatures; drawn or imported from image
- **Edit pages** — rotate, delete, duplicate pages
- **Form filling** — detect and fill PDF form fields
- **Save & undo** — full undo/redo history; safe save (no file-lock issues)
- **Bilingual** — German and English UI (switchable in Settings → restart to apply)
- **Register as Windows default PDF viewer**
- **Dark theme**

## Download (Windows)

Download the latest release from the [Releases](../../releases) page.  
Extract the ZIP and run `PDFEditor.exe` — no installation required.

**Requirements:** Windows 10 / 11, 64-bit

## Keyboard Shortcuts

| Key | Tool |
|-----|------|
| `S` | Select |
| `H` | Highlight |
| `P` | Pen |
| `E` | Eraser |
| `T` | Text |
| `U` | Signature |
| `Esc` | Back to Select |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Ctrl+O` | Open |
| `Ctrl+S` | Save |
| `Ctrl+scroll` | Zoom |

## Build from Source

### Requirements

- Windows 10/11 64-bit
- [Qt 6.6+](https://www.qt.io/download) (MSVC 2022 64-bit component)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (Desktop C++ workload)
- [vcpkg](https://vcpkg.io/) with `libmupdf:x64-windows`
- CMake 3.21+

### Setup

```powershell
# Install MuPDF via vcpkg
vcpkg install libmupdf:x64-windows

# Clone the repo
git clone https://github.com/YOUR_USERNAME/pdf-editor.git
cd pdf-editor/cpp

# Configure
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release -j 4
```

The executable and all required DLLs will be in `build/Release/`.

## License

MIT License — see [LICENSE](LICENSE)
