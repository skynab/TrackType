#pragma once

#include <QString>
#include <QList>

// One selectable whisper model in the download catalog.
struct ModelInfo {
    QString name;          // file name / settings key, e.g. "ggml-base.en.bin"
    QString label;         // human-readable label for the settings combo
    QString url;           // download URL
    QString sha256;        // lowercase hex; empty = checksum not verified
    qint64  size = 0;      // expected size in bytes (0 = unknown), for display
    bool    multilingual = false;  // false → English-only (.en); language fixed to "en"
};

// Resolves whisper model files and the download catalog.
//
// Path logic mirrors the translations loader (tsparser.cpp / loadBestTranslator):
// the per-user writable QStandardPaths::AppLocalDataLocation, here under a
// "models/" subdirectory so a user can drop a ggml model in by hand.
namespace ModelManager {

// Default model used on first run ("ggml-base.en.bin").
QString defaultModelName();

// Selectable models for the settings UI (default first).
QList<ModelInfo> catalog();

// Catalog entry for `name`, or the default entry if `name` is unknown/empty.
ModelInfo modelInfo(const QString& name);

// <AppLocalDataLocation>/models
QString modelsDir();

// Absolute path to a model file in modelsDir (defaultModelName() when empty).
QString modelFilePath(const QString& name = QString());

// True when the model file exists and is non-empty.
bool isModelAvailable(const QString& name = QString());

} // namespace ModelManager
