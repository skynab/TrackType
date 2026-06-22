#pragma once

#include <QString>

// Resolves the local whisper model file and its download metadata.
//
// Path logic mirrors the translations loader (tsparser.cpp / loadBestTranslator):
// the per-user writable QStandardPaths::AppLocalDataLocation, here under a
// "models/" subdirectory so a user can drop a different ggml model in by hand.
namespace ModelManager {

// Default model shipped/downloaded on first run ("ggml-base.en.bin").
QString defaultModelName();

// <AppLocalDataLocation>/models
QString modelsDir();

// Absolute path to a model file in modelsDir (defaultModelName() when empty).
QString modelFilePath(const QString& name = QString());

// True when the model file exists and is non-empty.
bool isModelAvailable(const QString& name = QString());

// Download metadata for the default model (HuggingFace, ggerganov/whisper.cpp).
QString defaultModelUrl();
QString defaultModelSha256();   // lowercase hex
qint64  defaultModelSize();     // expected size in bytes

} // namespace ModelManager
