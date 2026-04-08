#include "../include/pdfpoppler.h"
#include <glib.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#ifdef _WIN32
static CRITICAL_SECTION cairo_mutex;
#else
static pthread_mutex_t cairo_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// Define cross-platform mutex functions in separate functions
static void init_cairo_mutex(void) {
#ifdef _WIN32
    InitializeCriticalSection(&cairo_mutex);
#else
    pthread_mutex_init(&cairo_mutex, NULL);
#endif
}

// Destroy the mutex when it is no longer needed.
static void destroy_cairo_mutex() {
#ifdef _WIN32
    DeleteCriticalSection(&cairo_mutex);
#else
    pthread_mutex_destroy(&cairo_mutex);
#endif
}

// Initialize the pdfpoppler library.
void pdfpoppler_init(void) {
    setlocale(LC_ALL, "");
    init_cairo_mutex();
}

void pdfpoppler_cleanup(void) {
    destroy_cairo_mutex();
}

// lock the mutex
static void lock_cairo_mutex() {
#ifdef _WIN32
    EnterCriticalSection(&cairo_mutex);
#else
    pthread_mutex_lock(&cairo_mutex);
#endif
}

// unlock the mutex
static void unlock_cairo_mutex() {
#ifdef _WIN32
    LeaveCriticalSection(&cairo_mutex);
#else
    pthread_mutex_unlock(&cairo_mutex);
#endif
}

// Open a PDF document and return the number of pages
PopplerDocument* open_document(const char* filename, size_t* num_pages, const char* password) {
    GFile* file = g_file_new_for_path(filename);
    if (file == NULL) {
        return NULL;
    }

    GError* error = NULL;
    GBytes* bytes = g_file_load_bytes(file, NULL, NULL, &error);
    g_object_unref(file);

    if (error != NULL) {
        g_print("%s\n", error->message);
        g_clear_error(&error);
        return NULL;
    }

    PopplerDocument* doc = poppler_document_new_from_bytes(bytes, password, &error);
    if (doc == NULL) {
        if (error != NULL) {
            g_print("%s\n", error->message);
            g_clear_error(&error);
        }
        g_bytes_unref(bytes);
        *num_pages = 0;
        return NULL;
    }

    *num_pages = (size_t)poppler_document_get_n_pages(doc);
    g_bytes_unref(bytes);
    return doc;
}

// Close a PopplerDocument object.
void close_document(PopplerDocument* doc) {
    g_object_unref(doc);
}

// Resolve render options, falling back to defaults if NULL.
static render_opts_t resolve_opts(const render_opts_t* opts) {
    if (opts != NULL) {
        return *opts;
    }
    return RENDER_OPTS_DEFAULT;
}

void render_page_to_image(PopplerPage* page, int width, int height, const char* output_file,
                          const render_opts_t* opts) {
    render_opts_t o = resolve_opts(opts);
    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    // Lock the mutex before creating the Cairo surface
    lock_cairo_mutex();

    // Create the Cairo surface with the specified resolution
    cairo_surface_t* surface =
        cairo_image_surface_create(o.pixel_format, pixel_width, pixel_height);
    if (surface == NULL) {
        unlock_cairo_mutex();
        puts("Unable to create cairo surface");
        return;
    }

    cairo_t* cr = cairo_create(surface);
    if (cr == NULL) {
        cairo_surface_destroy(surface);
        unlock_cairo_mutex();
        puts("Error: could not create cairo context");
        return;
    }

    // Set the background color
    cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
    cairo_paint(cr);

    cairo_set_antialias(cr, o.antialias);

    // Calculate the scaling factors to maintain the original aspect ratio
    double scale_x = (double)pixel_width / width;
    double scale_y = (double)pixel_height / height;
    cairo_scale(cr, scale_x, scale_y);

    poppler_page_render(page, cr);

    // Unlock the mutex after rendering the page
    unlock_cairo_mutex();

    cairo_status_t status = cairo_surface_write_to_png(surface, output_file);
    if (status != CAIRO_STATUS_SUCCESS) {
        puts("Error: could not write to png file");
    }

    cairo_surface_destroy(surface);
    cairo_destroy(cr);
}

// Render a single page from a document. Avoids multiple cgo calls
bool render_page_from_document(const char* pdf_path, int page_num, const char* output_png,
                               const render_opts_t* opts) {
    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        puts("Error opening document");
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        puts("Page number is out of range of this document");
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        printf("PopplerPage for page %d is NULL\n", page_num);
        g_object_unref(doc);
        return false;
    }

    double width, height;
    poppler_page_get_size(page, &width, &height);

    render_page_to_image(page, (int)width, (int)height, output_png, opts);
    g_object_unref(doc);
    g_object_unref(page);
    return true;
}

