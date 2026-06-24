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
    // Write to a temp file first, then rename — avoids any leftover file handles.
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
    opts.do_garbage = 0;
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
    }
    m_wordCache.clear();
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

QPixmap PdfDocument::renderPage(int pageNum, float zoom) const {
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
        fz_rect sel = fz_make_rect(qMin(x0,x1), qMin(y0,y1), qMax(x0,x1), qMax(y0,y1));
        // gather quads from words that intersect sel
        fz_page *page = (fz_page*)pp;
        fz_stext_options opts{};
        fz_stext_page *stext = fz_new_stext_page_from_page(m_ctx, page, &opts);
        QVector<fz_quad> quads;
        for (fz_stext_block *blk = stext->first_block; blk; blk = blk->next) {
            if (blk->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (fz_stext_line *ln = blk->u.t.first_line; ln; ln = ln->next) {
                for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                    fz_rect cr = fz_rect_from_quad(ch->quad);
                    if (!fz_is_empty_rect(fz_intersect_rect(cr, sel)))
                        quads.append(ch->quad);
                }
            }
        }
        fz_drop_stext_page(m_ctx, stext);

        pdf_annot *annot = pdf_create_annot(m_ctx, pp, PDF_ANNOT_HIGHLIGHT);
        if (!quads.isEmpty()) {
            pdf_set_annot_quad_points(m_ctx, annot,
                quads.size(), quads.constData());
        } else {
            // fallback: use the rect as a single quad
            fz_quad q = fz_quad_from_rect(sel);
            pdf_set_annot_quad_points(m_ctx, annot, 1, &q);
        }
        float color[3] = {r, g, b};
        pdf_set_annot_color(m_ctx, annot, 3, color);
        pdf_set_annot_opacity(m_ctx, annot, 0.5f);
        pdf_update_annot(m_ctx, annot);
        ok = true;
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
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    snapshot();
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
// Eraser
// ──────────────────────────────────────────────

bool PdfDocument::deleteAnnotAt(int pageNum, float x, float y) {
    pdf_page *pp = loadPdfPage(pageNum);
    if (!pp) return false;
    bool ok = false;
    fz_try(m_ctx) {
        // Get page height to try both coordinate orientations
        fz_rect mediabox;
        fz_matrix ctm;
        pdf_page_transform(m_ctx, pp, &mediabox, &ctm);
        float pageH = mediabox.y1 - mediabox.y0;

        fz_point pt      = {x, y};
        fz_point pt_flip = {x, pageH - y};

        QVector<pdf_annot*> annots;
        for (pdf_annot *a = pdf_first_annot(m_ctx, pp); a; a = pdf_next_annot(m_ctx, a))
            annots.prepend(a);

        for (pdf_annot *a : annots) {
            int atype = pdf_annot_type(m_ctx, a);
            bool hit = false;

            if (atype == PDF_ANNOT_INK) {
                // Direct proximity check against ink stroke vertices
                int ns = pdf_annot_ink_list_count(m_ctx, a);
                for (int s = 0; s < ns && !hit; s++) {
                    int np = pdf_annot_ink_list_stroke_count(m_ctx, a, s);
                    fz_point prev = {-1e9f, -1e9f};
                    for (int k = 0; k < np && !hit; k++) {
                        fz_point p = pdf_annot_ink_list_stroke_vertex(m_ctx, a, s, k);
                        if (hypotf(p.x - x, p.y - y) < 30.f) { hit = true; break; }
                        if (k > 0) {
                            float dx = p.x - prev.x, dy = p.y - prev.y;
                            float len2 = dx*dx + dy*dy;
                            if (len2 > 1.f) {
                                float t = ((x-prev.x)*dx + (y-prev.y)*dy) / len2;
                                t = (t < 0.f) ? 0.f : ((t > 1.f) ? 1.f : t);
                                float nx = prev.x + t*dx, ny = prev.y + t*dy;
                                if (hypotf(nx - x, ny - y) < 20.f) hit = true;
                            }
                        }
                        prev = p;
                    }
                }
            } else {
                fz_rect r = pdf_annot_rect(m_ctx, a);
                float rx0 = qMin(r.x0,r.x1), rx1 = qMax(r.x0,r.x1);
                float ry0 = qMin(r.y0,r.y1), ry1 = qMax(r.y0,r.y1);
                fz_rect hit_r = fz_make_rect(rx0-20, ry0-20, rx1+20, ry1+20);
                hit = fz_is_point_inside_rect(pt, hit_r) || fz_is_point_inside_rect(pt_flip, hit_r);
            }

            if (hit) {
                snapshot();
                pdf_delete_annot(m_ctx, pp, a);
                ok = true;
                break;
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

        // Register image as XObject in document
        pdf_obj *imgobj = pdf_add_image(m_ctx, m_pdoc, img);

        // Give it a unique name
        char imgname[32];
        snprintf(imgname, sizeof(imgname), "PEImg%d", m_imgSeq++);

        // Add to page resources/XObject dict
        pdf_obj *res = pdf_page_resources(m_ctx, pp);
        pdf_obj *xobj = pdf_dict_get(m_ctx, res, PDF_NAME(XObject));
        if (!xobj || !pdf_is_dict(m_ctx, xobj)) {
            xobj = pdf_new_dict(m_ctx, m_pdoc, 4);
            pdf_dict_put(m_ctx, res, PDF_NAME(XObject), xobj);
            pdf_drop_obj(m_ctx, xobj);
            xobj = pdf_dict_get(m_ctx, res, PDF_NAME(XObject));
        }
        pdf_dict_puts(m_ctx, xobj, imgname, imgobj);
        pdf_drop_obj(m_ctx, imgobj);

        // Page height for PDF coordinate flip (PDF y=0 is bottom)
        fz_rect pr = fz_bound_page(m_ctx, (fz_page*)pp);
        float ph = pr.y1 - pr.y0;
        float x0 = cx - w/2.f;
        float y0 = ph - (cy + h/2.f); // flip to PDF bottom-left origin

        // Write content stream fragment: q <matrix> cm /ImgName Do Q
        char snippet[256];
        snprintf(snippet, sizeof(snippet),
                 "q %.4g 0 0 %.4g %.4g %.4g cm /%s Do Q\n",
                 w, h, x0, y0, imgname);

        // Create new stream for the snippet
        fz_buffer *frag = fz_new_buffer_from_copied_data(m_ctx,
            reinterpret_cast<const unsigned char*>(snippet), strlen(snippet));
        pdf_obj *stream = pdf_add_stream(m_ctx, m_pdoc, frag, nullptr, 0);
        fz_drop_buffer(m_ctx, frag);
        frag = nullptr;

        // Append to page Contents
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

bool PdfDocument::duplicatePage(int pageNum) {
    if (!m_pdoc || pageNum >= pageCount()) return false;
    snapshot();
    bool ok = false;
    fz_try(m_ctx) {
        pdf_graft_page(m_ctx, m_pdoc, pageNum + 1, m_pdoc, pageNum);
        ok = true;
    } fz_catch(m_ctx) {
        qWarning() << "duplicatePage:" << fz_caught_message(m_ctx);
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
