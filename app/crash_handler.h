#pragma once

// Installs a best-effort crash reporter before QApplication exists.
// Dumps/reports stay on the local disk only and are never uploaded.
namespace nexpdf::crash {
void install();
}
