#pragma once

#include <QIcon>

namespace nexpdf::icons {

enum class Kind {
    Open,
    SaveAs,
    Undo,
    Redo,
    PreviousPage,
    NextPage,
    ZoomOut,
    ZoomIn,
    ActualSize,
    Encrypt,
    Decrypt,
    InsertPage,
    ImportPages,
    DeletePage,
    PageUp,
    PageDown,
    RotateLeft,
    RotateRight,
    AddText,
    AddImage,
    Highlight,
    Underline,
    StrikeOut,
    Rectangle,
    Ellipse,
    Ink,
    Move,
    Resize,
    DeleteObject,
    RedactionPreview,
    ApplyRedactions,
    Search,
    TextWatermark,
    ImageWatermark,
    ScanWatermark,
    RemoveWatermark
};

QIcon actionIcon(Kind kind);
QIcon applicationIcon();

} // namespace nexpdf::icons