// Render a pdf of a Poppler Page with cairo
bool poppler_page_to_pdf(PopplerPage* page, const char* output_pdf, const render_opts_t* opts) {
    cairo_surface_t* surface;
    cairo_t* cr;

    double width, height;
    poppler_page_get_size(page, &width, &height);

    render_opts_t o = resolve_opts(opts);
    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    lock_cairo_mutex();

    surface = cairo_pdf_surface_create(output_pdf, pixel_width, pixel_height);
    cairo_status_t status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        printf("Error creating PDF surface: %s\n", cairo_status_to_string(status));
        unlock_cairo_mutex();
        return false;
    }

    cr = cairo_create(surface);
    status = cairo_status(cr);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        printf("Error creating cairo context: %s\n", cairo_status_to_string(status));
        unlock_cairo_mutex();
        return false;
    }

    // Set the background color
    cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
    cairo_paint(cr);

    cairo_scale(cr, pixel_width / width, pixel_height / height);

    poppler_page_render(page, cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    cairo_destroy(cr);
    unlock_cairo_mutex();
    return true;
}

// Takes in the page num, pdf path, outpdf path and renders the page to a pdf
// Calls poppler_page_to_pdf to render the page but avoids multiple cgo calls
// to open the document and get the page.
// Returns true if the page was rendered successfully, false otherwise.
bool render_page_to_pdf(const char* pdf_path, int page_num, const char* output_pdf,
                        const render_opts_t* opts) {
    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        puts("Error opening document");
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        puts("Page number is out of range of this document");
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        printf("PopplerPage for page %d is NULL\n", page_num);
        g_object_unref(doc);
        return false;
    }

    bool status = poppler_page_to_pdf(page, output_pdf, opts);
    g_object_unref(doc);
    g_object_unref(page);
    return status;
}

static void process_page(PopplerPage* page, gpointer user_data) {
    GPtrArray* text_array = (GPtrArray*)user_data;
    char* text = poppler_page_get_text(page);
    if (text != NULL) {
        g_ptr_array_add(text_array, g_strdup(text));
        g_free(text);
    }
}

char** read_pdf_text(const char* filename, size_t* num_pages, size_t num_threads) {
    PopplerDocument* doc = open_document(filename, num_pages, NULL);
    if (doc == NULL) {
        puts("Error opening document");
        return NULL;
    }

    *num_pages = (size_t)poppler_document_get_n_pages(doc);
    GPtrArray* text_array = g_ptr_array_new();
    GThreadPool* pool =
        g_thread_pool_new((GFunc)process_page, text_array, (gint)num_threads, TRUE, NULL);
    for (size_t i = 0; i < *num_pages; i++) {
        PopplerPage* page = poppler_document_get_page(doc, (int)i);
        g_thread_pool_push(pool, page, NULL);
    }

    g_thread_pool_free(pool, FALSE, TRUE);

    // Steal pointers directly from the array (no double-copy)
    char** text = (char**)malloc(text_array->len * sizeof(char*));
    for (guint i = 0; i < text_array->len; i++) {
        text[i] = g_ptr_array_index(text_array, i);
    }

    // Free the array container only, not the elements (we stole them)
    g_ptr_array_free(text_array, FALSE);
    g_object_unref(doc);
    return text;
}

void free_pdf_text(char** text, size_t num_pages) {
    if (text == NULL) {
        return;
    }

    for (size_t i = 0; i < num_pages; i++) {
        g_free(text[i]);
    }
    g_free(text);
}

struct OpenDocumentArgs {
    const char* filename;
    PopplerDocument* doc;
    size_t* num_pages;
};

void* async_open_document(struct OpenDocumentArgs* args) {
    PopplerDocument* doc = open_document(args->filename, args->num_pages, NULL);
    return doc;
}

bool open_documents(MDocument* md, const char** filenames, size_t num_files) {
    GThread** threads = g_new(GThread*, num_files);
    if (threads == NULL) {
        return false;
    }
    memset(md, 0, num_files * sizeof(MDocument));

    for (size_t i = 0; i < num_files; i++) {
        struct OpenDocumentArgs* args = g_new(struct OpenDocumentArgs, 1);
        args->filename = filenames[i];
        args->num_pages = &md[i].num_pages;
        threads[i] = g_thread_new(NULL, (GThreadFunc)async_open_document, (gpointer)args);
    }

    for (size_t i = 0; i < num_files; i++) {
        PopplerDocument* doc = g_thread_join(threads[i]);
        if (doc == NULL) {
            // free the documents that have been opened so far
            for (size_t j = 0; j < i; j++) {
                g_object_unref(md[j].document);
            }
            g_free(threads);
            return false;
        }
        md[i].document = doc;
    }
    g_free(threads);
    return true;
}

