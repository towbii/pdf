#include "PdfDocument.h"
#include <QDebug>
#include <QFileInfo>
#include <cstring>
#include <cstdio>
#include <cmath>

// ──────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────

static fz_context* makeCtx() {
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (ctx) fz_register_document_handlers(ctx);
    return ctx;
}

// ──────────────────────────────────────────────
// Construction
// ──────────────────────────────────────────────

PdfDocument::PdfDocument() : m_ctx(makeCtx()) {}

PdfDocument::~PdfDocument() {
    close();
    if (m_ctx) fz_drop_context(m_ctx);
}

// ──────────────────────────────────────────────
// Open / save / close
// ──────────────────────────────────────────────

bool PdfDocument::open(const QString &path) {
    close();
    // Read entire file with Qt to release the OS file handle immediately.
    // This lets pdf_save_document write back to the same path without a lock conflict.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray raw = file.readAll();
    file.close();

    bool ok = false;
    fz_try(m_ctx) {
        m_docBuf = fz_new_buffer_from_copied_data(m_ctx,
            reinterpret_cast<const unsigned char*>(raw.constData()),
            static_cast<size_t>(raw.size()));
        fz_stream *stream = fz_open_buffer(m_ctx, m_docBuf);
        m_doc  = fz_open_document_with_stream(m_ctx, "application/pdf", stream);
        fz_drop_stream(m_ctx, stream);
        m_pdoc = pdf_specifics(m_ctx, m_doc);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "PdfDocument::open:" << fz_caught_message(m_ctx);
        if (m_docBuf) { fz_drop_buffer(m_ctx, m_docBuf); m_docBuf = nullptr; }
    }
    if (ok) {
        m_filePath = path;
        m_history.clear();
        m_redoStack.clear();
        m_wordCache.clear();
    }
    return ok;
}

bool PdfDocument::save(const QString &path) {
    if (!m_pdoc) return false;
    QString dest = path.isEmpty() ? m_filePath : path;
    QString tmp = dest + ".~tmp";
    QByteArray utf8Tmp = tmp.toUtf8();
    pdf_write_options opts = pdf_default_write_options;
    opts.do_garbage = 1;
    opts.do_compress = 1;
    bool ok = false;
    fz_try(m_ctx) {
        pdf_save_document(m_ctx, m_pdoc, utf8Tmp.constData(), &opts);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "PdfDocument::save:" << fz_caught_message(m_ctx);
    }
    if (ok) {
        QFile::remove(dest);
        if (!QFile::rename(tmp, dest)) {
            QFile::remove(tmp);
            return false;
        }
        m_filePath = dest;
    }
    return ok;
}

void PdfDocument::close() {
    m_wordCache.clear();
    if (m_doc) {
        fz_drop_document(m_ctx, m_doc);
        m_doc  = nullptr;
        m_pdoc = nullptr;
    }
    if (m_docBuf) { fz_drop_buffer(m_ctx, m_docBuf); m_docBuf = nullptr; }
    m_filePath.clear();
}

// ──────────────────────────────────────────────
// Snapshot (undo system)
// ──────────────────────────────────────────────

QByteArray PdfDocument::toBytes() const {
    if (!m_pdoc) return {};
    QByteArray result;
    fz_buffer *buf = nullptr;
    fz_output *out = nullptr;
    pdf_write_options opts = pdf_default_write_options;
    opts.do_garbage  = 0;
    opts.do_compress = 1;
    fz_try(m_ctx) {
        buf = fz_new_buffer(m_ctx, 1 << 20);
        out = fz_new_output_with_buffer(m_ctx, buf);
        pdf_write_document(m_ctx, m_pdoc, out, &opts);
        fz_close_output(m_ctx, out);
        unsigned char *rawPtr = nullptr;
        size_t rawLen = fz_buffer_storage(m_ctx, buf, &rawPtr);
        result = QByteArray(reinterpret_cast<const char*>(rawPtr), static_cast<int>(rawLen));
    } fz_catch(m_ctx) {
        qWarning() << "toBytes:" << fz_caught_message(m_ctx);
    }
    if (out) fz_drop_output(m_ctx, out);
    if (buf) fz_drop_buffer(m_ctx, buf);
    return result;
}

bool PdfDocument::fromBytes(const QByteArray &data) {
    bool ok = false;
    fz_buffer *newBuf = nullptr;
    fz_try(m_ctx) {
        newBuf = fz_new_buffer_from_copied_data(m_ctx,
            reinterpret_cast<const unsigned char*>(data.constData()),
            static_cast<size_t>(data.size()));
        fz_stream *stream = fz_open_buffer(m_ctx, newBuf);
        fz_drop_document(m_ctx, m_doc);
        m_doc  = fz_open_document_with_stream(m_ctx, "application/pdf", stream);
        fz_drop_stream(m_ctx, stream);
        m_pdoc = pdf_specifics(m_ctx, m_doc);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "fromBytes:" << fz_caught_message(m_ctx);
        if (newBuf) { fz_drop_buffer(m_ctx, newBuf); newBuf = nullptr; }
    }
    if (ok) {
        if (m_docBuf) fz_drop_buffer(m_ctx, m_docBuf);
        m_docBuf = newBuf;
        // Flush MuPDF's page / resource store so the next render builds fresh
        // display lists from the new document, not stale ones from the old.
        fz_empty_store(m_ctx);
    }
    m_wordCache.clear();
    m_charCache.clear();
    return ok;
}

void PdfDocument::snapshot() {
    m_redoStack.clear();
    QByteArray snap = toBytes();
    if (!snap.isEmpty()) {
        m_history.append(snap);
        if (m_history.size() > MAX_HISTORY)
            m_history.removeFirst();
    }
}

bool PdfDocument::undo() {
    if (m_history.isEmpty()) return false;
    QByteArray cur = toBytes();
    if (!cur.isEmpty()) m_redoStack.append(cur);
    return fromBytes(m_history.takeLast());
}

bool PdfDocument::redo() {
    if (m_redoStack.isEmpty()) return false;
    QByteArray cur = toBytes();
    if (!cur.isEmpty()) m_history.append(cur);
    return fromBytes(m_redoStack.takeLast());
}

// ──────────────────────────────────────────────
// Page info
// ──────────────────────────────────────────────

int PdfDocument::pageCount() const {
    if (!m_doc) return 0;
    return fz_count_pages(m_ctx, m_doc);
}

PageSize PdfDocument::pageSize(int pageNum) const {
    PageSize ps;
    if (!m_doc || pageNum >= pageCount()) return ps;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        fz_rect r = fz_bound_page(m_ctx, page);
        ps.width  = r.x1 - r.x0;
        ps.height = r.y1 - r.y0;
        if (m_pdoc) {
            pdf_page *pp = pdf_load_page(m_ctx, m_pdoc, pageNum);
            ps.rotation = pdf_dict_get_int(m_ctx, pp->obj, PDF_NAME(Rotate));
            fz_drop_page(m_ctx, (fz_page*)pp);
        }
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}
    return ps;
}

