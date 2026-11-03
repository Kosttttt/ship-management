#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/domain/CertificateState.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace CertificateTestSupport;

// certificate-attachment-addendum-2-spec §7, and §9 cases 6 and 7: an attached
// file is named after whichever of the expiry date, the next annual survey
// window and the next intermediate survey window is soonest.
//
// Every certificate here is built relative to today rather than from fixed
// dates, because the rule under test is "what needs attention next", which is
// a statement about now. The expected date is then written out from the
// survey calendar by hand — anniversary minus three months, and so on — rather
// than by asking computeCertificateState() for it, which would only assert
// that the function agrees with itself.
class TestCertificateAttachmentNaming : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §7, the field the whole rule rests on.
    void theDueDateIsAlwaysTheDateDaysLeftCountsTo();

    // §7 and §9 case 6.
    void noSurveyRequirementLeavesTheExpiryDate();
    void theAnnualWindowWinsWhenItOpensFirst();
    void theIntermediateWindowWinsWhenItIsTheOneDue();

    // §9 case 7.
    void anActiveExtensionNamesTheFileFromItsOwnExpiry();

    void aCertificateThatNeverExpiresHasNoDateInItsName();

    // §7's last clause: computed once, at the moment of attaching.
    void anAlreadyAttachedFileIsNotRenamedWhenTheScheduleMoves();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString sourceFile(const QString& name)
    {
        const QString path = QDir(m_source.path()).filePath(name);
        QFile         file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return QString();
        }
        file.write("the document");
        return path;
    }

    static QString activePathOf(QSqlDatabase& db, const QString& certificateId)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT relative_path FROM certificate_attachment"
                                     " WHERE certificate_id = ? AND is_deleted = 0"));
        query.addBindValue(certificateId);
        if (!query.exec() || !query.next()) {
            return QString();
        }
        return query.value(0).toString();
    }

    // The window a survey anniversary opens, per
    // certificate-control-spec §3.2: three months either side.
    static QDate windowOpens(const QDate& anniversary) { return anniversary.addMonths(-3); }
    static QDate windowCloses(const QDate& anniversary) { return anniversary.addMonths(3); }

    // What the generated filename should end in, for a date.
    static QString expectedName(const QString& listNumber,
                                const QString& name,
                                const QDate&   date)
    {
        return QStringLiteral("%1_%2_%3.pdf")
            .arg(listNumber, name, date.toString(Qt::ISODate));
    }

    // Loads a seeded certificate back out, so what is attached to is a real
    // row rather than a struct a test filled in.
    Certificate load(const QString& id)
    {
        QSqlDatabase               db = database();
        CertificateRepository      certificates(db, officeContext());
        std::optional<Certificate> loaded;
        certificates.findById(id, &loaded);
        return loaded.value_or(Certificate{});
    }

    QDate         m_today;
    QString       m_connectionName;
    QTemporaryDir m_source;
};

void TestCertificateAttachmentNaming::init()
{
    m_connectionName = QStringLiteral("naming_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_source.isValid());

    // Read once per test, and the same value the service will read a moment
    // later. The service reads the clock itself — §7 says the name is a
    // snapshot taken when the file is attached — so there is no parameter to
    // inject here the way the pure state calculation allows.
    m_today = QDate::currentDate();
}

void TestCertificateAttachmentNaming::cleanup()
{
    closeDatabase(m_connectionName);
}

// §7's premise, stated against the domain function on its own: dueDate and
// daysLeft always describe the same date. Everything else in this suite
// depends on that being true.
void TestCertificateAttachmentNaming::theDueDateIsAlwaysTheDateDaysLeftCountsTo()
{
    const AlertThresholds thresholds;

    // Nothing outstanding: the expiry date.
    Certificate plain;
    plain.issueDate  = m_today.addYears(-1);
    plain.expiryDate = m_today.addYears(4);

    CertificateState state = computeCertificateState(plain, {}, thresholds, m_today);
    QCOMPARE(state.dueDate, plain.expiryDate);
    QCOMPARE(state.daysLeft, m_today.daysTo(state.dueDate));

    // A survey outstanding: the date that window closes. Asserted exactly,
    // not merely as "before expiry" — the old before/after-the-opening switch
    // also satisfied that weaker claim, so it could not have caught the bug
    // certificate-survey-deadline-fix.md describes. Today is well before this
    // window opens, which is precisely the case that used to be wrong.
    Certificate withSurvey      = plain;
    withSurvey.issueDate        = m_today;
    withSurvey.expiryDate       = m_today.addYears(5);
    withSurvey.requiresAnnualSurvey = true;

    const QDate firstAnniversary = withSurvey.expiryDate.addYears(-4);
    QVERIFY(m_today < windowOpens(firstAnniversary));

    state = computeCertificateState(withSurvey, {}, thresholds, m_today);
    QCOMPARE(state.dueDate, windowCloses(firstAnniversary));
    QCOMPARE(state.daysLeft, m_today.daysTo(state.dueDate));
    QVERIFY2(state.dueDate < withSurvey.expiryDate, qPrintable(state.dueDate.toString()));

    // Nothing to count to at all: invalid, which is also what leaves a date
    // out of the filename.
    Certificate never;
    never.issueDate = m_today;
    state           = computeCertificateState(never, {}, thresholds, m_today);
    QVERIFY(never.neverExpires());
    QVERIFY(!state.dueDate.isValid());
}

void TestCertificateAttachmentNaming::noSurveyRequirementLeavesTheExpiryDate()
{
    QSqlDatabase db = database();

    const QDate expiry = m_today.addYears(3);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Tonnage"), QStringLiteral("7"),
                                m_today.addYears(-2), expiry, /*requiresAnnual=*/false);

    AttachmentFixture attachments(db, officeContext());
    QVERIFY(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                               sourceFile(QStringLiteral("scan.pdf")), false));

    // §7: with no survey schedule there is nothing sooner to compare the
    // expiry date to, so the behaviour is exactly what it was before.
    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             expectedName(QStringLiteral("7"), QStringLiteral("Tonnage"), expiry));
}