void free_documents(MDocument** md, size_t num_files, bool free_array) {
    if (md == NULL)
        return;

    for (size_t i = 0; i < num_files; i++) {
        g_object_unref(md[i]->document);
    }

    if (free_array) {
        free(md);
        md = NULL;
    }
}

// Get page text from a PopplerPage object
char* get_page_text(PopplerPage* page) {
    return poppler_page_get_text(page);
}

// free the page text
void free_page_text(char* text) {
    g_free(text);
}

void free_pdf_buffer(pdf_buffer_t* buf) {
    if (buf == NULL)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
}

/* ==========================================================================
 * In-Memory Buffer Rendering
 * ========================================================================== */

/**
 * Internal structure used for collecting PNG/PDF data from Cairo's write callback.
 */
typedef struct {
    unsigned char* data;
    size_t size;
    size_t capacity;
} cairo_write_context_t;

/**
 * Cairo write callback that appends data to a dynamically growing buffer.
 */
static cairo_status_t cairo_write_to_buffer(void* closure, const unsigned char* data,
                                            unsigned int length) {
    cairo_write_context_t* ctx = (cairo_write_context_t*)closure;

    size_t required = ctx->size + length;
    if (required > ctx->capacity) {
        size_t new_capacity = ctx->capacity == 0 ? 4096 : ctx->capacity * 2;
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        unsigned char* new_data = realloc(ctx->data, new_capacity);
        if (new_data == NULL) {
            return CAIRO_STATUS_WRITE_ERROR;
        }

        ctx->data = new_data;
        ctx->capacity = new_capacity;
    }

    memcpy(ctx->data + ctx->size, data, length);
    ctx->size += length;

    return CAIRO_STATUS_SUCCESS;
}

bool render_page_to_buffer(PopplerPage* page, int width, int height, pdf_buffer_t* out_buffer,
                           const render_opts_t* opts) {
    if (page == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_to_buffer\n");
        return false;
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Invalid dimensions (%d x %d)\n", width, height);
        return false;
    }

    render_opts_t o = resolve_opts(opts);
    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    lock_cairo_mutex();

    cairo_surface_t* surface =
        cairo_image_surface_create(o.pixel_format, pixel_width, pixel_height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo surface\n");
        unlock_cairo_mutex();
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo context\n");
        cairo_surface_destroy(surface);
        unlock_cairo_mutex();
        return false;
    }

    cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
    cairo_paint(cr);
    cairo_set_antialias(cr, o.antialias);

    double scale_x = (double)pixel_width / width;
    double scale_y = (double)pixel_height / height;
    cairo_scale(cr, scale_x, scale_y);

    poppler_page_render(page, cr);

    unlock_cairo_mutex();

    cairo_write_context_t write_ctx = {.data = NULL, .size = 0, .capacity = 0};
    cairo_status_t status =
        cairo_surface_write_to_png_stream(surface, cairo_write_to_buffer, &write_ctx);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Could not write PNG to buffer: %s\n",
                cairo_status_to_string(status));
        free(write_ctx.data);
        return false;
    }

    out_buffer->data = write_ctx.data;
    out_buffer->size = write_ctx.size;
    return true;
}

bool render_page_from_document_to_buffer(const char* pdf_path, int page_num,
                                         pdf_buffer_t* out_buffer, const render_opts_t* opts) {
    if (pdf_path == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_from_document_to_buffer\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        fprintf(stderr, "Error: Could not open document: %s\n", pdf_path);
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        fprintf(stderr, "Error: Page number %d is out of range (0-%zu)\n", page_num, num_pages - 1);
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        fprintf(stderr, "Error: Could not get page %d\n", page_num);
        g_object_unref(doc);
        return false;
    }

    double width, height;
    poppler_page_get_size(page, &width, &height);

    bool result = render_page_to_buffer(page, (int)width, (int)height, out_buffer, opts);

    g_object_unref(page);
    g_object_unref(doc);
    return result;
}

/* ==========================================================================
 * Gzip-Compressed Buffer Rendering
 * ========================================================================== */

typedef struct {
    z_stream stream;
    unsigned char* data;
    size_t size;
    size_t capacity;
    bool initialized;
    bool error_occurred;
} cairo_gzip_write_context_t;

