#pragma once
#include <QHash>
#include <QTranslator>

// Loads a Qt Linguist .ts XML file at runtime — no lrelease or .qm needed.
// Falls back gracefully: returns an empty string for any untranslated entry,
// letting Qt fall through to the source string as normal.
class TsTranslator : public QTranslator
{
    Q_OBJECT
public:
    explicit TsTranslator(QObject* parent = nullptr);

    // Load from a file path or a Qt resource path (e.g. ":/translations/tracktype_fr.ts").
    bool loadTs(const QString& path);

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation, int n) const override;
    bool isEmpty() const override;

private:
    // context  →  source string  →  translation
    QHash<QString, QHash<QString, QString>> m_data;
};

// Load the best available translator for lang (e.g. "fr", "zh_CN").
// Search order per directory: .qm (compiled) → .ts (live XML)
//   1. QStandardPaths::AppLocalDataLocation/translations  ← user can drop updated files here
//   2. <binary>/translations
//   3. <binary>/../translations                           ← macOS app bundle
//   4. :/translations                                     ← embedded fallback
// Returns nullptr only when lang == "en" or no file is found at all.
QTranslator* loadBestTranslator(const QString& lang, QObject* parent = nullptr);