// ──────────────────────────────────────────────
// Rendering
// ──────────────────────────────────────────────

QPixmap PdfDocument::pixmapFromMupdf(fz_page *page, float zoom) const {
    QPixmap result;
    fz_matrix mat = fz_scale(zoom, zoom);
    fz_pixmap *pix = nullptr;
    fz_try(m_ctx) {
        pix = fz_new_pixmap_from_page(m_ctx, page, mat,
                                       fz_device_rgb(m_ctx), 0);
        QImage img(pix->samples, pix->w, pix->h,
                   pix->stride, QImage::Format_RGB888);
        result = QPixmap::fromImage(img.copy());
    } fz_catch(m_ctx) {
        qWarning() << "render:" << fz_caught_message(m_ctx);
    }
    if (pix) fz_drop_pixmap(m_ctx, pix);
    return result;
}

QPixmap PdfDocument::renderPage(int pageNum, float zoom) {
    if (!m_doc || pageNum >= pageCount()) return {};
    QPixmap result;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        result = pixmapFromMupdf(page, zoom);
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}
    return result;
}

QPixmap PdfDocument::renderThumbnail(int pageNum, int targetWidth) const {
    if (!m_doc || pageNum >= pageCount()) return {};
    QPixmap result;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        fz_rect r = fz_bound_page(m_ctx, page);
        float zoom = (r.x1 - r.x0 > 0) ? targetWidth / (r.x1 - r.x0) : 1.f;
        result = pixmapFromMupdf(page, zoom);
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}
    return result;
}

// ──────────────────────────────────────────────
// Text extraction
// ──────────────────────────────────────────────

void PdfDocument::invalidatePage(int pageNum) {
    m_wordCache.remove(pageNum);
    m_charCache.remove(pageNum);
}

QVector<WordBox> PdfDocument::getWords(int pageNum) {
    if (m_wordCache.contains(pageNum)) return m_wordCache[pageNum];
    QVector<WordBox> words;
    if (!m_doc || pageNum >= pageCount()) return words;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        fz_stext_options opts{};
        fz_stext_page *stext = fz_new_stext_page_from_page(m_ctx, page, &opts);

        for (fz_stext_block *blk = stext->first_block; blk; blk = blk->next) {
            if (blk->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (fz_stext_line *ln = blk->u.t.first_line; ln; ln = ln->next) {
                for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                    if (ch->c <= 32) {
                        // Space character: add a word boundary box
                        if (!words.isEmpty() && words.last().text != " ") {
                            fz_rect bbox = fz_rect_from_quad(ch->quad);
                            if (!fz_is_empty_rect(bbox)) {
                                WordBox wb;
                                wb.x0 = qMin(bbox.x0, bbox.x1); wb.y0 = qMin(bbox.y0, bbox.y1);
                                wb.x1 = qMax(bbox.x0, bbox.x1); wb.y1 = qMax(bbox.y0, bbox.y1);
                                wb.text = " ";
                                words.append(wb);
                            }
                        }
                        continue;
                    }
                    fz_rect bbox = fz_rect_from_quad(ch->quad);
                    if (fz_is_empty_rect(bbox)) continue;
                    WordBox wb;
                    wb.x0 = qMin(bbox.x0, bbox.x1); wb.y0 = qMin(bbox.y0, bbox.y1);
                    wb.x1 = qMax(bbox.x0, bbox.x1); wb.y1 = qMax(bbox.y0, bbox.y1);
                    wb.text = QString(QChar(ch->c));
                    words.append(wb);
                }
            }
        }
        fz_drop_stext_page(m_ctx, stext);
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}

    m_wordCache[pageNum] = words;
    return words;
}

QVector<CharBox> PdfDocument::getChars(int pageNum) {
    if (m_charCache.contains(pageNum)) return m_charCache[pageNum];
    QVector<CharBox> chars;
    if (!m_doc || pageNum >= pageCount()) return chars;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        fz_stext_options opts{};
        fz_stext_page *stext = fz_new_stext_page_from_page(m_ctx, page, &opts);
        for (fz_stext_block *blk = stext->first_block; blk; blk = blk->next) {
            if (blk->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (fz_stext_line *ln = blk->u.t.first_line; ln; ln = ln->next) {
                for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                    if (ch->c < 32) continue;
                    fz_rect bbox = fz_rect_from_quad(ch->quad);
                    if (fz_is_empty_rect(bbox)) continue;
                    CharBox cb;
                    cb.ch = QChar(ch->c);
                    cb.x0 = qMin(bbox.x0, bbox.x1); cb.y0 = qMin(bbox.y0, bbox.y1);
                    cb.x1 = qMax(bbox.x0, bbox.x1); cb.y1 = qMax(bbox.y0, bbox.y1);
                    chars.append(cb);
                }
            }
        }
        fz_drop_stext_page(m_ctx, stext);
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}
    m_charCache[pageNum] = chars;
    return chars;
}

// ──────────────────────────────────────────────
// Annotation helpers
// ──────────────────────────────────────────────

pdf_page* PdfDocument::loadPdfPage(int pageNum) const {
    if (!m_pdoc || pageNum >= pageCount()) return nullptr;
    pdf_page *pp = nullptr;
    fz_try(m_ctx) { pp = pdf_load_page(m_ctx, m_pdoc, pageNum); }
    fz_catch(m_ctx) { pp = nullptr; }
    return pp;
}

// ──────────────────────────────────────────────
// Highlight
// ──────────────────────────────────────────────

