#!/usr/bin/env python3
"""Apply the narrow Little CMS callback fix to MuPDF's pinned source."""

import sys
from pathlib import Path


def main() -> None:
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


if __name__ == "__main__":
    main()
