#pragma once

#include <QString>

class VesselRepository;

// Which folder one vessel's documents live in, and what happens when that
// answer changes (certificate-attachment-addendum-spec §2).
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
    // The one folder the application creates inside a vessel's chosen root.
    //
    // The *layout underneath* it — attachments/<certificateId>/archive and so
    // on — stays the certificates module's business and is written down in
    // CertificateAttachmentStorage. This top name is here because §2.5 has to
    // move the subtree wholesale when a vessel's folder changes, and the
    // module cannot be the one doing that; the module builds its paths from
    // this constant, so the string still appears exactly once.
    static const QString kManagedSubfolder;

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

    // Where a *stored* relative path actually is, for opening it.
    //
    // Not subject to the refusal above, on purpose. §2.2 requires an
    // attachment made before this addendum shipped to go on opening, and such
    // an attachment physically sits under the installation's own folder. So
    // this prefers the vessel's configured folder when the file is really
    // there and falls back to the pre-addendum location when it is not.
    //
    // The fallback is read-only and existence-checked. Nothing is ever written
    // through it: that would be the silent fallback §2.2 forbids.
    QString resolveForOpening(const QString& vesselId, const QString& relativePath);

    // Points a vessel at a different folder, taking its documents with it
    // (§2.5), in the one order that cannot leave the two disagreeing:
    //
    //   1. validate the new folder — it must exist, be a folder, be writable,
    //      and not already hold a managed subfolder of its own;
    //   2. move the existing subtree, if there is one;
    //   3. only then store the new path.
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