bool PdfDocument::addHighlight(int pageNum,
                                float x0, float y0, float x1, float y1,
                                float r, float g, float b) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        float xLo = qMin(x0, x1), xHi = qMax(x0, x1);
        float yLo = qMin(y0, y1), yHi = qMax(y0, y1);
        float color[3] = {r, g, b};

        // ── Step 1: rectangle-based text selection.
        // Only characters whose bounding box intersects the drag rect are included.
        // fz_highlight_selection is a cursor algorithm (selects everything between two
        // points in reading order) — it grabs text outside the drag area. Here we want
        // exactly what's inside the drawn rectangle, merged into one quad per line.
        bool useRect = true;
        {
            fz_stext_options opts{};
            fz_stext_page *stext = fz_new_stext_page_from_page(m_ctx, (fz_page*)pp, &opts);
            if (stext) {
                fz_rect sel = fz_make_rect(xLo, yLo, xHi, yHi);
                QVector<fz_quad> quads;

                for (fz_stext_block *blk = stext->first_block; blk; blk = blk->next) {
                    if (blk->type != FZ_STEXT_BLOCK_TEXT) continue;
                    for (fz_stext_line *ln = blk->u.t.first_line; ln; ln = ln->next) {
                        fz_quad lineQ{};
                        bool gotChar = false;
                        for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                            if (ch->c < 32) continue;
                            fz_rect cr = fz_rect_from_quad(ch->quad);
                            if (fz_is_empty_rect(fz_intersect_rect(cr, sel))) continue;
                            if (!gotChar) {
                                lineQ = ch->quad;
                                gotChar = true;
                            } else {
                                // Expand the line quad to cover this character.
                                // fitz device space: y increases downward, so
                                // ul/ur have smaller y (top) and ll/lr have larger y (bottom).
                                lineQ.ul.x = std::min(lineQ.ul.x, ch->quad.ul.x);
                                lineQ.ul.y = std::min(lineQ.ul.y, ch->quad.ul.y);
                                lineQ.ur.x = std::max(lineQ.ur.x, ch->quad.ur.x);
                                lineQ.ur.y = std::min(lineQ.ur.y, ch->quad.ur.y);
                                lineQ.ll.x = std::min(lineQ.ll.x, ch->quad.ll.x);
                                lineQ.ll.y = std::max(lineQ.ll.y, ch->quad.ll.y);
                                lineQ.lr.x = std::max(lineQ.lr.x, ch->quad.lr.x);
                                lineQ.lr.y = std::max(lineQ.lr.y, ch->quad.lr.y);
                            }
                        }
                        if (gotChar) quads.append(lineQ);
                    }
                }
                fz_drop_stext_page(m_ctx, stext);

                if (!quads.isEmpty()) {
                    pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_HIGHLIGHT);
                    pdf_set_annot_quad_points(m_ctx, annot, quads.size(), quads.constData());
                    pdf_set_annot_color(m_ctx, annot, 3, color);
                    pdf_set_annot_opacity(m_ctx, annot, 0.4f);
                    pdf_update_annot(m_ctx, annot);
                    ok = true;
                    useRect = false;
                }
            }
        }

        // ── Step 2: flat rectangle fallback — no text under cursor.
        // Use PDF_ANNOT_SQUARE for sharp corners.
        // PDF_ANNOT_HIGHLIGHT always renders Bézier-curved corners by design.
        if (useRect) {
            float h = qMax(yHi - yLo, 10.f);
            fz_rect rectArea = fz_make_rect(xLo, yLo, xHi, yLo + h);
            pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_SQUARE);
            pdf_set_annot_rect(m_ctx, annot, rectArea);
            pdf_set_annot_interior_color(m_ctx, annot, 3, color);
            pdf_set_annot_border_width(m_ctx, annot, 0.f);
            pdf_set_annot_opacity(m_ctx, annot, 0.4f);
            pdf_update_annot(m_ctx, annot);
            ok = true;
        }
    } fz_catch(m_ctx) {
        qWarning() << "addHighlight:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// ──────────────────────────────────────────────
// Ink stroke
// ──────────────────────────────────────────────

bool PdfDocument::addInkStroke(int pageNum, const QVector<QPointF> &pts,
                                float r, float g, float b, float width) {
    if (pts.size() < 2) return false;
    snapshot();
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        QVector<fz_point> fpts;
        fpts.reserve(pts.size());
        for (const QPointF &p : pts) fpts.append({(float)p.x(), (float)p.y()});
        int count = fpts.size();
        pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_INK);
        pdf_set_annot_ink_list(m_ctx, annot, 1, &count, fpts.constData());
        float col[3] = {r, g, b};
        pdf_set_annot_color(m_ctx, annot, 3, col);
        pdf_set_annot_border_width(m_ctx, annot, width);
        pdf_update_annot(m_ctx, annot);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "addInkStroke:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// ──────────────────────────────────────────────
// FreeText
// ──────────────────────────────────────────────

bool PdfDocument::insertText(int pageNum, const QString &text,
                              float x, float y, float fontsize,
                              float r, float g, float b) {
    if (text.trimmed().isEmpty()) return false;
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        float w = qMax(text.length() * fontsize * 0.58f + 16.f, 80.f);
        fz_rect rect = fz_make_rect(x, y - fontsize, x + w, y + fontsize * 1.2f);
        pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_FREE_TEXT);
        pdf_set_annot_rect(m_ctx, annot, rect);
        QByteArray utf8 = text.toUtf8();
        pdf_set_annot_contents(m_ctx, annot, utf8.constData());
        // Default appearance string: font Helvetica, given size, given color
        char da[128];
        snprintf(da, sizeof(da), "/Helv %.4g Tf %.4g %.4g %.4g rg", fontsize, r, g, b);
        pdf_dict_put_text_string(m_ctx, pdf_annot_obj(m_ctx, annot),
                                 PDF_NAME(DA), da);
        pdf_set_annot_border_width(m_ctx, annot, 0.f);
        pdf_update_annot(m_ctx, annot);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "insertText:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

QString PdfDocument::freetextAt(int pageNum, float x, float y) const {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return {};
    QString found;
    fz_try(m_ctx) {
        fz_rect mediabox;
        fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
        float pageH = mediabox.y1 - mediabox.y0;

        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};

        for (pdf_annot *annot = pdf_first_annot(m_ctx, pp);
             annot; annot = pdf_next_annot(m_ctx, annot)) {
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_FREE_TEXT) continue;
            fz_rect r = pdf_annot_rect(m_ctx, annot);
            float rx0 = qMin(r.x0, r.x1), rx1 = qMax(r.x0, r.x1);
            float ry0 = qMin(r.y0, r.y1), ry1 = qMax(r.y0, r.y1);
            fz_rect hit = fz_make_rect(rx0 - 6, ry0 - 6, rx1 + 6, ry1 + 6);
            if (fz_is_point_inside_rect(pt, hit) || fz_is_point_inside_rect(pt_flip, hit)) {
                const char *c = pdf_annot_contents(m_ctx, annot);
                if (c) found = QString::fromUtf8(c);
                break;
            }
        }
    } fz_catch(m_ctx) {}
    fz_drop_page(m_ctx, (fz_page*)pp);
    return found;
}

