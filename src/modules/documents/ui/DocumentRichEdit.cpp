#include "modules/documents/ui/DocumentRichEdit.h"

#include "modules/documents/data/DocumentImageStore.h"
#include "modules/documents/ui/WordPasteCleaner.h"

#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>

namespace {

const char* const kImageScheme = "doc-image";

// A QTextDocument that resolves this module's picture references.
//
// Qt asks the document for any resource its content names, which is the one
// place a stored picture can be turned into bytes without the editor having
// to find and replace them itself.
class StoredImageDocument : public QTextDocument
{
public:
    StoredImageDocument(DocumentImageStore& images, QObject* parent)
        : QTextDocument(parent)
        , m_images(images)
    {
    }

protected:
    QVariant loadResource(int type, const QUrl& name) override
    {
        if (type == QTextDocument::ImageResource
            && name.scheme() == QLatin1String(kImageScheme)) {
            const QByteArray bytes = m_images.load(name.path());
            if (bytes.isEmpty()) {
                // A page referring to a picture that is not there draws as a
                // missing image. Far better than refusing to show the page:
                // the text is what the manual is for.
                return QVariant();
            }
            QImage image;
            image.loadFromData(bytes);
            return image;
        }
        return QTextDocument::loadResource(type, name);
    }

private:
    DocumentImageStore& m_images;
};

} // namespace

const char* DocumentRichEdit::imageScheme()
{
    return kImageScheme;
}

DocumentRichEdit::DocumentRichEdit(DocumentImageStore& images, QWidget* parent)
    : QTextEdit(parent)
    , m_images(images)
{
    setAcceptRichText(true);
    // Its own document, so picture references resolve. Set before any
    // content is loaded, or the first page would draw its pictures as
    // missing and only later ones would work.
    setDocument(new StoredImageDocument(images, this));
}

bool DocumentRichEdit::insertStoredImage(const QImage& image, QString* error)
{
    QString imageId;
    if (!m_images.store(image, &imageId)) {
        *error = m_images.errorString();
        return false;
    }

    QTextImageFormat format;
    format.setName(QStringLiteral("%1:%2").arg(QLatin1String(kImageScheme), imageId));
    textCursor().insertImage(format);
    return true;
}

QString DocumentRichEdit::importDataUriImages(const QString& html)
{
    // `<img src="data:image/png;base64,…">`, which is how a web page and a
    // recent Word both put a picture on the clipboard.
    static const QRegularExpression pattern(
        QStringLiteral("src=\"data:(image/[A-Za-z0-9.+-]+);base64,([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);

    QString result    = html;
    int     searchFrom = 0;

    while (true) {
        const QRegularExpressionMatch match = pattern.match(result, searchFrom);
        if (!match.hasMatch()) {
            break;
        }

        const QByteArray encoded =
            QByteArray::fromBase64(match.captured(2).toLatin1());

        QString imageId;
        if (!m_images.storeEncoded(encoded, &imageId)) {
            // Not a picture this program can read. The reference is left
            // alone rather than rewritten: it will draw as a missing image,
            // which is honest, and nothing else about the paste is lost.
            searchFrom = match.capturedEnd();
            continue;
        }

        const QString replacement =
            QStringLiteral("src=\"%1:%2\"").arg(QLatin1String(kImageScheme), imageId);
        result.replace(match.capturedStart(), match.capturedLength(), replacement);
        searchFrom = match.capturedStart() + replacement.size();
    }
    return result;
}

void DocumentRichEdit::insertFromMimeData(const QMimeData* source)
{
    if (source == nullptr) {
        return;
    }

    if (source->hasHtml()) {
        // §6.1: the structure survives and Word's fonts, sizes and colours
        // do not. Pictures are pulled into the database first, so the
        // cleaner sees references it can keep rather than megabytes of
        // base64 it would carry around.
        const QString imported = importDataUriImages(source->html());
        insertHtml(WordPasteCleaner::clean(imported));
        return;
    }

    if (source->hasImage()) {
        // A screenshot or a picture copied from another program. Stored like
        // an inserted one, so there is only ever one kind of picture here.
        const QImage image = qvariant_cast<QImage>(source->imageData());
        QString      error;
        if (!image.isNull() && insertStoredImage(image, &error)) {
            return;
        }
    }

    // Plain text, or something nothing above could use: QTextEdit's own
    // handling, which inserts the text and nothing else.
    QTextEdit::insertFromMimeData(source);
}
