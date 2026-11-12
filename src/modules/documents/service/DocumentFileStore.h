#pragma once

#include "modules/documents/domain/Document.h"

#include <QString>

class VesselRepository;

// Where a document's files go, and how they get there
// (document-management-spec.md §4.2).
//
// The layout inside the documents folder:
//
//     <documents folder>/Fleet/<CATEGORY>/…
//     <documents folder>/<IMO>/<CATEGORY>/…
//
// so that a person opening the folder in a file browser can find a document
// without the program's help — which matters for paperwork that may have to
// be produced during an inspection on a computer that will not start.
//
// **A version is never overwritten.** Each one is a new file named after the
// document and its sequence, so every older version stays exactly where it
// was and stays openable. That is the whole point of the module's history.
//
// The copying itself is `ManagedFileStore`, in core/, which already knows how
// to copy a file into a managed folder without colliding with what is there.
// Nothing here writes file code of its own (§4.2).
//
// Every method takes the documents folder as a parameter rather than reading
// it from settings. The folder can be changed from Settings while the program
// runs, so a captured copy would go stale — and tests then run entirely
// inside a QTemporaryDir (CLAUDE.md §10.1).
class DocumentFileStore
{
public:
    // The vessel repository is for one thing: turning a document's vessel id
    // into the IMO number its folder is named after.
    explicit DocumentFileStore(VesselRepository& vessels);

    // "Fleet/SMS" or "9074729/CIRCULAR", relative to the documents folder.
    // Empty with *errorString set when the vessel cannot be read or has no
    // IMO number — a folder named after an empty string would quietly collect
    // every vessel's documents in one place.
    QString relativeFolderFor(const Document& document, QString* errorString);

    // The name a version's file is given: the reference when there is one and
    // the title otherwise, then the sequence, then the source file's own
    // extension — "SMS-03__v5.pdf".
    //
    // Public because it is worth testing directly, and because it says what a
    // person will see in the folder.
    static QString fileNameFor(const Document& document, int sequence,
                               const QString& sourcePath);

    // Copies sourcePath into the right folder under documentsRoot and reports
    // where it landed, relative to that root (CLAUDE.md §6.9 — only the part
    // below the root is ever stored).
    bool store(const QString& documentsRoot, const Document& document, int sequence,
               const QString& sourcePath, QString* relativePathOut, QString* errorString);

    // A stored relative path resolved back to an absolute one, for opening
    // it. Empty for an empty path or an empty root, rather than the folder
    // itself: "no file" must never open the whole documents folder.
    static QString absolutePathFor(const QString& documentsRoot, const QString& relativePath);

private:
    VesselRepository& m_vessels;
};