bool PdfDocument::updateFreetext(int pageNum, float x, float y,
                                  const QString &newText,
                                  float fontsize, float r, float g, float b) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        fz_rect mediabox;
        fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
        float pageH = mediabox.y1 - mediabox.y0;

        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};

        pdf_annot *target = nullptr;
        for (pdf_annot *annot = pdf_first_annot(m_ctx, pp);
             annot; annot = pdf_next_annot(m_ctx, annot)) {
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_FREE_TEXT) continue;
            fz_rect r2 = pdf_annot_rect(m_ctx, annot);
            float rx0 = qMin(r2.x0, r2.x1), rx1 = qMax(r2.x0, r2.x1);
            float ry0 = qMin(r2.y0, r2.y1), ry1 = qMax(r2.y0, r2.y1);
            fz_rect hit = fz_make_rect(rx0 - 6, ry0 - 6, rx1 + 6, ry1 + 6);
            if (fz_is_point_inside_rect(pt, hit) || fz_is_point_inside_rect(pt_flip, hit)) { target = annot; break; }
        }
        if (target) {
            snapshot();
            fz_rect old = pdf_annot_rect(m_ctx, target);
            pdf_delete_annot(m_ctx, pp, target);
            if (!newText.trimmed().isEmpty()) {
                float w = qMax(qMax(newText.length() * fontsize * 0.58f + 16.f, 80.f),
                               old.x1 - old.x0);
                fz_rect rect = fz_make_rect(old.x0, old.y0,
                                            old.x0 + w, old.y0 + fontsize * 2.2f);
                pdf_annot *na = pdf_create_annot(m_ctx, pp, PDF_ANNOT_FREE_TEXT);
                pdf_set_annot_rect(m_ctx, na, rect);
                QByteArray utf8 = newText.toUtf8();
                pdf_set_annot_contents(m_ctx, na, utf8.constData());
                char da[128];
                snprintf(da, sizeof(da), "/Helv %.4g Tf %.4g %.4g %.4g rg", fontsize, r, g, b);
                pdf_dict_put_text_string(m_ctx, pdf_annot_obj(m_ctx, na), PDF_NAME(DA), da);
                pdf_set_annot_border_width(m_ctx, na, 0.f);
                pdf_update_annot(m_ctx, na);
            }
            ok = true;
        }
    } fz_catch(m_ctx) {
        qWarning() << "updateFreetext:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// ──────────────────────────────────────────────
QRectF PdfDocument::freetextRectAt(int pageNum, float x, float y) const {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return {};
    QRectF result;
    fz_try(m_ctx) {
        fz_rect mediabox; fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
        float pageH = mediabox.y1 - mediabox.y0;
        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};
        for (pdf_annot *annot = pdf_first_annot(m_ctx, pp);
             annot; annot = pdf_next_annot(m_ctx, annot)) {
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_FREE_TEXT) continue;
            fz_rect r = pdf_annot_rect(m_ctx, annot);
            float rx0 = qMin(r.x0,r.x1), rx1 = qMax(r.x0,r.x1);
            float ry0 = qMin(r.y0,r.y1), ry1 = qMax(r.y0,r.y1);
            fz_rect hit = fz_make_rect(rx0-6, ry0-6, rx1+6, ry1+6);
            if (fz_is_point_inside_rect(pt, hit) || fz_is_point_inside_rect(pt_flip, hit)) {
                result = QRectF(rx0, ry0, rx1-rx0, ry1-ry0);
                break;
            }
        }
    } fz_catch(m_ctx) {}
    fz_drop_page(m_ctx, (fz_page*)pp);
    return result;
}

bool PdfDocument::moveFreetext(int pageNum, const QRectF &oldRect, float dx, float dy) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        fz_rect mediabox; fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
        float pageH = mediabox.y1 - mediabox.y0;
        // find the annotation whose rect matches oldRect
        for (pdf_annot *annot = pdf_first_annot(m_ctx, pp);
             annot; annot = pdf_next_annot(m_ctx, annot)) {
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_FREE_TEXT) continue;
            fz_rect r = pdf_annot_rect(m_ctx, annot);
            float rx0 = qMin(r.x0,r.x1), ry0 = qMin(r.y0,r.y1);
            float rx1 = qMax(r.x0,r.x1), ry1 = qMax(r.y0,r.y1);
            // match with a bit of tolerance
            if (qAbs(rx0 - (float)oldRect.x()) > 4 || qAbs(ry0 - (float)oldRect.y()) > 4) continue;
            snapshot();
            // coords may be y-up or y-down depending on the annot's CTM;
            // freetextRectAt stores them in screen space (y-down), so we offset
            // in screen space and convert back
            float newX0 = rx0 + dx;
            float newY0 = ry0 + dy;
            float newX1 = rx1 + dx;
            float newY1 = ry1 + dy;
            // clamp to page bounds
            float pw = (float)pageSize(pageNum).width;
            float ph = (float)pageSize(pageNum).height;
            float w = newX1 - newX0, h = newY1 - newY0;
            newX0 = qBound(0.f, newX0, pw - w);
            newY0 = qBound(0.f, newY0, ph - h);
            fz_rect newR = fz_make_rect(newX0, newY0, newX0+w, newY0+h);
            // if the stored rect was y-flipped, flip back
            if (r.y0 > r.y1) {
                newR = fz_make_rect(newX0, pageH - newY0, newX0+w, pageH - (newY0+h));
            }
            pdf_set_annot_rect(m_ctx, annot, newR);
            pdf_update_annot(m_ctx, annot);
            ok = true;
            break;
        }
    } fz_catch(m_ctx) {
        qWarning() << "moveFreetext:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// Returns true if point (pt or pt_flip) hits the annotation rect (or raw dict rect).
static bool annotHitTest(fz_context *ctx, pdf_annot *a,
                          fz_point pt, fz_point pt_flip, float margin,
                          fz_rect *outRect = nullptr) {
    // Test 1: CTM-transformed rect (device space)
    fz_rect r = pdf_annot_rect(ctx, a);
    float rx0 = qMin(r.x0,r.x1), rx1 = qMax(r.x0,r.x1);
    float ry0 = qMin(r.y0,r.y1), ry1 = qMax(r.y0,r.y1);
    fz_rect hr = fz_make_rect(rx0-margin, ry0-margin, rx1+margin, ry1+margin);
    if (fz_is_point_inside_rect(pt, hr) || fz_is_point_inside_rect(pt_flip, hr)) {
        if (outRect) *outRect = r;
        return true;
    }
    // Test 2: raw /Rect from PDF dict (PDF user-space, no CTM)
    fz_rect raw = pdf_dict_get_rect(ctx, pdf_annot_obj(ctx, a), PDF_NAME(Rect));
    rx0 = qMin(raw.x0,raw.x1); rx1 = qMax(raw.x0,raw.x1);
    ry0 = qMin(raw.y0,raw.y1); ry1 = qMax(raw.y0,raw.y1);
    hr = fz_make_rect(rx0-margin, ry0-margin, rx1+margin, ry1+margin);
    if (fz_is_point_inside_rect(pt, hr) || fz_is_point_inside_rect(pt_flip, hr)) {
        if (outRect) *outRect = raw;
        return true;
    }
    return false;
}

QRectF PdfDocument::annotRectAt(int pageNum, float x, float y) const {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return {};
    QRectF result;
    fz_try(m_ctx) {
        fz_rect mb; fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mb, &ctm);
        float pageH = mb.y1 - mb.y0;
        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};
        for (pdf_annot *a = pdf_first_annot(m_ctx, pp); a; a = pdf_next_annot(m_ctx, a)) {
            fz_rect r;
            if (annotHitTest(m_ctx, a, pt, pt_flip, 40.f, &r)) {
                result = QRectF(qMin(r.x0,r.x1), qMin(r.y0,r.y1),
                                qAbs(r.x1-r.x0), qAbs(r.y1-r.y0));
                break;
            }
        }
    } fz_catch(m_ctx) {}
    fz_drop_page(m_ctx, (fz_page*)pp);
    return result;
}

