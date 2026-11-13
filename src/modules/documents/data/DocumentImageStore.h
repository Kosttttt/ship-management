#pragma once

#include <QByteArray>
#include <QString>

class QImage;
class QSqlDatabase;

// `document_image` — pictures, in the database (§4.2).
//
// In the database and not in a folder for two reasons the developer and this
// project already care about: a page and its pictures are then in every
// backup, and they travel together in a package when step 6 sends a manual
// to a ship. A picture in a folder beside the database is a picture that
// arrives separately or not at all.
//
// **Immutable.** Editing a picture writes a new row, so an older revision
// still shows the picture it was published with — which is the whole point of
// keeping revisions at all.
class DocumentImageStore
{
public:
    DocumentImageStore(QSqlDatabase& database, const QString& nodeId);

    // The longest side a stored picture may have. A photograph straight from
    // a phone is several thousand pixels wide and a few megabytes; at this
    // size it is still sharper than anything this program prints or shows,
    // and a manual with fifty of them stays a database somebody can email.
    //
    // 1600 was chosen over 1024 (visibly soft on a large screen) and over
    // 2400 (roughly twice the bytes for a difference nobody sees on a page
    // of A4). Stated here rather than buried in the .cpp because it is a
    // judgment, not a constant.
    static constexpr int kMaxDimension = 1600;

    // Stores a picture, scaling it down first if it is larger than
    // kMaxDimension on either side, and reports its id.
    //
    // PNG when the picture has transparency and JPEG otherwise: a screenshot
    // or a diagram keeps its crisp edges, and a photograph does not get
    // stored at five times the size it needs.
    bool store(const QImage& image, QString* newId);

    // Stores bytes that are already an encoded picture — what a paste or a
    // `data:` URI hands over. Decoded first, so an enormous one is scaled
    // like any other and something that is not a picture at all is refused
    // rather than written.
    bool storeEncoded(const QByteArray& encoded, QString* newId);

    // The bytes back, for showing in the editor. Empty when there is no such
    // picture, which the editor draws as a missing image rather than failing.
    QByteArray load(const QString& imageId);

    QString errorString() const;

private:
    QSqlDatabase& m_database;
    QString       m_nodeId;
    QString       m_errorString;
};
