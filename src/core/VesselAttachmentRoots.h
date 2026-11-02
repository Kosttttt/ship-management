#pragma once

#include <QString>

class VesselRepository;

// Which folder one vessel's documents live in, and what happens when that
// answer changes (certificate-attachment-addendum-spec §2, as amended by
// certificate-attachment-addendum-2-spec §3).
//
// This exists because "where do this vessel's files go?" is a question about
// vessels, and ManagedFileStore is deliberately ignorant of vessels — it moves
// and copies files under a root it is handed. So the policy lives here: look
// the folder up, refuse when there is none, and sequence a change of folder so
// that the database never claims a location the files are not actually in.
//
// In core/ rather than in the certificates module because the vessel owns the
// concept (§2.3). The Document Management module on this project's roadmap
// will store files against a vessel too, and should ask this rather than
// invent a second answer.
//
// No SQL of its own: it asks VesselRepository, which is the only place vessel
// SQL is allowed to live (CLAUDE.md §4 rule 2).
class VesselAttachmentRoots
{
public:
    // The hidden file that records which vessel a folder belongs to
    // (certificate-attachment-addendum-2-spec §3.3). It holds that vessel's
    // id and nothing else.
    //
    // It exists because the flattened layout removed the thing the old check
    // looked for. Until addendum 2 a vessel's documents sat under an
    // "attachments" subfolder, so a folder already holding one obviously
    // belonged to another vessel; now the documents sit directly in the folder
    // and there is nothing structural left to recognise. This file is that
    // recognition, written once when the folder is configured and read only
    // when a folder is being configured.
    static const QString kMarkerFileName;

    explicit VesselAttachmentRoots(VesselRepository& vessels);

    // The folder to write this vessel's documents into, or an empty string
    // when there is none yet — with *errorString set to something a crew
    // member can act on.
    //
    // §2.2 is categorical about this: an empty answer is a refusal, never a
    // licence to fall back to the installation's own folder. Every caller
    // reports the message and stops.
    //
    // ManagedFileStore's test override is honoured ahead of the lookup, with
    // the same precedence it has always had (§2.4), which is what lets a test
    // exercise the attachment workflow without configuring a vessel at all.
    QString rootForWriting(const QString& vesselId, QString* errorString);

    // The root a *stored* relative path is actually found under, or an empty
    // string when it is nowhere.
    //
    // Prefers the vessel's own configured folder and falls back to the
    // installation's own folder, which is where an attachment made before the
    // addendum shipped physically sits (§2.2). Existence-checked in both
    // cases, which is what makes the fallback safe to offer at all.
    //
    // Read-only by intention. Nothing is written through it: that would be the
    // silent fallback §2.2 forbids. Deleting something is allowed, because a
    // delete acts on the file that is actually there rather than choosing
    // where a new one should go.
    QString rootHolding(const QString& vesselId, const QString& relativePath);

    // Where a stored relative path actually is, as an absolute path, for
    // opening it. Built on rootHolding(), and not subject to the refusal
    // above — §2.2 requires an attachment made before this addendum shipped to
    // go on opening.
    QString resolveForOpening(const QString& vesselId, const QString& relativePath);

    // Points a vessel at a different folder, taking its documents with it
    // (§2.5), in the one order that cannot leave the two disagreeing:
    //
    //   1. validate the new folder — it must exist, be a folder, be writable,
    //      and not already carry another vessel's marker file (§3.3);
    //   2. move what is in the old folder across;
    //   3. store the new path;
    //   4. and only then claim the folder with a marker file of its own.
    //
    // A failure at any step changes nothing at all: the stored path still
    // names the folder the files are still in. Choosing the folder a vessel is
    // already pointed at succeeds and moves nothing.
    bool changeRoot(const QString& vesselId, const QString& newRootPath, QString* errorString);

private:
    // The stored column for one vessel, with "vessel not found" distinguished
    // from "found, but nothing configured".
    bool storedRoot(const QString& vesselId, QString* rootOut, QString* errorString);

    VesselRepository& m_vessels;
};