bool PdfDocument::moveAnnotAt(int pageNum, float x, float y, float dx, float dy) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        fz_rect mb; fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mb, &ctm);
        float pageH = mb.y1 - mb.y0;
        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};
        const float margin = 40.f;

        for (pdf_annot *a = pdf_first_annot(m_ctx, pp); a; a = pdf_next_annot(m_ctx, a)) {
            fz_rect r;
            if (!annotHitTest(m_ctx, a, pt, pt_flip, margin, &r))
                continue;

            int type = pdf_annot_type(m_ctx, a);
            snapshot();

            if (type == PDF_ANNOT_INK) {
                // Rebuild ink list with offset points
                int ns = pdf_annot_ink_list_count(m_ctx, a);
                QVector<int> counts;
                QVector<fz_point> pts;
                for (int s = 0; s < ns; s++) {
                    int np = pdf_annot_ink_list_stroke_count(m_ctx, a, s);
                    counts.append(np);
                    for (int k = 0; k < np; k++) {
                        fz_point vp = pdf_annot_ink_list_stroke_vertex(m_ctx, a, s, k);
                        vp.x += dx; vp.y += dy;
                        pts.append(vp);
                    }
                }
                pdf_set_annot_ink_list(m_ctx, a, ns, counts.constData(), pts.constData());
            } else {
                // Move the annotation's /Rect
                fz_rect newR = fz_make_rect(r.x0+dx, r.y0+dy, r.x1+dx, r.y1+dy);
                pdf_set_annot_rect(m_ctx, a, newR);
            }
            pdf_update_annot(m_ctx, a);
            ok = true;
            break;
        }
    } fz_catch(m_ctx) {
        qWarning() << "moveAnnotAt:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

bool PdfDocument::resizeAnnotAt(int pageNum, float x, float y,
                                 float newX0, float newY0, float newX1, float newY1) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        fz_rect mb; fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mb, &ctm);
        float pageH = mb.y1 - mb.y0;
        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};
        const float margin = 40.f;

        for (pdf_annot *a = pdf_first_annot(m_ctx, pp); a; a = pdf_next_annot(m_ctx, a)) {
            fz_rect r = pdf_annot_rect(m_ctx, a);
            float rx0 = qMin(r.x0,r.x1), rx1 = qMax(r.x0,r.x1);
            float ry0 = qMin(r.y0,r.y1), ry1 = qMax(r.y0,r.y1);
            float rw = rx1 - rx0, rh = ry1 - ry0;
            fz_rect hr = fz_make_rect(rx0-margin, ry0-margin, rx1+margin, ry1+margin);
            if (!fz_is_point_inside_rect(pt, hr) && !fz_is_point_inside_rect(pt_flip, hr))
                continue;

            int type = pdf_annot_type(m_ctx, a);
            snapshot();

            if (type == PDF_ANNOT_INK && rw > 0.f && rh > 0.f) {
                // Scale ink points into new bounding box
                float scaleX = (newX1 - newX0) / rw;
                float scaleY = (newY1 - newY0) / rh;
                int ns = pdf_annot_ink_list_count(m_ctx, a);
                QVector<int> counts;
                QVector<fz_point> pts;
                for (int s = 0; s < ns; s++) {
                    int np = pdf_annot_ink_list_stroke_count(m_ctx, a, s);
                    counts.append(np);
                    for (int k = 0; k < np; k++) {
                        fz_point vp = pdf_annot_ink_list_stroke_vertex(m_ctx, a, s, k);
                        vp.x = newX0 + (vp.x - rx0) * scaleX;
                        vp.y = newY0 + (vp.y - ry0) * scaleY;
                        pts.append(vp);
                    }
                }
                pdf_set_annot_ink_list(m_ctx, a, ns, counts.constData(), pts.constData());
            } else {
                // Resize by setting new /Rect
                pdf_set_annot_rect(m_ctx, a, fz_make_rect(newX0, newY0, newX1, newY1));
            }
            pdf_update_annot(m_ctx, a);
            ok = true;
            break;
        }
    } fz_catch(m_ctx) {
        qWarning() << "resizeAnnotAt:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

bool PdfDocument::deleteAnnotAt(int pageNum, float x, float y) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        // Collect annotations newest-first so we erase the topmost one.
        QVector<pdf_annot*> annots;
        for (pdf_annot *a = pdf_first_annot(m_ctx, pp); a; a = pdf_next_annot(m_ctx, a))
            annots.prepend(a);

        if (!annots.isEmpty()) {
            // Render at 1× zoom: toPdf() already gives fitz device coords at 1× zoom,
            // so the cursor pixel (cx, cy) matches exactly without scaling.
            // fz_run_page rebuilds the display from scratch each call (no display-list
            // cache is involved), so we don't need to empty the store between renders.
            const float Z = 1.0f;
            const int R = 4;   // hit radius in pixels (= PDF points at 1×)
            int cx = qRound(x), cy = qRound(y);

            QImage base = pixmapFromMupdf((fz_page*)pp, Z).toImage();

            for (pdf_annot *a : annots) {
                pdf_obj *obj = pdf_annot_obj(m_ctx, a);
                pdf_obj *fObj = pdf_dict_get(m_ctx, obj, PDF_NAME(F));
                int savedF = pdf_to_int(m_ctx, fObj);

                // Hide the annotation and re-render.
                pdf_dict_put_int(m_ctx, obj, PDF_NAME(F), savedF | 2 /*PDF_ANNOT_IS_HIDDEN*/);
                QImage hidden = pixmapFromMupdf((fz_page*)pp, Z).toImage();

                // Restore the flag.
                if (savedF) pdf_dict_put_int(m_ctx, obj, PDF_NAME(F), savedF);
                else        pdf_dict_del(m_ctx, obj, PDF_NAME(F));

                // If any pixel within the radius changed, the annotation covers the cursor.
                bool hit = false;
                for (int dy = -R; dy <= R && !hit; ++dy) {
                    for (int dx = -R; dx <= R && !hit; ++dx) {
                        int px = cx + dx, py = cy + dy;
                        if (px >= 0 && px < base.width() &&
                            py >= 0 && py < base.height() &&
                            base.pixel(px, py) != hidden.pixel(px, py))
                            hit = true;
                    }
                }

                if (hit) {
                    snapshot();
                    pdf_delete_annot(m_ctx, pp, a);
                    ok = true;
                    break;
                }
            }
        }
    } fz_catch(m_ctx) {
        qWarning() << "deleteAnnotAt:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// ──────────────────────────────────────────────
// Image insertion
// ──────────────────────────────────────────────

bool PdfDocument::insertImage(int pageNum, const QString &imgPath,
                               float cx, float cy, float w, float h) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    snapshot();
    bool ok = false;
    fz_image *img = nullptr;
    fz_try(m_ctx) {
        QByteArray pathUtf8 = imgPath.toUtf8();
        img = fz_new_image_from_file(m_ctx, pathUtf8.constData());

        // Stamp annotation whose /Rect matches the desired placement.
        // pdf_set_annot_rect takes fitz device-space coords (y-down, 1× zoom)
        // and stores them as PDF user-space via the inverse page CTM.
        fz_rect annot_rect = fz_make_rect(cx - w/2.f, cy - h/2.f,
                                           cx + w/2.f, cy + h/2.f);
        pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_STAMP);
        pdf_set_annot_rect(m_ctx, annot, annot_rect);

        // Use MuPDF's stamp-image API so that pdf_update_annot takes the
        // image path (pdf_write_stamp_appearance_image) rather than the rubber-
        // stamp path (pdf_write_stamp_appearance_rubber).  The rubber-stamp path
        // modifies *rect to enforce a 190:50 aspect ratio, which would corrupt
        // our /Rect and cause the image to appear horizontally stretched.
        // The image path leaves /Rect intact and generates /BBox=[0,0,1,1];
        // pdf_annot_transform then maps [0,0,1,1] → /Rect without distortion.
        pdf_set_annot_stamp_image(m_ctx, annot, img);
        pdf_update_annot(m_ctx, annot);

        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "insertImage:" << fz_caught_message(m_ctx);
    }
    if (img) fz_drop_image(m_ctx, img);
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