static cairo_status_t cairo_write_to_gzip_buffer(void* closure, const unsigned char* data,
                                                 unsigned int length) {
    cairo_gzip_write_context_t* ctx = (cairo_gzip_write_context_t*)closure;

    if (ctx->error_occurred) {
        return CAIRO_STATUS_WRITE_ERROR;
    }

    if (!ctx->initialized) {
        ctx->stream = (z_stream){0};
        int ret = deflateInit2(&ctx->stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                               Z_DEFAULT_STRATEGY);
        if (ret != Z_OK) {
            fprintf(stderr, "Error: Failed to initialize gzip compression: %d\n", ret);
            ctx->error_occurred = true;
            return CAIRO_STATUS_WRITE_ERROR;
        }
        ctx->initialized = true;

        ctx->capacity = 8192;
        ctx->data = malloc(ctx->capacity);
        if (ctx->data == NULL) {
            deflateEnd(&ctx->stream);
            ctx->error_occurred = true;
            return CAIRO_STATUS_WRITE_ERROR;
        }
    }

    ctx->stream.next_in = (unsigned char*)data;
    ctx->stream.avail_in = length;

    while (ctx->stream.avail_in > 0) {
        if (ctx->size >= ctx->capacity) {
            size_t new_capacity = ctx->capacity * 2;
            unsigned char* new_data = realloc(ctx->data, new_capacity);
            if (new_data == NULL) {
                ctx->error_occurred = true;
                return CAIRO_STATUS_WRITE_ERROR;
            }
            ctx->data = new_data;
            ctx->capacity = new_capacity;
        }

        ctx->stream.next_out = ctx->data + ctx->size;
        ctx->stream.avail_out = (uInt)(ctx->capacity - ctx->size);

        int ret = deflate(&ctx->stream, Z_NO_FLUSH);
        if (ret != Z_OK) {
            fprintf(stderr, "Error: Compression failed during deflate: %d\n", ret);
            ctx->error_occurred = true;
            return CAIRO_STATUS_WRITE_ERROR;
        }

        ctx->size = ctx->capacity - ctx->stream.avail_out;
    }

    return CAIRO_STATUS_SUCCESS;
}

static bool finalize_gzip_compression(cairo_gzip_write_context_t* ctx) {
    if (!ctx->initialized || ctx->error_occurred) {
        return false;
    }

    ctx->stream.avail_in = 0;
    int ret;
    do {
        if (ctx->size >= ctx->capacity) {
            size_t new_capacity = ctx->capacity * 2;
            unsigned char* new_data = realloc(ctx->data, new_capacity);
            if (new_data == NULL) {
                return false;
            }
            ctx->data = new_data;
            ctx->capacity = new_capacity;
        }

        ctx->stream.next_out = ctx->data + ctx->size;
        ctx->stream.avail_out = (uInt)(ctx->capacity - ctx->size);

        ret = deflate(&ctx->stream, Z_FINISH);
        ctx->size = ctx->capacity - ctx->stream.avail_out;

        if (ret != Z_STREAM_END && ret != Z_OK) {
            fprintf(stderr, "Error: Compression failed during finalization: %d\n", ret);
            return false;
        }
    } while (ret != Z_STREAM_END);

    deflateEnd(&ctx->stream);
    return true;
}

bool render_page_to_compressed_buffer(PopplerPage* page, int width, int height,
                                      pdf_buffer_t* out_buffer, const render_opts_t* opts) {
    if (page == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_to_compressed_buffer\n");
        return false;
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Invalid dimensions (%d x %d)\n", width, height);
        return false;
    }

    render_opts_t o = resolve_opts(opts);
    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    lock_cairo_mutex();

    cairo_surface_t* surface =
        cairo_image_surface_create(o.pixel_format, pixel_width, pixel_height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo surface\n");
        unlock_cairo_mutex();
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo context\n");
        cairo_surface_destroy(surface);
        unlock_cairo_mutex();
        return false;
    }

    cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
    cairo_paint(cr);
    cairo_set_antialias(cr, o.antialias);

    double scale_x = (double)pixel_width / width;
    double scale_y = (double)pixel_height / height;
    cairo_scale(cr, scale_x, scale_y);

    poppler_page_render(page, cr);

    unlock_cairo_mutex();

    cairo_gzip_write_context_t write_ctx = {.stream = {0},
                                            .data = NULL,
                                            .size = 0,
                                            .capacity = 0,
                                            .initialized = false,
                                            .error_occurred = false};

    cairo_status_t status =
        cairo_surface_write_to_png_stream(surface, cairo_write_to_gzip_buffer, &write_ctx);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Could not write PNG to buffer: %s\n",
                cairo_status_to_string(status));
        if (write_ctx.initialized) {
            deflateEnd(&write_ctx.stream);
        }
        free(write_ctx.data);
        return false;
    }

    if (!finalize_gzip_compression(&write_ctx)) {
        fprintf(stderr, "Error: Failed to finalize gzip compression\n");
        free(write_ctx.data);
        return false;
    }

    out_buffer->data = write_ctx.data;
    out_buffer->size = write_ctx.size;
    return true;
}

/* ==========================================================================
 * PDF Metadata
 * ========================================================================== */

