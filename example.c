#include <assert.h>
#include <stdio.h>
#include "include/pdfpoppler.h"

int main(void) {
    pdfpoppler_init();

    // Open a PDF document
    int n;
    // Replace with your pdf file path
    PopplerDocument* doc = open_document("../../../Downloads/PDFs/BNF 2020.pdf", &n, NULL);
    assert(doc != NULL);

    // Print the number of pages
    printf("Number of pages: %d\n", n);

    // Get the first page
    PopplerPage* page = poppler_document_get_page(doc, 0);
    assert(page != NULL);

    // Render the first page to a PNG image
    render_page_to_image(page, 800, 600, "output.png");

    // Render the first page to a PDF file
    poppler_page_to_pdf(page, "output.pdf");

    // free page
    g_object_unref(page);

    // Close the document
    close_document(doc);

    pdfpoppler_cleanup();
    return 0;
}