// ──────────────────────────────────────────────
// Page operations
// ──────────────────────────────────────────────

bool PdfDocument::rotatePage(int pageNum, int degrees) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        int cur = pdf_dict_get_int(m_ctx, pp->obj, PDF_NAME(Rotate));
        pdf_dict_put_int(m_ctx, pp->obj, PDF_NAME(Rotate), (cur + degrees + 360) % 360);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "rotatePage:" << fz_caught_message(m_ctx);
    }
    fz_drop_page(m_ctx, (fz_page*)pp);
    if (ok) invalidatePage(pageNum);
    return ok;
}

bool PdfDocument::deletePage(int pageNum) {
    if (!m_pdoc || pageNum >= pageCount()) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        pdf_delete_page(m_ctx, m_pdoc, pageNum);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "deletePage:" << fz_caught_message(m_ctx);
    }
    if (ok) invalidatePage(pageNum);
    return ok;
}

bool PdfDocument::insertBlankPage(int afterPageNum) {
    if (!m_pdoc) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        // Get the size of the page at afterPageNum to match it
        fz_rect mediabox = fz_make_rect(0, 0, 595, 842); // A4 default
        if (afterPageNum >= 0 && afterPageNum < pageCount()) {
            pdf_page *pp = loadPdfPage(afterPageNum);
            if (pp) {
                fz_matrix ctm;
                pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
                fz_drop_page(m_ctx, (fz_page*)pp);
            }
        }
        pdf_obj *page = pdf_add_page(m_ctx, m_pdoc, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(m_ctx, m_pdoc, afterPageNum + 1, page);
        pdf_drop_obj(m_ctx, page);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "insertBlankPage:" << fz_caught_message(m_ctx);
    }
    return ok;
}

bool PdfDocument::duplicatePage(int pageNum) {
    if (!m_pdoc || pageNum >= pageCount()) return false;
    snapshot();
    bool ok = false;
    fz_buffer *tmpBuf = nullptr;
    fz_output *tmpOut = nullptr;
    fz_document *tmpDoc = nullptr;
    pdf_graft_map *gmap = nullptr;
    fz_try(m_ctx) {
        // Serialize the document to a temp buffer so we can graft from it.
        // do_appearance=1 ensures every annotation AP stream is committed first.
        fz_empty_store(m_ctx);
        pdf_write_options wopts = pdf_default_write_options;
        wopts.do_garbage    = 0;
        wopts.do_compress   = 0;
        wopts.do_appearance = 1;
        tmpBuf = fz_new_buffer(m_ctx, 1 << 20);
        tmpOut = fz_new_output_with_buffer(m_ctx, tmpBuf);
        pdf_write_document(m_ctx, m_pdoc, tmpOut, &wopts);
        fz_close_output(m_ctx, tmpOut);
        fz_drop_output(m_ctx, tmpOut); tmpOut = nullptr;

        fz_stream *tmpStream = fz_open_buffer(m_ctx, tmpBuf);
        tmpDoc = fz_open_document_with_stream(m_ctx, "application/pdf", tmpStream);
        fz_drop_stream(m_ctx, tmpStream);
        pdf_document *tmpPdf = pdf_specifics(m_ctx, tmpDoc);

        // pdf_graft_mapped_page intentionally omits /Annots from its copy_list.
        // We use our own graft map so we can also graft the /Annots array through
        // the same map — this ensures shared XObjects (e.g. AP streams, images) are
        // only copied once and properly referenced by both the page and its annots.
        gmap = pdf_new_graft_map(m_ctx, m_pdoc);
        pdf_graft_mapped_page(m_ctx, gmap, pageNum + 1, tmpPdf, pageNum);

        // Now copy /Annots from the source page to the newly inserted page.
        pdf_obj *srcPageRef = pdf_lookup_page_obj(m_ctx, tmpPdf, pageNum);
        pdf_obj *srcAnnots  = pdf_dict_get(m_ctx, srcPageRef, PDF_NAME(Annots));
        if (srcAnnots && pdf_array_len(m_ctx, srcAnnots) > 0) {
            pdf_obj *dstPageRef  = pdf_lookup_page_obj(m_ctx, m_pdoc, pageNum + 1);
            pdf_obj *newAnnots   = pdf_graft_mapped_object(m_ctx, gmap, srcAnnots);
            pdf_dict_put_drop(m_ctx, dstPageRef, PDF_NAME(Annots), newAnnots);
        }

        pdf_drop_graft_map(m_ctx, gmap); gmap = nullptr;
        fz_drop_document(m_ctx, tmpDoc); tmpDoc = nullptr;
        fz_drop_buffer(m_ctx, tmpBuf);  tmpBuf = nullptr;
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "duplicatePage:" << fz_caught_message(m_ctx);
        if (gmap)   pdf_drop_graft_map(m_ctx, gmap);
        if (tmpOut) fz_drop_output(m_ctx, tmpOut);
        if (tmpDoc) fz_drop_document(m_ctx, tmpDoc);
        if (tmpBuf) fz_drop_buffer(m_ctx, tmpBuf);
    }
    return ok;
}

