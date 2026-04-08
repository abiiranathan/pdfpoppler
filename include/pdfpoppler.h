#ifndef BCFD4BF4_44B2_4B5B_837F_DAC6528A40E7
#define BCFD4BF4_44B2_4B5B_837F_DAC6528A40E7

#include <cairo/cairo-pdf.h>
#include <cairo/cairo.h>

#include <poppler/glib/poppler.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct MDocument {
    PopplerDocument* document;
    size_t num_pages;
} MDocument;

/**
 * Structure to hold in-memory PNG/PDF data.
 * Free with free_pdf_buffer() when done.
 */
typedef struct {
    unsigned char* data; /**< Raw PNG/PDF data. */
    size_t size;         /**< Size of the data in bytes. */
} pdf_buffer_t;

/**
 * Frees the data inside a pdf_buffer_t and zeroes it.
 *
 * @param buf The buffer to free. May be NULL.
 */
void free_pdf_buffer(pdf_buffer_t* buf);

#define PDF_DATE_SIZE 20    /**< "YYYY-MM-DD HH:MM:SS" + null */
#define PDF_VERSION_SIZE 16 /**< e.g. "PDF-2.0" + null */

/**
 * Structure to hold PDF document metadata.
 * Free with free_pdf_metadata() when done.
 */
typedef struct {
    gchar* title;                      /**< Allocated by poppler. Free with free_pdf_metadata(). */
    gchar* author;                     /**< Allocated by poppler. Free with free_pdf_metadata(). */
    gchar* subject;                    /**< Allocated by poppler. Free with free_pdf_metadata(). */
    gchar* keywords;                   /**< Allocated by poppler. Free with free_pdf_metadata(). */
    gchar* creator;                    /**< Allocated by poppler. Free with free_pdf_metadata(). */
    gchar* producer;                   /**< Allocated by poppler. Free with free_pdf_metadata(). */
    char creation_date[PDF_DATE_SIZE]; /**< Fixed buffer: "YYYY-MM-DD HH:MM:SS" */
    char mod_date[PDF_DATE_SIZE];      /**< Fixed buffer: "YYYY-MM-DD HH:MM:SS" */
    size_t page_count;
    bool is_encrypted;
    char pdf_version[PDF_VERSION_SIZE]; /**< Fixed buffer: e.g. "PDF-2.0" */
} pdf_metadata_t;

/**
 * Rendering options for configurable output.
 * Pass NULL to any function accepting render_opts_t* to use defaults.
 */
typedef struct {
    double dpi;                  /**< Resolution in DPI. Default: 300.0 */
    cairo_antialias_t antialias; /**< Cairo antialias mode. Default: CAIRO_ANTIALIAS_NONE */
    cairo_format_t pixel_format; /**< Cairo pixel format. Default: CAIRO_FORMAT_ARGB32 */
    double bg_red;               /**< Background red component [0..1]. Default: 1.0 */
    double bg_green;             /**< Background green component [0..1]. Default: 1.0 */
    double bg_blue;              /**< Background blue component [0..1]. Default: 1.0 */
} render_opts_t;

/** Default render options: 300 DPI, no antialias, ARGB32, white background. */
#define RENDER_OPTS_DEFAULT                                                                        \
    (render_opts_t){.dpi = 300.0,                                                                  \
                    .antialias = CAIRO_ANTIALIAS_NONE,                                             \
                    .pixel_format = CAIRO_FORMAT_ARGB32,                                           \
                    .bg_red = 1.0,                                                                 \
                    .bg_green = 1.0,                                                               \
                    .bg_blue = 1.0}

/**
 * Page dimensions in points (1 point = 1/72 inch).
 */
typedef struct {
    double width;
    double height;
} pdf_page_size_t;

/**
 * A single search result with text position on a page.
 */
typedef struct {
    double x1, y1, x2, y2; /**< Bounding rectangle in page points. */
} pdf_search_result_t;

/**
 * Array of search results returned by search_page_text().
 * Free with free_search_results().
 */
typedef struct {
    pdf_search_result_t* results; /**< Array of bounding rectangles. */
    size_t count;                 /**< Number of results. */
} pdf_search_results_t;

