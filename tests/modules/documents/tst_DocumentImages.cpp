#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentImageStore.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/service/DocumentPublisher.h"
#include "modules/documents/ui/DocumentRichEdit.h"

#include <QBuffer>
#include <QImage>
#include <QMimeData>
#include <QtTest>

using namespace DocumentTestSupport;

namespace {

QImage solidImage(int width, int height, QImage::Format format = QImage::Format_RGB32)
{
    QImage image(width, height, format);
    image.fill(format == QImage::Format_ARGB32 ? Qt::transparent : Qt::darkBlue);
    return image;
}

QByteArray encodedPng(const QImage& image)
{
    QByteArray bytes;
    QBuffer    buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString mimeTypeOf(QSqlDatabase& db, const QString& imageId)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT mime_type FROM document_image WHERE id = ?"));
    query.addBindValue(imageId);
    if (!query.exec() || !query.next()) {
        return QString();
    }
    return query.value(0).toString();
}

// Lets the test hand the editor a paste without a clipboard, which a test
// must not touch — the developer's own clipboard is not ours to overwrite.
class PastableEdit : public DocumentRichEdit
{
public:
    using DocumentRichEdit::DocumentRichEdit;

    void paste(const QMimeData* data) { insertFromMimeData(data); }
};

} // namespace

// document-management-spec.md §4.2, §6.1 and §7: pictures, which live in the
// database rather than in a folder, and are immutable like the revisions that
// refer to them.
class TestDocumentImages : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aPictureIsStoredInTheDatabase();
    void anOversizePictureIsScaledDown();
    void aPictureThatIsSmallEnoughIsNotEnlarged();
    void transparencyDecidesPngOrJpeg();
    void aMissingPictureLoadsAsNothing();
    void somethingThatIsNotAPictureIsRefused();

    void anEditedPictureIsANewRowAndTheOlderRevisionKeepsTheOld();

    void theEditorRefersToAStoredPictureAndCanDrawIt();
    void aPastedDataUriPictureIsStored();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

void TestDocumentImages::init()
{
    m_connectionName = QStringLiteral("docimg_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentImages::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentImages::aPictureIsStoredInTheDatabase()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    QString id;
    QVERIFY2(images.store(solidImage(40, 30), &id), qPrintable(images.errorString()));
    QVERIFY(!id.isEmpty());
    QCOMPARE(rowCountOf(db, QStringLiteral("document_image")), 1);

    // In the database and nowhere else, which is what makes a page and its
    // pictures travel together in a backup and in a package (§4.2).
    const QByteArray bytes = images.load(id);
    QVERIFY(!bytes.isEmpty());

    QImage reloaded;
    QVERIFY(reloaded.loadFromData(bytes));
    QCOMPARE(reloaded.size(), QSize(40, 30));
}

void TestDocumentImages::anOversizePictureIsScaledDown()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    // A photograph out of a phone, roughly.
    QString id;
    QVERIFY(images.store(solidImage(3000, 2000), &id));

    QImage reloaded;
    QVERIFY(reloaded.loadFromData(images.load(id)));

    // The chosen limit, 1600 px on the longest side: still sharper than
    // anything this program prints, and a manual with fifty photographs is
    // still a database somebody can email.
    QCOMPARE(reloaded.width(), DocumentImageStore::kMaxDimension);
    QVERIFY(reloaded.height() <= DocumentImageStore::kMaxDimension);

    // Not cropped and not squashed: 3:2 in, 3:2 out.
    const double ratio = double(reloaded.width()) / double(reloaded.height());
    QVERIFY2(qAbs(ratio - 1.5) < 0.01, qPrintable(QString::number(ratio)));
}

void TestDocumentImages::aPictureThatIsSmallEnoughIsNotEnlarged()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    QString id;
    QVERIFY(images.store(solidImage(200, 120), &id));

    QImage reloaded;
    QVERIFY(reloaded.loadFromData(images.load(id)));
    QCOMPARE(reloaded.size(), QSize(200, 120));
}

void TestDocumentImages::transparencyDecidesPngOrJpeg()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    QString diagram;
    QVERIFY(images.store(solidImage(50, 50, QImage::Format_ARGB32), &diagram));
    QCOMPARE(mimeTypeOf(db, diagram), QStringLiteral("image/png"));

    QString photograph;
    QVERIFY(images.store(solidImage(50, 50, QImage::Format_RGB32), &photograph));
    QCOMPARE(mimeTypeOf(db, photograph), QStringLiteral("image/jpeg"));
}

void TestDocumentImages::aMissingPictureLoadsAsNothing()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    // A page that refers to a picture which is not there must still open. It
    // draws as a missing image, which a person can see and fix; refusing to
    // show the page is not something a person can do anything about.
    QVERIFY(images.load(QStringLiteral("no-such-picture")).isEmpty());
    QVERIFY(images.load(QString()).isEmpty());
}