bool PdfDocument::movePage(int from, int to) {
    if (!m_pdoc) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        // MuPDF 1.26 has no pdf_move_page: load page obj, delete, re-insert
        pdf_page *ppage = pdf_load_page(m_ctx, m_pdoc, from);
        pdf_obj *pageobj = pdf_keep_obj(m_ctx, ppage->obj);
        fz_drop_page(m_ctx, (fz_page*)ppage);
        pdf_delete_page(m_ctx, m_pdoc, from);
        int insertAt = (to > from) ? to - 1 : to;
        pdf_insert_page(m_ctx, m_pdoc, insertAt, pageobj);
        pdf_drop_obj(m_ctx, pageobj);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "movePage:" << fz_caught_message(m_ctx);
    }
    return ok;
}

// ──────────────────────────────────────────────
// PDF Tools
// ──────────────────────────────────────────────

QRectF PdfDocument::firstTextRect(int pageNum) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return {};
    QRectF result;
    fz_try(m_ctx) {
        fz_stext_options opts{};
        fz_stext_page *stext = fz_new_stext_page_from_page(m_ctx, (fz_page*)pp, &opts);
        if (stext) {
            for (fz_stext_block *blk = stext->first_block; blk; blk = blk->next) {
                if (blk->type == FZ_STEXT_BLOCK_TEXT && blk->u.t.first_line) {
                    fz_rect r = blk->bbox;
                    result = QRectF(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
                    break;
                }
            }
            fz_drop_stext_page(m_ctx, stext);
        }
    } fz_catch(m_ctx) {}
    fz_drop_page(m_ctx, (fz_page*)pp);
    return result;
}

bool PdfDocument::mergeFrom(const QStringList &paths) {
    if (!m_pdoc) return false;
    snapshot();
    bool anyOk = false;
    for (const QString &path : paths) {
        fz_document *srcDoc = nullptr;
        fz_try(m_ctx) {
            QByteArray utf8 = path.toUtf8();
            srcDoc = fz_open_document(m_ctx, utf8.constData());
            pdf_document *srcPdf = pdf_specifics(m_ctx, srcDoc);
            if (srcPdf) {
                int np = fz_count_pages(m_ctx, srcDoc);
                int dst = pageCount();
                for (int i = 0; i < np; ++i)
                    pdf_graft_page(m_ctx, m_pdoc, dst + i, srcPdf, i);
                anyOk = true;
            }
        } fz_catch(m_ctx) {
            qWarning() << "mergeFrom:" << path << fz_caught_message(m_ctx);
        }
        if (srcDoc) fz_drop_document(m_ctx, srcDoc);
    }
    m_wordCache.clear();
    return anyOk;
}

bool PdfDocument::extractPages(const QString &outPath, const QList<int> &pages1based) {
    if (!m_pdoc || pages1based.isEmpty()) return false;
    bool ok = false;
    fz_document *newDoc = nullptr;
    fz_output   *out    = nullptr;
    fz_try(m_ctx) {
        newDoc = (fz_document*)pdf_create_document(m_ctx);
        pdf_document *newPdf = pdf_specifics(m_ctx, newDoc);
        int dst = 0;
        for (int p1 : pages1based) {
            int p0 = p1 - 1;
            if (p0 < 0 || p0 >= pageCount()) continue;
            pdf_graft_page(m_ctx, newPdf, dst++, m_pdoc, p0);
        }
        QByteArray utf8 = outPath.toUtf8();
        pdf_write_options wopts = pdf_default_write_options;
        wopts.do_compress = 1;
        wopts.do_garbage  = 2;
        out = fz_new_output_with_path(m_ctx, utf8.constData(), 0);
        pdf_write_document(m_ctx, newPdf, out, &wopts);
        fz_close_output(m_ctx, out);
        fz_drop_output(m_ctx, out); out = nullptr;
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "extractPages:" << fz_caught_message(m_ctx);
        if (out) fz_drop_output(m_ctx, out);
    }
    if (newDoc) fz_drop_document(m_ctx, newDoc);
    return ok;
}

bool PdfDocument::saveCopy(const QString &outPath, int imageQuality) {
    if (!m_pdoc) return false;
    bool ok = false;
    fz_output *out = nullptr;
    fz_try(m_ctx) {
        pdf_write_options wopts = pdf_default_write_options;
        wopts.do_compress        = 1;
        wopts.do_compress_images = 1;
        wopts.do_garbage         = 4;
        wopts.do_clean           = 1;
        // compression_effort: 0 = default, 1 = min, 100 = max
        // invert the user's quality slider so "low quality" means more compression
        wopts.compression_effort = 100 - imageQuality;
        QByteArray utf8 = outPath.toUtf8();
        out = fz_new_output_with_path(m_ctx, utf8.constData(), 0);
        pdf_write_document(m_ctx, m_pdoc, out, &wopts);
        fz_close_output(m_ctx, out);
        fz_drop_output(m_ctx, out); out = nullptr;
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "saveCopy:" << fz_caught_message(m_ctx);
        if (out) fz_drop_output(m_ctx, out);
    }
    return ok;
}