static void format_time_property(time_t t, char* buf, size_t buf_size) {
    buf[0] = '\0';
    if (t == 0)
        return;

    GDateTime* dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL)
        return;

    char* formatted = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref(dt);

    if (formatted != NULL) {
        g_snprintf(buf, buf_size, "%s", formatted);
        g_free(formatted);
    }
}

void free_pdf_metadata(pdf_metadata_t* meta) {
    if (meta == NULL)
        return;
    g_free(meta->title);
    g_free(meta->author);
    g_free(meta->subject);
    g_free(meta->keywords);
    g_free(meta->creator);
    g_free(meta->producer);
    memset(meta, 0, sizeof(pdf_metadata_t));
}

bool get_pdf_metadata(const char* filename, pdf_metadata_t* out_meta) {
    if (filename == NULL || out_meta == NULL) {
        fprintf(stderr, "Error: Invalid parameters to get_pdf_metadata\n");
        return false;
    }

    memset(out_meta, 0, sizeof(pdf_metadata_t));

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(filename, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    out_meta->page_count = num_pages;

    const char* ver_str = poppler_document_get_pdf_version_string(doc);
    if (ver_str) {
        g_snprintf(out_meta->pdf_version, PDF_VERSION_SIZE, "%s", ver_str);
    }
    out_meta->is_encrypted = false;

    out_meta->title = poppler_document_get_title(doc);
    out_meta->author = poppler_document_get_author(doc);
    out_meta->subject = poppler_document_get_subject(doc);
    out_meta->keywords = poppler_document_get_keywords(doc);
    out_meta->creator = poppler_document_get_creator(doc);
    out_meta->producer = poppler_document_get_producer(doc);

    time_t create_time = poppler_document_get_creation_date(doc);
    time_t mod_time = poppler_document_get_modification_date(doc);

    format_time_property(create_time, out_meta->creation_date, PDF_DATE_SIZE);
    format_time_property(mod_time, out_meta->mod_date, PDF_DATE_SIZE);

    g_object_unref(doc);
    return true;
}

/* ==========================================================================
 * Vector PDF Buffer Rendering
 * ========================================================================== */

bool render_page_to_pdf_buffer(PopplerPage* page, pdf_buffer_t* out_buffer) {
    if (page == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_to_pdf_buffer\n");
        return false;
    }

    double width, height;
    poppler_page_get_size(page, &width, &height);

    lock_cairo_mutex();

    cairo_write_context_t write_ctx = {.data = NULL, .size = 0, .capacity = 0};

    cairo_surface_t* surface =
        cairo_pdf_surface_create_for_stream(cairo_write_to_buffer, &write_ctx, width, height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo PDF surface\n");
        free(write_ctx.data);
        unlock_cairo_mutex();
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo context\n");
        cairo_surface_destroy(surface);
        free(write_ctx.data);
        unlock_cairo_mutex();
        return false;
    }

    poppler_page_render(page, cr);

    cairo_surface_finish(surface);

    cairo_status_t status = cairo_surface_status(surface);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    unlock_cairo_mutex();

    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Cairo surface finish failed: %s\n", cairo_status_to_string(status));
        free(write_ctx.data);
        return false;
    }

    out_buffer->data = write_ctx.data;
    out_buffer->size = write_ctx.size;
    return true;
}

bool render_page_from_document_to_pdf_buffer(const char* pdf_path, int page_num,
                                             pdf_buffer_t* out_buffer) {
    if (pdf_path == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        fprintf(stderr, "Error: Could not open document: %s\n", pdf_path);
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        fprintf(stderr, "Error: Page number %d is out of range (0-%zu)\n", page_num, num_pages - 1);
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        fprintf(stderr, "Error: Could not get page %d\n", page_num);
        g_object_unref(doc);
        return false;
    }

    bool result = render_page_to_pdf_buffer(page, out_buffer);

    g_object_unref(page);
    g_object_unref(doc);
    return result;
}

/* ==========================================================================
 * Page Dimensions
 * ========================================================================== */

bool get_page_size(PopplerPage* page, pdf_page_size_t* out_size) {
    if (page == NULL || out_size == NULL) {
        fprintf(stderr, "Error: Invalid parameters to get_page_size\n");
        return false;
    }
    poppler_page_get_size(page, &out_size->width, &out_size->height);
    return true;
}

