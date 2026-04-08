#include <assert.h>
#include <stdio.h>
#include "include/pdfpoppler.h"

#define PDF_PATH "../../../Downloads/PDFs/BNF 2020.pdf"

int main(void) {
    pdfpoppler_init();

    // Open a PDF document
    size_t n;
    PopplerDocument* doc = open_document(PDF_PATH, &n, NULL);
    assert(doc != NULL);

    printf("Number of pages: %zu\n", n);

    // Get the first page
    PopplerPage* page = poppler_document_get_page(doc, 0);
    assert(page != NULL);

    // Query page dimensions without rendering
    pdf_page_size_t size;
    if (get_page_size(page, &size)) {
        printf("Page 0 dimensions: %.1f x %.1f points\n", size.width, size.height);
    }

    // Render with default options (300 DPI)
    render_page_to_image(page, 800, 600, "output.png", NULL);
    poppler_page_to_pdf(page, "output.pdf", NULL);

    // Render with custom options (150 DPI, good antialias)
    render_opts_t low_dpi = RENDER_OPTS_DEFAULT;
    low_dpi.dpi = 150.0;
    low_dpi.antialias = CAIRO_ANTIALIAS_GOOD;
    render_page_to_image(page, 800, 600, "output_150dpi.png", &low_dpi);

    // Render page to in-memory PNG buffer
    pdf_buffer_t png_buf = {0};
    if (render_page_to_buffer(page, 800, 600, &png_buf, NULL)) {
        printf("PNG buffer size: %zu bytes\n", png_buf.size);
        free_pdf_buffer(&png_buf);
    }

    // Render page to gzip-compressed PNG buffer
    pdf_buffer_t gz_buf = {0};
    if (render_page_to_compressed_buffer(page, 800, 600, &gz_buf, NULL)) {
        printf("Compressed buffer size: %zu bytes\n", gz_buf.size);
        free_pdf_buffer(&gz_buf);
    }

    // Render page to in-memory PDF buffer (vector)
    pdf_buffer_t pdf_buf = {0};
    if (render_page_to_pdf_buffer(page, &pdf_buf)) {
        printf("PDF buffer size: %zu bytes\n", pdf_buf.size);
        free_pdf_buffer(&pdf_buf);
    }

    // Search for text on the page
    pdf_search_results_t search = {0};
    if (search_page_text(page, "the", &search)) {
        printf("Found %zu matches for 'the'\n", search.count);
        for (size_t i = 0; i < search.count && i < 3; i++) {
            printf("  Match %zu: (%.1f, %.1f) - (%.1f, %.1f)\n", i, search.results[i].x1,
                   search.results[i].y1, search.results[i].x2, search.results[i].y2);
        }
        free_search_results(&search);
    }

    g_object_unref(page);

    // Get PDF metadata
    pdf_metadata_t meta = {0};
    if (get_pdf_metadata(PDF_PATH, &meta)) {
        printf("Title: %s\n", meta.title ? meta.title : "(none)");
        printf("Author: %s\n", meta.author ? meta.author : "(none)");
        printf("Pages: %zu\n", meta.page_count);
        printf("PDF Version: %s\n", meta.pdf_version);
        free_pdf_metadata(&meta);
    }

    // Render pages 0-2 to a multi-page PDF
    if (n >= 3) {
        render_page_range_to_pdf(PDF_PATH, 0, 2, "pages_0_2.pdf", NULL);
        printf("Rendered pages 0-2 to pages_0_2.pdf\n");
    }

    // Extract table of contents
    pdf_outline_t outline = {0};
    if (get_pdf_outline_from_document(doc, &outline)) {
        printf("Outline entries: %zu\n", outline.num_entries);
        for (size_t i = 0; i < outline.num_entries && i < 5; i++) {
            printf("  [%zu] %s (page %d)\n", i,
                   outline.entries[i].title ? outline.entries[i].title : "(untitled)",
                   outline.entries[i].page_num);
        }
        free_pdf_outline(&outline);
    }

    close_document(doc);
    pdfpoppler_cleanup();
    return 0;
}