bool PdfDocument::addWatermarkText(const QString &text, int fontSize,
                                    float opacity, int angleDeg) {
    if (!m_pdoc || text.isEmpty()) return false;
    snapshot();
    bool ok = true;
    QByteArray textUtf8 = text.toUtf8();
    // Sanitise text for PDF literal string (escape parens/backslash)
    QByteArray escaped;
    for (unsigned char c : textUtf8) {
        if (c == '(' || c == ')' || c == '\\') escaped += '\\';
        escaped += c;
    }
    int n = pageCount();
    for (int pn = 0; pn < n && ok; ++pn) {
        pdf_page *pp = loadPdfPage(pn);
        if (!pp) { ok = false; break; }
        fz_try(m_ctx) {
            fz_rect pr = fz_bound_page(m_ctx, (fz_page*)pp);
            float pw = pr.x1 - pr.x0, ph = pr.y1 - pr.y0;
            float cx = pw / 2.f, cy = ph / 2.f;

            // Add ExtGState with alpha to page resources
            pdf_obj *res   = pdf_page_resources(m_ctx, pp);
            pdf_obj *extGs = pdf_dict_get(m_ctx, res, PDF_NAME(ExtGState));
            if (!extGs || !pdf_is_dict(m_ctx, extGs)) {
                extGs = pdf_new_dict(m_ctx, m_pdoc, 4);
                pdf_dict_put(m_ctx, res, PDF_NAME(ExtGState), extGs);
                pdf_drop_obj(m_ctx, extGs);
                extGs = pdf_dict_get(m_ctx, res, PDF_NAME(ExtGState));
            }
            pdf_obj *gs = pdf_new_dict(m_ctx, m_pdoc, 4);
            pdf_dict_put_name(m_ctx, gs, PDF_NAME(Type), "ExtGState");
            pdf_dict_put_real(m_ctx, gs, PDF_NAME(ca), (double)opacity);
            pdf_dict_put_real(m_ctx, gs, PDF_NAME(CA), (double)opacity);
            pdf_dict_puts(m_ctx, extGs, "WMgs", gs);
            pdf_drop_obj(m_ctx, gs);

            // Build content stream snippet
            double radians = angleDeg * M_PI / 180.0;
            double cosA = cos(radians), sinA = sin(radians);
            // Transformation matrix for rotation around page centre
            // [cos -sin sin cos tx ty]
            double tx = cx - cx*cosA + cy*sinA;
            double ty = cy - cx*sinA - cy*cosA;
            char snippet[1024];
            snprintf(snippet, sizeof(snippet),
                "q /WMgs gs "
                "0.5 0.5 0.5 rg "
                "BT "
                "/Helv %d Tf "
                "%.4f %.4f %.4f %.4f %.4f %.4f Tm "
                "(%s) Tj "
                "ET Q\n",
                fontSize,
                cosA, sinA, -sinA, cosA, tx, ty,
                escaped.constData());

            fz_buffer *frag = fz_new_buffer_from_copied_data(m_ctx,
                reinterpret_cast<const unsigned char*>(snippet), strlen(snippet));
            pdf_obj *stream = pdf_add_stream(m_ctx, m_pdoc, frag, nullptr, 0);
            fz_drop_buffer(m_ctx, frag);

            pdf_obj *contents = pdf_dict_get(m_ctx, pp->obj, PDF_NAME(Contents));
            if (!contents) {
                pdf_dict_put(m_ctx, pp->obj, PDF_NAME(Contents), stream);
            } else if (pdf_is_array(m_ctx, contents)) {
                pdf_array_push(m_ctx, contents, stream);
            } else {
                pdf_obj *arr = pdf_new_array(m_ctx, m_pdoc, 2);
                pdf_array_push(m_ctx, arr, contents);
                pdf_array_push(m_ctx, arr, stream);
                pdf_dict_put(m_ctx, pp->obj, PDF_NAME(Contents), arr);
                pdf_drop_obj(m_ctx, arr);
            }
            pdf_drop_obj(m_ctx, stream);
        } fz_catch(m_ctx) {
            qWarning() << "addWatermarkText page" << pn << fz_caught_message(m_ctx);
            ok = false;
        }
        fz_drop_page(m_ctx, (fz_page*)pp);
        invalidatePage(pn);
    }
    return ok;
}


// ──────────────────────────────────────────────
// Form fields
// ──────────────────────────────────────────────

QVector<PdfDocument::FormField> PdfDocument::getFormFields() const {
    QVector<FormField> fields;
    if (!m_pdoc) return fields;
    int n = pageCount();
    fz_try(m_ctx) {
        for (int pn = 0; pn < n; ++pn) {
            pdf_page *pp = pdf_load_page(m_ctx, m_pdoc, pn);
            for (pdf_annot *w = pdf_first_widget(m_ctx, pp); w;
                 w = pdf_next_widget(m_ctx, w)) {
                FormField f;
                f.pageNum = pn;
                pdf_obj *wobj = pdf_annot_obj(m_ctx, w);
                const char *nm = pdf_to_text_string(m_ctx,
                    pdf_dict_get(m_ctx, wobj, PDF_NAME(T)));
                f.name = nm ? QString::fromUtf8(nm) : QString("Field%1").arg(fields.size());
                f.type = (int)pdf_widget_type(m_ctx, w);
                const char *val = pdf_field_value(m_ctx, wobj);
                f.value = val ? QString::fromUtf8(val) : QString();
                fz_rect r = pdf_annot_rect(m_ctx, w);
                f.rect = QRectF(qMin(r.x0,r.x1), qMin(r.y0,r.y1),
                                qAbs(r.x1-r.x0), qAbs(r.y1-r.y0));
                fields.append(f);
            }
            fz_drop_page(m_ctx, (fz_page*)pp);
        }
    } fz_catch(m_ctx) {}
    return fields;
}

bool PdfDocument::hasFormFields() const {
    return !getFormFields().isEmpty();
}

bool PdfDocument::setFormField(int pageNum, const QString &name, const QString &value) {
    if (!m_pdoc) return false;
    bool ok = false;
    fz_try(m_ctx) {
        pdf_page *pp = pdf_load_page(m_ctx, m_pdoc, pageNum);
        for (pdf_annot *w = pdf_first_widget(m_ctx, pp); w;
             w = pdf_next_widget(m_ctx, w)) {
            pdf_obj *wobj = pdf_annot_obj(m_ctx, w);
            const char *nm = pdf_to_text_string(m_ctx,
                pdf_dict_get(m_ctx, wobj, PDF_NAME(T)));
            if (nm && QString::fromUtf8(nm) == name) {
                snapshot();
                pdf_set_field_value(m_ctx, m_pdoc,
                    wobj, value.toUtf8().constData(), 0);
                ok = true;
                break;
            }
        }
        fz_drop_page(m_ctx, (fz_page*)pp);
    } fz_catch(m_ctx) {
        qWarning() << "setFormField:" << fz_caught_message(m_ctx);
    }
    return ok;
}

// ──────────────────────────────────────────────
// Search
// ──────────────────────────────────────────────

QVector<QRectF> PdfDocument::searchText(int pageNum, const QString &query) const {
    QVector<QRectF> results;
    if (!m_doc || query.isEmpty()) return results;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageNum);
        const int MAX = 500;
        fz_quad hits[MAX];
        int n = fz_search_page(m_ctx, page, query.toUtf8().constData(), nullptr, hits, MAX);
        results.reserve(n);
        for (int i = 0; i < n; ++i) {
            fz_rect r = fz_rect_from_quad(hits[i]);
            results << QRectF(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
        }
        fz_drop_page(m_ctx, page);
    } fz_catch(m_ctx) {}
    return results;
}

// ──────────────────────────────────────────────
// Outline (bookmarks / table of contents)
// ──────────────────────────────────────────────

static void buildOutlineItems(fz_outline *o,
                              QVector<PdfDocument::OutlineItem> &out) {
    for (; o; o = o->next) {
        PdfDocument::OutlineItem item;
        item.title = o->title ? QString::fromUtf8(o->title) : QStringLiteral("(untitled)");
        item.page  = o->page.page; // 0-based, -1 if not set
        buildOutlineItems(o->down, item.children);
        out << std::move(item);
    }
}

QVector<PdfDocument::OutlineItem> PdfDocument::getOutline() const {
    QVector<OutlineItem> result;
    if (!m_doc) return result;
    fz_try(m_ctx) {
        fz_outline *outline = fz_load_outline(m_ctx, m_doc);
        if (outline) {
            buildOutlineItems(outline, result);
            fz_drop_outline(m_ctx, outline);
        }
    } fz_catch(m_ctx) {}
    return result;
}
