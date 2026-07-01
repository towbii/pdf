# PDF Editor

A fast, open-source PDF editor for Windows — built with C++, Qt 6 and MuPDF. **Current version: 1.5.6**

## Download

**[⬇ Download Installer (Windows 10/11, 64-bit)](../../releases/latest)**

Run `PDFEditor-Setup.exe` and follow the wizard. No Qt, no Visual Studio, no dependencies — everything is bundled.

> **⚠️ Antivirus warning (false positive)**
> Windows Defender or your browser may flag the installer as suspicious. This is a **false positive** — the file contains no malware.
> It happens because the installer is newly published and has not yet built up a download reputation with Microsoft.
> The application is fully open source — you can review every line of code in this repository.
> To install anyway: click **More info → Run anyway** in the SmartScreen dialog, or go to **Windows Security → Protection history**, find the entry and choose **Allow**.

> **🔒 Your data is always safe.** Signatures, settings, and recent files are stored in `%APPDATA%\PDFEditor\` and are never modified by the installer or uninstaller. Updating to a new version preserves everything automatically.

## Features

### Annotation tools
- **Highlight** — drag over text to highlight it; drag over any area for a flat coloured rectangle
- **Pen** — freehand drawing with colour picker and adjustable stroke width
- **Eraser** — removes any annotation or ink stroke with a single drag
- **Text** — click anywhere on the page to insert a text annotation; double-click an existing annotation to edit it
- **Signatures** — create, save and reuse multiple signatures; drawn with the mouse or imported from a PNG/JPG
- **Insert image** — insert a PNG/JPG/BMP file as a full-page image or place it on the current page
- **Edit Text** — in Select mode, drag over any region to extract its text automatically (native PDF text layer first, Tesseract OCR as fallback for true scans). A small inline editor opens pre-filled with the recognised text — correct it and press Enter to place a new text annotation at that position. No separate tool needed.

### Moving and resizing annotations
Click any annotation or inserted image in **Select mode** to show 8 resize handles:
- **Drag a handle** — resize the annotation
- **Drag the body** — move the annotation anywhere on the page
- **Double-click** — edit the text content (FreeText annotations)
- **Click elsewhere** — deselect

### Page management
- **Rotate, delete, duplicate** pages — annotations and ink are preserved on duplication
- **Merge PDFs** — drag a PDF onto an open document to append its pages, or use the Merge dialog (also supports drag & drop)
- **Split / extract pages** — save a subset of pages to a new file
- **Insert blank page** — right-click in the thumbnail panel
- **Reorder pages** — drag thumbnails in the sidebar to reorder pages

### Search
- **Ctrl+F** — search for text across all pages; matches are highlighted in yellow, the current match in orange
- Navigate results with **Enter** / **Shift+Enter** or the ▲ ▼ buttons

### Viewing
- **Real-time zoom slider** — instant live preview while dragging; snaps to 100%; **double-click to reset to 100%**
- **Zoom shortcuts** — `Ctrl+0` fit to window, `Ctrl+1` reset to 100%, `Ctrl++` / `Ctrl+-` zoom in/out
- **Page navigation** — jump field with ◀ ▶ arrows in the toolbar; updates live while scrolling
- **Auto-center on zoom** — view stays locked on the same content while zooming in and out
- **Text-aware scroll** — on open, automatically scrolls to the first line of text (not blank margins)
- **Page thumbnails** — live sidebar that updates after every edit

### Other
- **Print** — `Ctrl+P` prints via the Windows print dialog at full printer resolution
- **Form filling** — detects and fills PDF form fields
- **Watermark** — add semi-transparent text watermarks
- **Undo / Redo** — full history for all edits
- **Dark and light theme**
- **Drag & drop** — open PDFs or images by dropping them onto the window; if no PDF is open, a new document is created automatically
- **Register as Windows default PDF viewer**
- **Customisable keyboard shortcuts** (Settings → Keyboard Shortcuts)

## Keyboard Shortcuts

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
| `Ctrl+P` | Print |
| `Ctrl+F` | Search |
| `Ctrl+scroll` | Zoom |
| `Ctrl+0` | Fit to window |
| `Ctrl+1` | Zoom 100% |
| `Ctrl+[` / `Ctrl+]` | Rotate page |

All shortcuts can be changed in **Settings → Keyboard Shortcuts**.

## Uninstall

Go to **Windows Settings → Apps → PDF Editor → Uninstall**, or open the **Start menu → PDF Editor → Uninstall PDF Editor**.

The uninstaller removes the program files (`Program Files\PDF Editor`) but **leaves your personal data untouched**:

| Kept | Removed |
|------|---------|
| Signatures | PDFEditor.exe and all DLLs |
| Settings (theme, shortcuts, …) | Qt plugins and translations |
| Recent file list | Desktop / Start menu shortcuts |

Your data lives in `%APPDATA%\PDFEditor\` and is never touched by the uninstaller.

**Updating** works the same way — the new installer silently removes the old version first, then installs fresh. Your settings and signatures are preserved automatically.

## Contributing

Pull requests are welcome. Please open an issue first for larger changes so we can discuss the approach.

## Build from Source

> For developers only. End users should use the installer above.

**Requirements:**
- Windows 10/11 64-bit
- [Visual Studio 2022](https://visualstudio.microsoft.com/) — Desktop C++ workload
- [Qt 6.6+](https://www.qt.io/download) — MSVC 2022 64-bit component
- [vcpkg](https://vcpkg.io/) — for MuPDF
- CMake 3.21+

See [cpp/BUILD.md](cpp/BUILD.md) for step-by-step instructions.

## License

[MIT](LICENSE)