/**
 * A single entry in a PDF table of contents / outline.
 */
typedef struct pdf_outline_entry {
    char* title;                        /**< Bookmark title. Free with free_pdf_outline(). */
    int page_num;                       /**< Zero-based destination page, or -1 if unknown. */
    struct pdf_outline_entry* children; /**< Array of child entries. */
    size_t num_children;                /**< Number of child entries. */
} pdf_outline_entry_t;

/**
 * Complete table of contents for a PDF document.
 * Free with free_pdf_outline().
 */
typedef struct {
    pdf_outline_entry_t* entries; /**< Top-level outline entries. */
    size_t num_entries;           /**< Number of top-level entries. */
} pdf_outline_t;

// Initialize the pdfpoppler library.
void pdfpoppler_init(void);

// Cleanup the pdfpoppler library.
void pdfpoppler_cleanup(void);

/**
 * Opens a PDF document and returns a PopplerDocument object.
 *
 * @param filename Path to the PDF file to open.
 * @param num_pages Output parameter that receives the total number of pages.
 * @param password Password for encrypted PDFs, or NULL for unprotected files.
 * @return Pointer to PopplerDocument on success, NULL on failure.
 * @note Caller must call close_document() on the returned document when done.
 * @note Thread-safe.
 */
PopplerDocument* open_document(const char* filename, size_t* num_pages, const char* password);

// Close a PopplerDocument object.
void close_document(PopplerDocument* doc);

/**
 * Opens multiple documents in parallel and returns an array of MDocument objects.
 *
 * @param md Pre-allocated array of MDocument structs to populate.
 * @param filenames Array of file paths to open.
 * @param num_files Number of files to open.
 * @return true if all documents were opened successfully, false otherwise.
 * @note Free with free_documents() when done.
 */
bool open_documents(MDocument* md, const char** filenames, size_t num_files);

// Free multiple documents opened with open_documents.
void free_documents(MDocument** md, size_t num_files, bool free_array);

/**
 * Renders a Poppler page to a PDF file using Cairo.
 *
 * @param page The PopplerPage to render. Must not be NULL.
 * @param output_pdf Path where the output PDF will be written.
 * @param opts Render options, or NULL for defaults (300 DPI).
 * @return true on success, false on failure.
 * @note Thread-safe. Uses internal mutex to serialize Cairo operations.
 */
bool poppler_page_to_pdf(PopplerPage* page, const char* output_pdf, const render_opts_t* opts);

/**
 * Renders a PDF page to a PNG image file.
 *
 * @param page The PopplerPage to render. Must not be NULL.
 * @param width Width of the page in points (72 DPI).
 * @param height Height of the page in points (72 DPI).
 * @param output_file Path where the output PNG will be written.
 * @param opts Render options, or NULL for defaults.
 * @note Thread-safe. Uses internal mutex to serialize Cairo operations.
 */
void render_page_to_image(PopplerPage* page, int width, int height, const char* output_file,
                          const render_opts_t* opts);

/**
 * Renders a PDF page to a PNG image in memory.
 *
 * @param page The PopplerPage to render. Must not be NULL.
 * @param width Width of the page in points (72 DPI).
 * @param height Height of the page in points (72 DPI).
 * @param out_buffer Output parameter that receives the PNG data. Must not be NULL.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_to_buffer(PopplerPage* page, int width, int height, pdf_buffer_t* out_buffer,
                           const render_opts_t* opts);

/**
 * Renders a PDF page to a gzip-compressed PNG buffer.
 *
 * @param page The PopplerPage to render.
 * @param width Width in points (72 DPI).
 * @param height Height in points (72 DPI).
 * @param out_buffer Output buffer for gzip-compressed PNG data.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_to_compressed_buffer(PopplerPage* page, int width, int height,
                                      pdf_buffer_t* out_buffer, const render_opts_t* opts);

/**
 * Renders a single page from a PDF document to a PNG file.
 * Convenience function that opens, renders, and cleans up in one call.
 *
 * @param pdf_path Path to the input PDF file.
 * @param page_num Zero-based page number to render.
 * @param output_png Path where the output PNG will be written.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 */
