#include "modelmanager.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace {
const char* kDefaultModel = "ggml-base.en.bin";
const char* kBaseUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

// Download catalog.  base.en carries its authoritative checksum/size; the others
// have empty sha256 (checksum skipped) until their values are filled in — see the
// note below.  Order matters: the default model is listed first.
//
// TODO: populate the tiny.en and base (multilingual) sha256/size from the
// canonical HuggingFace LFS pointers so those downloads are checksum-verified too.
QList<ModelInfo> makeCatalog()
{
    return {
        { "ggml-base.en.bin", "Base — English (default)",
          QString(kBaseUrl) + "ggml-base.en.bin",
          "a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002",
          147964211, false },
        { "ggml-tiny.en.bin", "Tiny — English (fastest)",
          QString(kBaseUrl) + "ggml-tiny.en.bin",
          QString(), 0, false },
        { "ggml-base.bin",    "Base — multilingual",
          QString(kBaseUrl) + "ggml-base.bin",
          QString(), 0, true },
    };
}
} // namespace

QString ModelManager::defaultModelName()
{
    return QString::fromLatin1(kDefaultModel);
}

QList<ModelInfo> ModelManager::catalog()
{
    return makeCatalog();
}

ModelInfo ModelManager::modelInfo(const QString& name)
{
    const QList<ModelInfo> all = makeCatalog();
    for (const ModelInfo& m : all) {
        if (m.name == name)
            return m;
    }
    return all.first();   // default
}

QString ModelManager::modelsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
         + "/models";
}

QString ModelManager::modelFilePath(const QString& name)
{
    const QString n = name.isEmpty() ? defaultModelName() : name;
    return modelsDir() + "/" + n;
}

bool ModelManager::isModelAvailable(const QString& name)
{
    const QFileInfo fi(modelFilePath(name));
    return fi.exists() && fi.size() > 0;
}