bool get_page_size_from_document(const char* pdf_path, int page_num, pdf_page_size_t* out_size) {
    if (pdf_path == NULL || out_size == NULL) {
        fprintf(stderr, "Error: Invalid parameters to get_page_size_from_document\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        fprintf(stderr, "Error: Page number %d is out of range (0-%zu)\n", page_num, num_pages - 1);
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        g_object_unref(doc);
        return false;
    }

    poppler_page_get_size(page, &out_size->width, &out_size->height);
    g_object_unref(page);
    g_object_unref(doc);
    return true;
}

/* ==========================================================================
 * Page Range Rendering
 * ========================================================================== */

bool render_page_range_to_pdf(const char* pdf_path, int start_page, int end_page,
                              const char* output_pdf, const render_opts_t* opts) {
    if (pdf_path == NULL || output_pdf == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_range_to_pdf\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    if (start_page < 0 || (size_t)end_page >= num_pages || start_page > end_page) {
        fprintf(stderr, "Error: Invalid page range [%d, %d] for document with %zu pages\n",
                start_page, end_page, num_pages);
        g_object_unref(doc);
        return false;
    }

    render_opts_t o = resolve_opts(opts);

    // Get first page dimensions for initial surface size
    PopplerPage* first_page = poppler_document_get_page(doc, start_page);
    if (first_page == NULL) {
        g_object_unref(doc);
        return false;
    }

    double width, height;
    poppler_page_get_size(first_page, &width, &height);
    g_object_unref(first_page);

    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    lock_cairo_mutex();

    cairo_surface_t* surface = cairo_pdf_surface_create(output_pdf, pixel_width, pixel_height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create PDF surface\n");
        unlock_cairo_mutex();
        g_object_unref(doc);
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Cairo context\n");
        cairo_surface_destroy(surface);
        unlock_cairo_mutex();
        g_object_unref(doc);
        return false;
    }

    for (int i = start_page; i <= end_page; i++) {
        PopplerPage* page = poppler_document_get_page(doc, i);
        if (page == NULL) {
            fprintf(stderr, "Error: Could not get page %d\n", i);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            unlock_cairo_mutex();
            g_object_unref(doc);
            return false;
        }

        double pw, ph;
        poppler_page_get_size(page, &pw, &ph);
        int ppw = (int)(pw * o.dpi / 72.0);
        int pph = (int)(ph * o.dpi / 72.0);

        cairo_pdf_surface_set_size(surface, ppw, pph);

        cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
        cairo_paint(cr);
        cairo_scale(cr, (double)ppw / pw, (double)pph / ph);

        poppler_page_render(page, cr);
        cairo_show_page(cr);

        // Reset transform for next page
        cairo_identity_matrix(cr);
        g_object_unref(page);
    }

    cairo_surface_finish(surface);
    cairo_status_t status = cairo_surface_status(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    unlock_cairo_mutex();
    g_object_unref(doc);

    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Cairo surface finish failed: %s\n", cairo_status_to_string(status));
        return false;
    }
    return true;
}

bool render_page_range_to_pdf_buffer(const char* pdf_path, int start_page, int end_page,
                                     pdf_buffer_t* out_buffer, const render_opts_t* opts) {
    if (pdf_path == NULL || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to render_page_range_to_pdf_buffer\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    if (start_page < 0 || (size_t)end_page >= num_pages || start_page > end_page) {
        fprintf(stderr, "Error: Invalid page range [%d, %d] for document with %zu pages\n",
                start_page, end_page, num_pages);
        g_object_unref(doc);
        return false;
    }

    render_opts_t o = resolve_opts(opts);

    PopplerPage* first_page = poppler_document_get_page(doc, start_page);
    if (first_page == NULL) {
        g_object_unref(doc);
        return false;
    }

    double width, height;
    poppler_page_get_size(first_page, &width, &height);
    g_object_unref(first_page);

    int pixel_width = (int)(width * o.dpi / 72.0);
    int pixel_height = (int)(height * o.dpi / 72.0);

    lock_cairo_mutex();

    cairo_write_context_t write_ctx = {.data = NULL, .size = 0, .capacity = 0};
    cairo_surface_t* surface = cairo_pdf_surface_create_for_stream(
        cairo_write_to_buffer, &write_ctx, pixel_width, pixel_height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        free(write_ctx.data);
        unlock_cairo_mutex();
        g_object_unref(doc);
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        free(write_ctx.data);
        unlock_cairo_mutex();
        g_object_unref(doc);
        return false;
    }

    for (int i = start_page; i <= end_page; i++) {
        PopplerPage* page = poppler_document_get_page(doc, i);
        if (page == NULL) {
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            free(write_ctx.data);
            unlock_cairo_mutex();
            g_object_unref(doc);
            return false;
        }

        double pw, ph;
        poppler_page_get_size(page, &pw, &ph);
        int ppw = (int)(pw * o.dpi / 72.0);
        int pph = (int)(ph * o.dpi / 72.0);

        cairo_pdf_surface_set_size(surface, ppw, pph);
        cairo_set_source_rgb(cr, o.bg_red, o.bg_green, o.bg_blue);
        cairo_paint(cr);
        cairo_scale(cr, (double)ppw / pw, (double)pph / ph);
        poppler_page_render(page, cr);
        cairo_show_page(cr);
        cairo_identity_matrix(cr);
        g_object_unref(page);
    }

    cairo_surface_finish(surface);
    cairo_status_t status = cairo_surface_status(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    unlock_cairo_mutex();
    g_object_unref(doc);

    if (status != CAIRO_STATUS_SUCCESS) {
        free(write_ctx.data);
        return false;
    }

    out_buffer->data = write_ctx.data;
    out_buffer->size = write_ctx.size;
    return true;
}

/* ==========================================================================
 * PDF Merging
 * ========================================================================== */

static bool merge_pdfs_to_surface(const char** pdf_paths, size_t num_files,
                                  cairo_surface_t* surface, cairo_t* cr) {
    for (size_t f = 0; f < num_files; f++) {
        size_t num_pages = 0;
        PopplerDocument* doc = open_document(pdf_paths[f], &num_pages, NULL);
        if (doc == NULL) {
            fprintf(stderr, "Error: Could not open document: %s\n", pdf_paths[f]);
            return false;
        }

        for (size_t i = 0; i < num_pages; i++) {
            PopplerPage* page = poppler_document_get_page(doc, i);
            if (page == NULL) {
                g_object_unref(doc);
                return false;
            }

            double pw, ph;
            poppler_page_get_size(page, &pw, &ph);

            cairo_pdf_surface_set_size(surface, pw, ph);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_paint(cr);
            poppler_page_render(page, cr);
            cairo_show_page(cr);
            cairo_identity_matrix(cr);
            g_object_unref(page);
        }
        g_object_unref(doc);
    }
    return true;
}

bool merge_pdfs(const char** pdf_paths, size_t num_files, const char* output_pdf) {
    if (pdf_paths == NULL || num_files == 0 || output_pdf == NULL) {
        fprintf(stderr, "Error: Invalid parameters to merge_pdfs\n");
        return false;
    }

    lock_cairo_mutex();

    cairo_surface_t* surface = cairo_pdf_surface_create(output_pdf, 612, 792);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        unlock_cairo_mutex();
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        unlock_cairo_mutex();
        return false;
    }

    bool ok = merge_pdfs_to_surface(pdf_paths, num_files, surface, cr);

    cairo_surface_finish(surface);
    cairo_status_t status = cairo_surface_status(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    unlock_cairo_mutex();

    return ok && status == CAIRO_STATUS_SUCCESS;
}

bool merge_pdfs_to_buffer(const char** pdf_paths, size_t num_files, pdf_buffer_t* out_buffer) {
    if (pdf_paths == NULL || num_files == 0 || out_buffer == NULL) {
        fprintf(stderr, "Error: Invalid parameters to merge_pdfs_to_buffer\n");
        return false;
    }

    lock_cairo_mutex();

    cairo_write_context_t write_ctx = {.data = NULL, .size = 0, .capacity = 0};
    cairo_surface_t* surface =
        cairo_pdf_surface_create_for_stream(cairo_write_to_buffer, &write_ctx, 612, 792);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        free(write_ctx.data);
        unlock_cairo_mutex();
        return false;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        free(write_ctx.data);
        unlock_cairo_mutex();
        return false;
    }

    bool ok = merge_pdfs_to_surface(pdf_paths, num_files, surface, cr);

    cairo_surface_finish(surface);
    cairo_status_t status = cairo_surface_status(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    unlock_cairo_mutex();

    if (!ok || status != CAIRO_STATUS_SUCCESS) {
        free(write_ctx.data);
        return false;
    }

    out_buffer->data = write_ctx.data;
    out_buffer->size = write_ctx.size;
    return true;
}

/* ==========================================================================
 * Text Search
 * ========================================================================== */

bool search_page_text(PopplerPage* page, const char* text, pdf_search_results_t* out_results) {
    if (page == NULL || text == NULL || out_results == NULL) {
        fprintf(stderr, "Error: Invalid parameters to search_page_text\n");
        return false;
    }

    out_results->results = NULL;
    out_results->count = 0;

    GList* matches = poppler_page_find_text(page, text);
    if (matches == NULL) {
        return false;
    }

    int count = (int)g_list_length(matches);
    pdf_search_result_t* results = malloc((size_t)count * sizeof(pdf_search_result_t));
    if (results == NULL) {
        g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
        return false;
    }

    size_t idx = 0;
    for (GList* l = matches; l != NULL; l = l->next) {
        PopplerRectangle* rect = (PopplerRectangle*)l->data;
        results[idx].x1 = rect->x1;
        results[idx].y1 = rect->y1;
        results[idx].x2 = rect->x2;
        results[idx].y2 = rect->y2;
        idx++;
    }

    g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);

    out_results->results = results;
    out_results->count = (size_t)count;
    return true;
}

bool search_page_text_from_document(const char* pdf_path, int page_num, const char* text,
                                    pdf_search_results_t* out_results) {
    if (pdf_path == NULL || text == NULL || out_results == NULL) {
        fprintf(stderr, "Error: Invalid parameters to search_page_text_from_document\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    if (page_num < 0 || (size_t)page_num >= num_pages) {
        fprintf(stderr, "Error: Page number %d is out of range (0-%zu)\n", page_num, num_pages - 1);
        g_object_unref(doc);
        return false;
    }

    PopplerPage* page = poppler_document_get_page(doc, page_num);
    if (page == NULL) {
        g_object_unref(doc);
        return false;
    }

    bool result = search_page_text(page, text, out_results);
    g_object_unref(page);
    g_object_unref(doc);
    return result;
}

void free_search_results(pdf_search_results_t* results) {
    if (results == NULL)
        return;
    free(results->results);
    results->results = NULL;
    results->count = 0;
}

/* ==========================================================================
 * Table of Contents / Outline
 * ========================================================================== */

static void build_outline_entries(PopplerIndexIter* iter, PopplerDocument* doc,
                                  pdf_outline_entry_t** out_entries, size_t* out_count) {
    *out_entries = NULL;
    *out_count = 0;

    if (iter == NULL)
        return;

    // First pass: count entries at this level
    size_t count = 0;
    PopplerIndexIter* tmp = poppler_index_iter_copy(iter);
    do {
        count++;
    } while (poppler_index_iter_next(tmp));
    poppler_index_iter_free(tmp);

    pdf_outline_entry_t* entries = calloc(count, sizeof(pdf_outline_entry_t));
    if (entries == NULL)
        return;

    size_t idx = 0;
    do {
        PopplerAction* action = poppler_index_iter_get_action(iter);
        if (action != NULL) {
            if (action->type == POPPLER_ACTION_GOTO_DEST && action->goto_dest.title) {
                entries[idx].title = g_strdup(action->goto_dest.title);
                if (action->goto_dest.dest && action->goto_dest.dest->type == POPPLER_DEST_NAMED) {
                    PopplerDest* resolved =
                        poppler_document_find_dest(doc, action->goto_dest.dest->named_dest);
                    if (resolved) {
                        entries[idx].page_num = resolved->page_num - 1;  // Convert to 0-based
                        poppler_dest_free(resolved);
                    } else {
                        entries[idx].page_num = -1;
                    }
                } else if (action->goto_dest.dest) {
                    entries[idx].page_num = action->goto_dest.dest->page_num - 1;
                } else {
                    entries[idx].page_num = -1;
                }
            } else if (action->any.title) {
                entries[idx].title = g_strdup(action->any.title);
                entries[idx].page_num = -1;
            }
            poppler_action_free(action);
        }

        // Recurse into children
        PopplerIndexIter* child = poppler_index_iter_get_child(iter);
        if (child != NULL) {
            build_outline_entries(child, doc, &entries[idx].children, &entries[idx].num_children);
            poppler_index_iter_free(child);
        }

        idx++;
    } while (poppler_index_iter_next(iter));

    *out_entries = entries;
    *out_count = count;
}

bool get_pdf_outline_from_document(PopplerDocument* doc, pdf_outline_t* out_outline) {
    if (doc == NULL || out_outline == NULL) {
        fprintf(stderr, "Error: Invalid parameters to get_pdf_outline_from_document\n");
        return false;
    }

    memset(out_outline, 0, sizeof(pdf_outline_t));

    PopplerIndexIter* iter = poppler_index_iter_new(doc);
    if (iter == NULL) {
        // No outline in the document — that's fine, return empty
        return true;
    }

    build_outline_entries(iter, doc, &out_outline->entries, &out_outline->num_entries);
    poppler_index_iter_free(iter);
    return true;
}

bool get_pdf_outline(const char* pdf_path, pdf_outline_t* out_outline) {
    if (pdf_path == NULL || out_outline == NULL) {
        fprintf(stderr, "Error: Invalid parameters to get_pdf_outline\n");
        return false;
    }

    size_t num_pages = 0;
    PopplerDocument* doc = open_document(pdf_path, &num_pages, NULL);
    if (doc == NULL) {
        return false;
    }

    bool result = get_pdf_outline_from_document(doc, out_outline);
    g_object_unref(doc);
    return result;
}

static void free_outline_entries(pdf_outline_entry_t* entries, size_t count) {
    if (entries == NULL)
        return;
    for (size_t i = 0; i < count; i++) {
        g_free(entries[i].title);
        free_outline_entries(entries[i].children, entries[i].num_children);
    }
    free(entries);
}

void free_pdf_outline(pdf_outline_t* outline) {
    if (outline == NULL)
        return;
    free_outline_entries(outline->entries, outline->num_entries);
    outline->entries = NULL;
    outline->num_entries = 0;
}
