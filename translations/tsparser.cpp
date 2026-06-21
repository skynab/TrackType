#include "tsparser.h"
#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTranslator>
#include <QXmlStreamReader>

TsTranslator::TsTranslator(QObject* parent) : QTranslator(parent) {}

bool TsTranslator::loadTs(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QXmlStreamReader xml(&f);
    QString ctx, source;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNextStartElement();
        if (!xml.isStartElement()) continue;

        const auto tag = xml.name();
        if (tag == u"name") {
            ctx = xml.readElementText();
        } else if (tag == u"source") {
            source = xml.readElementText();
        } else if (tag == u"translation") {
            // Skip placeholder entries ("unfinished" or "obsolete")
            const auto type = xml.attributes().value("type");
            if (type != u"unfinished" && type != u"obsolete") {
                const QString t = xml.readElementText();
                if (!t.isEmpty() && !ctx.isEmpty() && !source.isEmpty())
                    m_data[ctx][source] = t;
            }
        }
    }
    return !xml.hasError();
}

QString TsTranslator::translate(const char* context, const char* sourceText,
                                 const char* /*disambiguation*/, int /*n*/) const
{
    const auto ci = m_data.constFind(QString::fromLatin1(context));
    if (ci == m_data.constEnd()) return {};
    const auto si = ci->constFind(QString::fromUtf8(sourceText));
    if (si == ci->constEnd()) return {};
    return *si;
}

bool TsTranslator::isEmpty() const
{
    return m_data.isEmpty();
}

// ─── loadBestTranslator ───────────────────────────────────────────────────────
QTranslator* loadBestTranslator(const QString& lang, QObject* parent)
{
    if (lang == "en") return nullptr;

    const QString stem = "tracktype_" + lang;

    // Ordered list of directories to search. The user-data directory comes first
    // so that dropping a new .ts file there overrides the bundled copy immediately,
    // without rebuilding the application.
    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + "/translations",
        QCoreApplication::applicationDirPath() + "/translations",
        QCoreApplication::applicationDirPath() + "/../translations",  // macOS bundle
    };

    // For each external directory: prefer a compiled .qm (faster when lrelease is
    // available), otherwise accept a plain .ts XML file (always editable).
    for (const QString& dir : dirs) {
        auto* qt = new QTranslator(parent);
        if (qt->load(dir + "/" + stem + ".qm")) return qt;
        delete qt;

        auto* ts = new TsTranslator(parent);
        if (ts->loadTs(dir + "/" + stem + ".ts")) return ts;
        delete ts;
    }

    // Embedded resource: guaranteed fallback, never missing.
    auto* ts = new TsTranslator(parent);
    if (ts->loadTs(":/translations/" + stem + ".ts")) return ts;
    delete ts;

    return nullptr;
}
