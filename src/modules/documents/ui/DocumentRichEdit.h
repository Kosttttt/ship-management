#pragma once

#include <QTextEdit>

class DocumentImageStore;

// The page editor: a QTextEdit that knows about this module's pictures and
// about paste from Word (document-management-spec.md §6.1).
//
// Two jobs beyond what QTextEdit already does.
//
// **Pictures live in the database** (§4.2), so content refers to one by
// `doc-image:<id>` and this is what turns that into something to draw —
// through QTextDocument::loadResource(), which is the hook Qt provides for
// exactly this. Without it a page's pictures would be broken images.
//
// **Paste from Word** is cleaned on the way in by WordPasteCleaner: the
// structure survives and Word's fonts, sizes and colours do not. A paste
// that carries an image rather than HTML is stored and referred to the same
// way, so a screenshot pasted straight from the clipboard behaves like one
// inserted from a file.
class DocumentRichEdit : public QTextEdit
{
    Q_OBJECT

public:
    // The scheme content uses to refer to a stored picture. Public because
    // the editor writes it and the tests read it.
    static const char* imageScheme();

    // `images` is where pictures are stored and loaded. A reference, because
    // the module owns it for longer than any screen.
    DocumentRichEdit(DocumentImageStore& images, QWidget* parent = nullptr);

    // Stores the picture and puts a reference to it at the cursor.
    bool insertStoredImage(const QImage& image, QString* error);

protected:
    // Where paste is cleaned (§6.1).
    void insertFromMimeData(const QMimeData* source) override;

private:
    // Pulls `data:` pictures out of pasted HTML into the image store and
    // rewrites the HTML to refer to them, so a paste from a web page or a
    // modern Word keeps its pictures instead of leaving dead references.
    QString importDataUriImages(const QString& html);

    DocumentImageStore& m_images;
};