bool render_page_from_document(const char* pdf_path, int page_num, const char* output_png,
                               const render_opts_t* opts);

/**
 * Renders a single page from a PDF document to a PNG buffer in memory.
 * Convenience function that opens, renders to memory, and cleans up.
 *
 * @param pdf_path Path to the input PDF file.
 * @param page_num Zero-based page number to render.
 * @param out_buffer Output parameter that receives the PNG data.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_from_document_to_buffer(const char* pdf_path, int page_num,
                                         pdf_buffer_t* out_buffer, const render_opts_t* opts);

/**
 * Renders a single page from a PDF document to a PDF file.
 *
 * @param pdf_path Path to the input PDF file.
 * @param page_num Zero-based page number to render.
 * @param output_pdf Path where the output PDF will be written.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 */
bool render_page_to_pdf(const char* pdf_path, int page_num, const char* output_pdf,
                        const render_opts_t* opts);

/**
 * Renders a page to an in-memory PDF buffer (vector output).
 *
 * @param page The PopplerPage to render.
 * @param out_buffer Output buffer for the PDF data.
 * @return true on success, false on failure.
 * @note Preserves vector content at 1:1 scale (72 DPI points).
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_to_pdf_buffer(PopplerPage* page, pdf_buffer_t* out_buffer);

/**
 * Renders a single page from a PDF document to an in-memory PDF buffer.
 * Convenience wrapper around render_page_to_pdf_buffer().
 *
 * @param pdf_path Path to the input PDF file.
 * @param page_num Zero-based page number to render.
 * @param out_buffer Output buffer for the PDF data.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_from_document_to_pdf_buffer(const char* pdf_path, int page_num,
                                             pdf_buffer_t* out_buffer);

// Read the whole PDF file in parallel extracting the text from each page.
char** read_pdf_text(const char* filename, size_t* num_pages, size_t num_threads);

// Free the memory allocated for the text extracted from a PDF file.
void free_pdf_text(char** text, size_t num_pages);

// Get page text from a PopplerPage object.
char* get_page_text(PopplerPage* page);

// Free the page text.
void free_page_text(char* text);

/**
 * Retrieves metadata from a PDF file.
 *
 * @param filename Path to the PDF file.
 * @param out_meta Output parameter for the metadata. Must not be NULL.
 * @return true on success, false on failure.
 * @note Free out_meta fields with free_pdf_metadata() when done.
 */
bool get_pdf_metadata(const char* filename, pdf_metadata_t* out_meta);

/**
 * Frees all fields in a pdf_metadata_t structure.
 * Zeroes the struct to prevent double-frees.
 *
 * @param meta The metadata structure to free. May be NULL.
 */
void free_pdf_metadata(pdf_metadata_t* meta);

/* ==========================================================================
 * Page Dimensions
 * ========================================================================== */

/**
 * Gets the dimensions of a page from a PopplerPage object.
 *
 * @param page The PopplerPage. Must not be NULL.
 * @param out_size Output for page dimensions. Must not be NULL.
 * @return true on success, false on failure.
 */
bool get_page_size(PopplerPage* page, pdf_page_size_t* out_size);

/**
 * Gets the dimensions of a page by page number from a PDF file.
 *
 * @param pdf_path Path to the PDF file.
 * @param page_num Zero-based page number.
 * @param out_size Output for page dimensions. Must not be NULL.
 * @return true on success, false on failure.
 */
bool get_page_size_from_document(const char* pdf_path, int page_num, pdf_page_size_t* out_size);

/* ==========================================================================
 * Page Range Rendering
 * ========================================================================== */

/**
 * Renders a range of pages from a PDF document to a multi-page PDF file.
 *
 * @param pdf_path Path to the input PDF file.
 * @param start_page Zero-based start page (inclusive).
 * @param end_page Zero-based end page (inclusive).
 * @param output_pdf Path where the output multi-page PDF will be written.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 */
bool render_page_range_to_pdf(const char* pdf_path, int start_page, int end_page,
                              const char* output_pdf, const render_opts_t* opts);