// §9 case 6, first way round. Issued today with a five-year term, so the first
// annual anniversary is a year out and the intermediate window is a year
// further still — the annual is plainly the thing that needs attention next.
void TestCertificateAttachmentNaming::theAnnualWindowWinsWhenItOpensFirst()
{
    QSqlDatabase db = database();

    const QDate issue  = m_today;
    const QDate expiry = m_today.addYears(5);

    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Load Line"), QStringLiteral("15D"), issue, expiry,
                                /*requiresAnnual=*/true, /*requiresIntermediate=*/true);

    // The anniversaries are expiry minus 4, 3, 2 and 1 years. Nothing is
    // recorded, so the first of them is outstanding, and its deadline is the
    // day its window closes — three months *after* the anniversary, not the
    // three-months-before opening (certificate-survey-deadline-fix.md §3).
    const QDate firstAnniversary = expiry.addYears(-4);
    const QDate expected         = windowCloses(firstAnniversary);

    // Sanity, so a failure below is about the rule and not about the dates:
    // this annual window closes before the intermediate window even opens, so
    // the annual is unambiguously the one that needs attention next.
    QVERIFY2(expected < windowOpens(expiry.addYears(-3)), qPrintable(expected.toString()));
    // And the window really has not opened yet, which is the case the fix
    // changed — today is still a good nine months short of it.
    QVERIFY(m_today < windowOpens(firstAnniversary));

    AttachmentFixture attachments(db, officeContext());
    QVERIFY2(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                                sourceFile(QStringLiteral("scan.pdf")), false),
             qPrintable(attachments.service.errorString()));

    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             expectedName(QStringLiteral("15D"), QStringLiteral("Load_Line"), expected));
}

// §9 case 6, the other way round. A certificate three years into its term with
// its first three annuals signed off: the next annual is a year away, while
// the intermediate window is open right now.
void TestCertificateAttachmentNaming::theIntermediateWindowWinsWhenItIsTheOneDue()
{
    QSqlDatabase db = database();

    const QDate expiry = m_today.addYears(2).addMonths(3);
    const QDate issue  = expiry.addYears(-5);

    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Safety Equipment"), QStringLiteral("9"), issue,
                                expiry, /*requiresAnnual=*/true, /*requiresIntermediate=*/true);

    // Anniversaries: expiry minus 4, 3, 2 and 1 years.
    const QDate first  = expiry.addYears(-4);
    const QDate second = expiry.addYears(-3);
    const QDate third  = expiry.addYears(-2);

    // The first three annuals signed off, each on its own anniversary, so the
    // next outstanding annual is the fourth — a year out.
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"),
                    QStringLiteral("ANNUAL"), first);
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"),
                    QStringLiteral("ANNUAL"), second);
    seedEndorsement(db, QStringLiteral("e3"), QStringLiteral("c1"),
                    QStringLiteral("ANNUAL"), third);

    // The intermediate window runs from the second anniversary's opening to
    // the third's closing (certificate-control-spec §3.2), and today is inside
    // it. No intermediate survey has been recorded, so it is outstanding — and
    // an outstanding window always counts to its closing date (§4.4). This
    // expectation did not move with certificate-survey-deadline-fix.md: today
    // is already past the opening here, so the old before/after switch picked
    // the close as well.
    const QDate intermediateOpens  = windowOpens(second);
    const QDate intermediateCloses = windowCloses(third);
    QVERIFY(m_today >= intermediateOpens);
    QVERIFY(m_today <= intermediateCloses);

    // And the next annual really is further off, which is what makes this the
    // mirror image of the test above.
    QVERIFY2(intermediateCloses < windowOpens(expiry.addYears(-1)),
             qPrintable(intermediateCloses.toString()));

    AttachmentFixture attachments(db, officeContext());
    QVERIFY2(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                                sourceFile(QStringLiteral("scan.pdf")), false),
             qPrintable(attachments.service.errorString()));

    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             expectedName(QStringLiteral("9"), QStringLiteral("Safety_Equipment"),
                          intermediateCloses));
}

