#include "modelmanager.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace {
// ggml-base.en.bin — small English-only model (~141 MB).  URL, checksum and size
// are the authoritative values from the canonical HuggingFace repo.
const char*  kDefaultModel = "ggml-base.en.bin";
const char*  kUrl =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin";
const char*  kSha256 =
    "a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002";
const qint64 kSize = 147964211;
} // namespace

QString ModelManager::defaultModelName()
{
    return QString::fromLatin1(kDefaultModel);
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

QString ModelManager::defaultModelUrl()    { return QString::fromLatin1(kUrl); }
QString ModelManager::defaultModelSha256() { return QString::fromLatin1(kSha256); }
qint64  ModelManager::defaultModelSize()   { return kSize; }
