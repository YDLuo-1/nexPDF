#!/usr/bin/env python3
"""Apply the small regression-confirmed fixes to MuPDF's pinned source."""

import sys
import re
from pathlib import Path


def patch_cms() -> None:
    source = Path(sys.argv[1]) / "thirdparty/lcms2/src/cmsopt.c"
    text = source.read_text(encoding="utf-8")
    anchor = "static\ncmsBool OptimizeByResampling("
    old = "(_cmsPipelineEval16Fn) DataCLUT->Params->Interpolation.Lerp16"
    new = "NexPdfEvalClut16"
    adapter = """// nexPDF: preserve the pipeline callback type instead of casting a function pointer.
static void NexPdfEvalClut16(cmsContext ContextID, const cmsUInt16Number In[],
                           cmsUInt16Number Out[], const void* Data)
{
    const cmsInterpParams* params = (const cmsInterpParams*) Data;
    params->Interpolation.Lerp16(ContextID, In, Out, params);
}

"""
    if old not in text and text.count(adapter) == 1 and text.count(new) == 2:
        print("Little CMS callback patch already applied")
        return
    if text.count(anchor) != 1 or text.count(old) != 1 or new in text:
        raise RuntimeError("Little CMS source changed; review the callback patch before building")
    text = text.replace(anchor, adapter + anchor).replace(old, new)
    source.write_text(text, encoding="utf-8", newline="\n")
    print("Applied Little CMS typed pipeline callback patch")


def patch_empty_bitmap() -> None:
    source = Path(sys.argv[1]) / "source/fitz/font.c"
    text = source.read_text(encoding="utf-8")
    old = "bitmap->buffer + (bitmap->rows-1)*bitmap->pitch"
    new = "(bitmap->buffer && bitmap->rows ? " + old + " : bitmap->buffer)"
    if text.count(new) == 4 and text.count(old) == 4:
        print("Empty font bitmap patch already applied")
        return
    if text.count(old) != 4 or new in text:
        raise RuntimeError("MuPDF font source changed; review the empty bitmap patch before building")
    source.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
    print("Applied empty font bitmap pointer guard")


def patch_abandoned_redo() -> None:
    source = Path(sys.argv[1]) / "source/pdf/pdf-object.c"
    text = source.read_text(encoding="utf-8")
    old = """\t\t/* We are about to add a fragment. Everything after 'current' in the
\t\t * history must be thrown away. If current is NULL, then *everything*
\t\t * must be thrown away. */
\t\tdiscard_journal_entries(ctx, doc->journal->current ? &doc->journal->current->next : &doc->journal->head);

"""
    anchor = "\telse if (entry->title != NULL)\n"
    commit = """\t/* nexPDF: retain redo until the outer operation commits successfully.
\t * pdf_abandon_operation can then roll back without losing prior history. */
\tif (entry && entry->head)
\t\tdiscard_journal_entries(ctx, doc->journal->current ? &doc->journal->current->next : &doc->journal->head);

"""
    insertion = "\t/* We really ought to have just a single pending entry at this point,\n"
    if old not in text and text.count(commit) == 1:
        print("Abandoned-operation redo patch already applied")
        return
    if text.count(old) != 1 or text.count(insertion) != 1 or text.count(anchor) != 1 or commit in text:
        raise RuntimeError("MuPDF journal source changed; review the abandoned-operation patch before building")
    source.write_text(text.replace(old, "").replace(insertion, commit + insertion),
                      encoding="utf-8", newline="\n")
    print("Applied commit-time redo invalidation patch")


def patch_aes_shifts() -> None:
    source = Path(sys.argv[1]) / "source/fitz/crypt-aes.c"
    text = source.read_text(encoding="utf-8")
    pattern = r"\( ([FR]Sb\[[^\n]+?\]) << 24 \)"
    fixed = r"\( \(uint32_t\) [FR]Sb\[[^\n]+?\] << 24 \)"
    if not re.search(pattern, text) and len(re.findall(fixed, text)) == 12:
        print("AES unsigned shift patch already applied")
        return
    patched, count = re.subn(pattern, r"( (uint32_t) \1 << 24 )", text)
    if count != 12 or re.search(fixed, text):
        raise RuntimeError("MuPDF AES source changed; review the unsigned shift patch before building")
    source.write_text(patched, encoding="utf-8", newline="\n")
    print("Applied AES unsigned 24-bit shift patch")


if __name__ == "__main__":
    patch_cms()
    patch_empty_bitmap()
    patch_abandoned_redo()
    patch_aes_shifts()
