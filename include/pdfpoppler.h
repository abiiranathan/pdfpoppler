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
    int num_pages;
} MDocument;

// Initialize the pdfpoppler library.
void pdfpoppler_init(void);

// Cleanup the pdfpoppler library.
void pdfpoppler_cleanup(void);

// Open a PDF document and return a PopplerDocument object.
// The number of pages in the document is stored in the num_pages parameter.
// If the document is password-protected, the password parameter must be set to the password,
// otherwise it should be set to NULL.
PopplerDocument* open_document(const char* filename, int* num_pages, const char* password);

// Close a PopplerDocument object.
void close_document(PopplerDocument* doc);

// Open multiple documents in parallel and return an array of MDocument objects.
// The number of pages in each document is stored in the num_pages in each MDocument object.
// Returns true if all documents were opened successfully, false otherwise.
// The array of MDocument objects must be freed by calling free_documents.
bool open_documents(MDocument* md, const char** filenames, size_t num_files);

// free multiple documents opened with open_documents.
void free_documents(MDocument** md, size_t num_files, bool free_array);

// Render a pdf of a Poppler Page with cairo.
bool poppler_page_to_pdf(PopplerPage* page, const char* output_pdf);

// Render a page of a PDF document to a PNG image.
// The image is scaled to fit within the specified width and height.
// This function is thread-safe and uses a mutex to prevent concurrent access to the cairo library.
void render_page_to_image(PopplerPage* page, int width, int height, const char* output_file);

// Render a PNG image of a page at page_num from a PDF document pointed to by pdf_path.
// The image is scaled to fit within the specified width and height.
// Returns true if the page was rendered successfully, false otherwise.
bool render_page_from_document(const char* pdf_path, int page_num, const char* output_png);

// Render a PDF of a page at page_num from a PDF document pointed to by pdf_path.
// Returns true if the pdf was rendered successfully, false otherwise.
bool render_page_to_pdf(const char* pdf_path, int page_num, const char* output_pdf);

// Read the whole PDF file in parallel extracting the text from each page.
// The number of pages in the PDF file is stored in the num_pages parameter.
// The text extracted from each page is stored in an array of strings that
// must be freed by calling free_pdf_text.
char** read_pdf_text(const char* filename, int* num_pages, int num_threads);

// free_pdf_text frees the memory allocated for the text extracted from a PDF file.
void free_pdf_text(char** text, int num_pages);

// Get page text from a PopplerPage object
char* get_page_text(PopplerPage* page);

// free the page text
void free_page_text(char* text);

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