void TestDocumentImages::somethingThatIsNotAPictureIsRefused()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    // Refused here rather than written and found to be undrawable later.
    QVERIFY(!images.storeEncoded(QByteArray("this is a spreadsheet, not a picture"), nullptr));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_image")), 0);
}

void TestDocumentImages::anEditedPictureIsANewRowAndTheOlderRevisionKeepsTheOld()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentImageStore      images(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                      QString(), &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Mooring plan"), &pageId));

    QString firstImage;
    QVERIFY(images.store(solidImage(60, 40), &firstImage));
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Mooring plan"),
                            QStringLiteral("<p><img src=\"%1:%2\"></p>")
                                .arg(QString::fromLatin1(DocumentRichEdit::imageScheme()),
                                     firstImage)));
    QVERIFY(publisher.publishPage(pageId, QString()));

    // The plan is redrawn and the page edited again.
    QString secondImage;
    QVERIFY(images.store(solidImage(60, 40), &secondImage));
    QVERIFY(firstImage != secondImage);
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Mooring plan"),
                            QStringLiteral("<p><img src=\"%1:%2\"></p>")
                                .arg(QString::fromLatin1(DocumentRichEdit::imageScheme()),
                                     secondImage)));
    QVERIFY(publisher.publishPage(pageId, QStringLiteral("New mooring plan")));

    // **Two rows, not one overwritten.** This is the whole reason pictures
    // are immutable: Rev 1 of a page has to still show the plan that was in
    // force when Rev 1 was issued.
    QCOMPARE(rowCountOf(db, QStringLiteral("document_image")), 2);

    QList<DocumentPageRevision> all;
    QVERIFY(revisions.listPageRevisions(pageId, &all));
    QCOMPARE(all.size(), 2);
    QVERIFY(all.first().content.contains(secondImage));  // Rev 2
    QVERIFY(all.last().content.contains(firstImage));    // Rev 1
    QVERIFY(!all.last().content.contains(secondImage));

    // And the older picture is still loadable, so Rev 1 still draws.
    QVERIFY(!images.load(firstImage).isEmpty());
}

void TestDocumentImages::theEditorRefersToAStoredPictureAndCanDrawIt()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    DocumentRichEdit editor(images);
    QString          error;
    QVERIFY2(editor.insertStoredImage(solidImage(80, 60), &error), qPrintable(error));

    QCOMPARE(rowCountOf(db, QStringLiteral("document_image")), 1);

    const QString html = editor.toHtml();
    QVERIFY2(html.contains(QString::fromLatin1(DocumentRichEdit::imageScheme()) + QLatin1Char(':')),
             qPrintable(html.left(400)));

    // The document can actually resolve it, which is what decides whether a
    // picture appears or shows as a broken image. Asserting only that the
    // HTML mentions the id would prove nothing about what is drawn.
    const QRegularExpression      pattern(QStringLiteral("src=\"(doc-image:[^\"]+)\""));
    const QRegularExpressionMatch match = pattern.match(html);
    QVERIFY2(match.hasMatch(), qPrintable(html.left(400)));

    const QVariant resource =
        editor.document()->resource(QTextDocument::ImageResource, QUrl(match.captured(1)));
    QVERIFY2(!resource.isNull(), "the editor must resolve doc-image: to the stored picture");
    QVERIFY(!resource.toByteArray().isEmpty() || !resource.value<QImage>().isNull());
}

void TestDocumentImages::aPastedDataUriPictureIsStored()
{
    QSqlDatabase       db = database();
    DocumentImageStore images(db, QStringLiteral("OFFICE"));

    PastableEdit editor(images);

    // What a paste from a web page or a recent Word carries: the picture
    // inline in the HTML. Left alone it would be a dead reference the moment
    // the HTML was stored, so it is pulled into the image store on the way in.
    const QString html = QStringLiteral("<p>Before <img src=\"data:image/png;base64,%1\"> after</p>")
                             .arg(QString::fromLatin1(encodedPng(solidImage(30, 20)).toBase64()));

    QMimeData data;
    data.setHtml(html);
    editor.paste(&data);

    QCOMPARE(rowCountOf(db, QStringLiteral("document_image")), 1);

    const QString stored = editor.toHtml();
    QVERIFY2(stored.contains(QString::fromLatin1(DocumentRichEdit::imageScheme())),
             qPrintable(stored.left(400)));
    QVERIFY2(!stored.contains(QStringLiteral("base64")), "the data: URI must not survive");
}

QTEST_MAIN(TestDocumentImages)
#include "tst_DocumentImages.moc"