// §9 case 7: while a short-term extension is in force it is the document that
// matters, so it is the date the file is named after — consistent with the
// list's Expiry column, which already substitutes it.
void TestCertificateAttachmentNaming::anActiveExtensionNamesTheFileFromItsOwnExpiry()
{
    QSqlDatabase db = database();

    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Load Line"), QStringLiteral("15D"), m_today,
                                m_today.addYears(5), /*requiresAnnual=*/true);

    AttachmentFixture attachments(db, officeContext());

    const QDate          extensionExpiry = m_today.addMonths(2);
    CertificateExtension extension;
    extension.certificateId = QStringLiteral("c1");
    extension.expiryDate    = extensionExpiry;
    QVERIFY2(attachments.extensions.start(extension),
             qPrintable(attachments.extensions.errorString()));

    QVERIFY2(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                                sourceFile(QStringLiteral("scan.pdf")), false),
             qPrintable(attachments.service.errorString()));

    // The extension's date, not the certificate's expiry and not its survey
    // window — survey evaluation is suppressed entirely while one is active
    // (certificate-extension-spec §2).
    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             expectedName(QStringLiteral("15D"), QStringLiteral("Load_Line"), extensionExpiry));
}

void TestCertificateAttachmentNaming::aCertificateThatNeverExpiresHasNoDateInItsName()
{
    QSqlDatabase db = database();

    // An invalid expiry seeds SQL NULL, which is how "does not expire" is
    // stored (CLAUDE.md §6.3).
    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Tonnage"), QStringLiteral("7"),
                                m_today.addYears(-1), QDate(), /*requiresAnnual=*/false);

    AttachmentFixture attachments(db, officeContext());
    QVERIFY2(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                                sourceFile(QStringLiteral("scan.pdf")), false),
             qPrintable(attachments.service.errorString()));

    QCOMPARE(activePathOf(db, QStringLiteral("c1")), QStringLiteral("7_Tonnage.pdf"));
}

// §7's last clause. The schedule moves on as surveys are recorded; a file
// already on disk does not follow it, because renaming it would break the
// stored path and gain nothing.
void TestCertificateAttachmentNaming::anAlreadyAttachedFileIsNotRenamedWhenTheScheduleMoves()
{
    QSqlDatabase db = database();

    const QDate expiry = m_today.addYears(5);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Load Line"), QStringLiteral("15D"), m_today,
                                expiry, /*requiresAnnual=*/true);

    AttachmentFixture attachments(db, officeContext());
    QVERIFY(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                               sourceFile(QStringLiteral("scan.pdf")), false));

    const QString storedBefore = activePathOf(db, QStringLiteral("c1"));
    QCOMPARE(storedBefore,
             expectedName(QStringLiteral("15D"), QStringLiteral("Load_Line"),
                          windowCloses(expiry.addYears(-4))));

    // The first annual signed off, which moves the next window a year out.
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    expiry.addYears(-4));

    // Nothing re-reads or renames it: the row and the file are exactly as they
    // were.
    QCOMPARE(activePathOf(db, QStringLiteral("c1")), storedBefore);
    QVERIFY(QFileInfo::exists(QDir(attachments.rootPath()).filePath(storedBefore)));

    // It is the *next* attachment that carries the new date, which is the
    // behaviour §7 asks for: computed once, each time, at the moment of
    // attaching.
    QVERIFY(attachments.service.replaceCurrent(load(QStringLiteral("c1")),
                                               sourceFile(QStringLiteral("newer.pdf")), false));
    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             expectedName(QStringLiteral("15D"), QStringLiteral("Load_Line"),
                          windowCloses(expiry.addYears(-3))));
}

QTEST_MAIN(TestCertificateAttachmentNaming)
#include "tst_CertificateAttachmentNaming.moc"
