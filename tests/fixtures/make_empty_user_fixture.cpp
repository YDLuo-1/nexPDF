#include <mupdf/pdf.h>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document *doc = nullptr;
    int status = 0;
    fz_var(doc); fz_var(status);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, argv[1]);
        pdf_write_options options = pdf_default_write_options;
        options.do_encrypt = PDF_ENCRYPT_AES_256;
        options.permissions = PDF_PERM_PRINT;
        std::strcpy(options.opwd_utf8, "owner");
        pdf_save_document(ctx, doc, argv[2], &options);
    }
    fz_always(ctx) { pdf_drop_document(ctx, doc); }
    fz_catch(ctx) { std::fprintf(stderr, "%s\n", fz_caught_message(ctx)); status = 1; }
    fz_drop_context(ctx);
    return status;
}