/**
 * Renders a range of pages from a PDF document to an in-memory multi-page PDF buffer.
 *
 * @param pdf_path Path to the input PDF file.
 * @param start_page Zero-based start page (inclusive).
 * @param end_page Zero-based end page (inclusive).
 * @param out_buffer Output buffer for the PDF data.
 * @param opts Render options, or NULL for defaults.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool render_page_range_to_pdf_buffer(const char* pdf_path, int start_page, int end_page,
                                     pdf_buffer_t* out_buffer, const render_opts_t* opts);

/* ==========================================================================
 * PDF Merging
 * ========================================================================== */

/**
 * Merges multiple PDF files into a single output PDF.
 *
 * @param pdf_paths Array of paths to PDF files.
 * @param num_files Number of files in the array.
 * @param output_pdf Path where the merged PDF will be written.
 * @return true on success, false on failure.
 */
bool merge_pdfs(const char** pdf_paths, size_t num_files, const char* output_pdf);

/**
 * Merges multiple PDF files into an in-memory PDF buffer.
 *
 * @param pdf_paths Array of paths to PDF files.
 * @param num_files Number of files in the array.
 * @param out_buffer Output buffer for the merged PDF data.
 * @return true on success, false on failure.
 * @note Caller must call free_pdf_buffer() when done.
 */
bool merge_pdfs_to_buffer(const char** pdf_paths, size_t num_files, pdf_buffer_t* out_buffer);

/* ==========================================================================
 * Text Search
 * ========================================================================== */

/**
 * Searches for text on a page and returns bounding rectangles for each match.
 *
 * @param page The PopplerPage to search.
 * @param text The text to search for.
 * @param out_results Output for search results. Must not be NULL.
 * @return true if any results found, false otherwise or on error.
 * @note Caller must call free_search_results() when done.
 */
bool search_page_text(PopplerPage* page, const char* text, pdf_search_results_t* out_results);

/**
 * Searches for text on a page by page number from a PDF file.
 *
 * @param pdf_path Path to the PDF file.
 * @param page_num Zero-based page number.
 * @param text The text to search for.
 * @param out_results Output for search results.
 * @return true if any results found, false otherwise or on error.
 * @note Caller must call free_search_results() when done.
 */
bool search_page_text_from_document(const char* pdf_path, int page_num, const char* text,
                                    pdf_search_results_t* out_results);

/**
 * Frees search results returned by search_page_text().
 *
 * @param results The results to free. May be NULL.
 */
void free_search_results(pdf_search_results_t* results);

/* ==========================================================================
 * Table of Contents / Outline
 * ========================================================================== */

/**
 * Extracts the table of contents (outline/bookmarks) from a PDF file.
 *
 * @param pdf_path Path to the PDF file.
 * @param out_outline Output for the outline. Must not be NULL.
 * @return true on success (even if outline is empty), false on error.
 * @note Caller must call free_pdf_outline() when done.
 */
bool get_pdf_outline(const char* pdf_path, pdf_outline_t* out_outline);

/**
 * Extracts the table of contents from an already-opened document.
 *
 * @param doc The PopplerDocument.
 * @param out_outline Output for the outline.
 * @return true on success, false on error.
 * @note Caller must call free_pdf_outline() when done.
 */
bool get_pdf_outline_from_document(PopplerDocument* doc, pdf_outline_t* out_outline);

/**
 * Frees a pdf_outline_t and all its children recursively.
 *
 * @param outline The outline to free. May be NULL.
 */
void free_pdf_outline(pdf_outline_t* outline);

// Memory management helpers
#define AutoCloseDoc __attribute__((cleanup(free_document_cleanup)))
#define AutoClosePage __attribute__((cleanup(free_page_cleanup)))

static inline void free_document_cleanup(PopplerDocument** doc) {
    if (*doc) {
        close_document(*doc);
    }
}

static inline void free_page_cleanup(PopplerPage** page) {
    if (*page) {
        g_object_unref(*page);
    }
}

#endif /* BCFD4BF4_44B2_4B5B_837F_DAC6528A40E7 */
