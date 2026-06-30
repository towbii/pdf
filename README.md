# PDF Editor

A fast, open-source PDF editor for Windows — built with C++, Qt 6 and MuPDF. **Current version: 1.5.0**

## Download

**[⬇ Download Installer (Windows 10/11, 64-bit)](../../releases/latest)**

Run `PDFEditor-Setup.exe` and follow the wizard. That's it — no Qt, no Visual Studio, no dependencies to install yourself. Everything is bundled.

## Features

- **Highlight** — select text and highlight it; drag over any area for a coloured rectangle
- **Pen** — freehand drawing with colour picker and adjustable size
- **Eraser** — removes any annotation or ink stroke
- **Text** — click anywhere to insert a text annotation
- **Signatures** — create, save and reuse multiple signatures (drawn or imported from image)
- **Page tools** — rotate, delete, duplicate pages (annotations copy with the page)
- **Form filling** — detect and fill PDF form fields
- **Thumbnails** — live page preview panel, updates as you edit
- **Undo / Redo** — full history
- **Dark and light theme**
- **Register as Windows default PDF viewer**

## Keyboard Shortcuts

All shortcuts can be changed in **Settings → Keyboard Shortcuts**.

| Key | Action |
|-----|--------|
| `S` | Select tool |
| `H` | Highlight tool |
| `P` | Pen tool |
| `E` | Eraser tool |
| `T` | Text tool |
| `U` | Signature tool |
| `Esc` | Back to Select |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `Ctrl+scroll` | Zoom |
| `Ctrl+[` / `Ctrl+]` | Rotate page |

## Contributing

Pull requests are welcome. Please open an issue first for larger changes so we can discuss the approach.

## Build from Source

> This section is for developers who want to contribute to the code.
> End users should use the installer above.

**Requirements:**

- Windows 10/11 64-bit
- [Visual Studio 2022](https://visualstudio.microsoft.com/) — Desktop C++ workload
- [Qt 6.6+](https://www.qt.io/download) — MSVC 2022 64-bit component
- [vcpkg](https://vcpkg.io/) — for MuPDF
- CMake 3.21+

See [cpp/BUILD.md](cpp/BUILD.md) for step-by-step instructions.

## License

[MIT](LICENSE)
