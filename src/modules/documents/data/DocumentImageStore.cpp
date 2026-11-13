#include "modules/documents/data/DocumentImageStore.h"

#include "modules/documents/data/DocumentSql.h"

#include <QBuffer>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// Good enough that a photograph in a manual looks like a photograph, small
// enough that the database stays something a person can email.
constexpr int kJpegQuality = 85;

} // namespace

DocumentImageStore::DocumentImageStore(QSqlDatabase& database, const QString& nodeId)
    : m_database(database)
    , m_nodeId(nodeId)
{
}

bool DocumentImageStore::store(const QImage& image, QString* newId)
{
    m_errorString.clear();

    if (image.isNull()) {
        m_errorString = QStringLiteral("That file is not a picture this program can read.");
        return false;
    }

    QImage scaled = image;
    if (scaled.width() > kMaxDimension || scaled.height() > kMaxDimension) {
        // Qt's smooth transformation, and bounded by both sides, so the
        // aspect ratio is kept and nothing is cropped.
        scaled = scaled.scaled(kMaxDimension, kMaxDimension, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }

    // Transparency is the deciding question: JPEG has none, so a screenshot
    // or a diagram with a transparent background would gain a black one.
    const bool    hasAlpha  = scaled.hasAlphaChannel();
    const QString mimeType  = hasAlpha ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    const char*   formatTag = hasAlpha ? "PNG" : "JPEG";

    QByteArray bytes;
    QBuffer    buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)
        || !scaled.save(&buffer, formatTag, hasAlpha ? -1 : kJpegQuality)) {
        m_errorString = QStringLiteral("The picture could not be prepared for storing.");
        return false;
    }
    buffer.close();

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = DocumentSql::nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO document_image"
        " (id, data, mime_type,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(bytes);
    query.addBindValue(mimeType);
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(m_nodeId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not store the picture:\n%1").arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool DocumentImageStore::storeEncoded(const QByteArray& encoded, QString* newId)
{
    // Decoded rather than stored as it arrived, so that an enormous picture
    // is scaled like any other and something that is not a picture at all is
    // refused here instead of being written and failing to draw later.
    QImage image;
    if (!image.loadFromData(encoded)) {
        m_errorString = QStringLiteral("That is not a picture this program can read.");
        return false;
    }
    return store(image, newId);
}

QByteArray DocumentImageStore::load(const QString& imageId)
{
    m_errorString.clear();

    if (imageId.isEmpty()) {
        return QByteArray();
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT data FROM document_image WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(imageId);

    if (!query.exec() || !query.next()) {
        // Not an error the user is told about: a page referring to a picture
        // that is not there draws as a missing image, which is far better
        // than refusing to show the page.
        return QByteArray();
    }
    return query.value(0).toByteArray();
}

QString DocumentImageStore::errorString() const
{
    return m_errorString;
}
