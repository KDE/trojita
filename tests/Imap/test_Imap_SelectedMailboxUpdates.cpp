/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License or (at your option) version 3 or any later version
   accepted by the membership of KDE e.V. (or its successor approved
   by the membership of KDE e.V.), which shall act as a proxy
   defined in Section 14 of version 3 of the license.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <QtTest>
#include "test_Imap_SelectedMailboxUpdates.h"
#include "Imap/Model/DummyNetworkWatcher.h"
#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/MemoryCache.h"
#include "Imap/Parser/Uids.h"
#include "Streams/FakeSocket.h"
#include "Imap/data.h"

/** @short Test that we survive a new message arrival and its subsequent removal in rapid sequence

The code won't notice that the message got expunged immediately (simply because it has no idea of a
next response when processing the first EXISTS), will ask for UID and FLAGS, but the message gets
deleted (and EXPUNGE sent) before the server receives the UID FETCH command. This leads to us having
a flawed idea of UIDNEXT, unless the server provides a hint via the * OK [UIDNEXT xyz] response.

It's a question whether or not to at least increment the UIDNEXT by one when the response doesn't
arrive. Doing that would probably break when the server employs an Outlook-workaround for IDLE with
a fake EXISTS/EXPUNGE responses.
*/
void ImapModelSelectedMailboxUpdatesTest::helperTestExpungeImmediatelyAfterArrival(bool sendUidNext)
{
    existsA = 3;
    uidValidityA = 6;
    uidMapA << 1 << 7 << 9;
    uidNextA = 16;
    helperSyncAWithMessagesEmptyState();
    helperCheckCache();
    cServer("* 1 FETCH (FLAGS ())\r\n");
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cEmpty();

    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QCOMPARE(static_cast<uint>(oldUidMap.size()), oldState.exists());
    QSignalSpy numbersWatcher(model, SIGNAL(messageCountPossiblyChanged(QModelIndex)));
    cServer(QString::fromUtf8("* %1 EXISTS\r\n* %1 EXPUNGE\r\n").arg(QString::number(existsA + 1)).toUtf8());
    cClient(QString(t.mk("UID FETCH %1:* (FLAGS)\r\n")).arg(QString::number(qMax(uidMapA.last() + 1, uidNextA))).toUtf8());
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);
    QCOMPARE(numbersWatcher.size(), 2);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    QCOMPARE(numbersWatcher[1][0].toModelIndex(), QModelIndex(idxA));
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);

    // Add message with this UID to our internal list
    uint addedUid = 33;
    uidNextA = addedUid + 1;

    QByteArray uidUpdateResponse = sendUidNext ? QStringLiteral("* OK [UIDNEXT %1] courtesy of the server\r\n").arg(
            QString::number(uidNextA)).toUtf8() : QByteArray();

    // ...but because it got deleted, here we go
    cServer(t.last("OK empty fetch\r\n") + uidUpdateResponse);
    cEmpty();
    QVERIFY(errorSpy->isEmpty());

    helperCheckCache( ! sendUidNext );
    helperVerifyUidMapA();
}

void ImapModelSelectedMailboxUpdatesTest::testUnsolicitedFetch()
{
    existsA = 2;
    uidValidityA = 666;
    uidMapA << 3 << 9;
    uidNextA = 33;
    helperSyncAWithMessagesEmptyState();
    helperCheckCache();
    cEmpty();

    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QCOMPARE(static_cast<uint>(oldUidMap.size()), oldState.exists());
    cServer(QString::fromUtf8("* %1 EXISTS\r\n* %1 FETCH (FLAGS (\\Seen \\Recent $NotJunk NotJunk))\r\n").arg(QString::number(existsA + 1)).toUtf8());
    cClient(QString(t.mk("UID FETCH %1:* (FLAGS)\r\n")).arg(
                 QString::number(qMax(uidMapA.last() + 1, uidNextA))).toUtf8());
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);

    // Add message with this UID to our internal list
    uint addedUid = 42;
    ++existsA;
    uidMapA << addedUid;
    uidNextA = addedUid + 1;

    cServer(QString("* %1 FETCH (FLAGS (\\Seen \\Recent $NotJunk NotJunk) UID %2)\r\n").arg(
            QString::number(existsA), QString::number(addedUid)).toUtf8() +
                       t.last("OK flags returned\r\n"));
    cEmpty();
    QVERIFY(errorSpy->isEmpty());

    helperCheckCache();
    helperVerifyUidMapA();
}

/** @short Test a rapid EXISTS/EXPUNGE sequence

@see helperTestExpungeImmediatelyAfterArrival for details
*/
void ImapModelSelectedMailboxUpdatesTest::testExpungeImmediatelyAfterArrival()
{
    helperTestExpungeImmediatelyAfterArrival(false);
}

/** @short Test a rapid EXISTS/EXPUNGE sequence with an added UIDNEXT response

@see helperTestExpungeImmediatelyAfterArrival for details
*/
void ImapModelSelectedMailboxUpdatesTest::testExpungeImmediatelyAfterArrivalWithUidNext()
{
    helperTestExpungeImmediatelyAfterArrival(true);
}

/** @short Test generic traffic to an opened mailbox without asking for message data too soon */
void ImapModelSelectedMailboxUpdatesTest::testGenericTraffic()
{
    helperGenericTraffic(false);
}

/** @short Test generic traffic to an opened mailbox when we ask for message metadata as soon as they arrive */
void ImapModelSelectedMailboxUpdatesTest::testGenericTrafficWithEnvelopes()
{
    helperGenericTraffic(true);
}

void ImapModelSelectedMailboxUpdatesTest::helperGenericTraffic(bool askForEnvelopes)
{
    // At first, sync to an empty state
    uidNextA = 12;
    uidValidityA = 333;
    helperSyncANoMessagesCompleteState();

    // Fake delivery of A, B and C
    helperGenericTrafficFirstArrivals(askForEnvelopes);

    // Now add one more message, the D
    helperGenericTrafficArrive2(askForEnvelopes);

    // Remove B
    helperDeleteOneMessage(1, QStringList() << QStringLiteral("A") << QStringLiteral("C") << QStringLiteral("D"));

    // Remove D
    helperDeleteOneMessage(2, QStringList() << QStringLiteral("A") << QStringLiteral("C"));

    // Remove A
    helperDeleteOneMessage(0, QStringList() << QStringLiteral("C"));

    // Remove C
    helperDeleteOneMessage(0, QStringList());

    // Add completely different messages, A, B, C and D
    helperGenericTrafficArrive3(askForEnvelopes);

    // remove C and B
    helperDeleteOneMessage(2, QStringList() << QStringLiteral("A") << QStringLiteral("B") << QStringLiteral("D"));
    helperDeleteOneMessage(1, QStringList() << QStringLiteral("A") << QStringLiteral("D"));

    // Add E, F and G
    helperGenericTrafficArrive4(askForEnvelopes);

    // Delete D and F
    helperDeleteTwoMessages(3, 1, QStringList() << QStringLiteral("A") << QStringLiteral("E") << QStringLiteral("G"));

    // Delete G and A
    helperDeleteTwoMessages(2, 0, QStringList() << QStringLiteral("E"));
}

/** @short Test an arrival of three brand new messages to an already synced mailbox

This function assumes that mailbox A is already openened and is active and synced. It will fake an arrival
of three brand new messages, A, B and C. This arrival will get noticed and processed.
 */
void ImapModelSelectedMailboxUpdatesTest::helperGenericTrafficFirstArrivals(bool askForEnvelopes)
{
    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QCOMPARE(static_cast<uint>(oldUidMap.size()), oldState.exists());
    // Fake delivery of A, B and C
    cServer(QByteArray("* 3 EXISTS\r\n* 3 RECENT\r\n"));
    // This should trigger a request for flags
    cClient(t.mk("UID FETCH 12:* (FLAGS)\r\n"));
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);

    // The messages sgould be there already
    QVERIFY(msgListA.model()->index(0, 0, msgListA).isValid());
    QModelIndex msgB = msgListA.model()->index(1, 0, msgListA);
    QVERIFY(msgB.isValid());
    QVERIFY(msgListA.model()->index(2, 0, msgListA).isValid());
    QVERIFY( ! msgListA.model()->index(3, 0, msgListA).isValid());
    // We shouldn't have the UID yet
    QCOMPARE(msgB.data(Imap::Mailbox::RoleMessageUid).toUInt(), 0u);

    // Verify various message counts
    // This one is parsed from RECENT
    QCOMPARE(idxA.data(Imap::Mailbox::RoleRecentMessageCount).toInt(), 3);
    // This one is easy, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleTotalMessageCount).toInt(), 3);
    // We have to wait for FLAGS for this one
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 0);

    if ( askForEnvelopes ) {
        // In the meanwhile, ask for ENVELOPE etc -- but it shouldn't be there yet
        QVERIFY( ! msgB.data(Imap::Mailbox::RoleMessageFrom).isValid() );
    }

    // FLAGS arrive, bringing the UID with them
    cServer(QByteArray("* 1 FETCH (UID 43 FLAGS (\\Recent))\r\n"
                                  "* 2 FETCH (UID 44 FLAGS (\\Recent))\r\n"
                                  "* 3 FETCH (UID 45 FLAGS (\\Recent))\r\n") +
                       t.last("OK fetched\r\n"));
    // The UIDs shall be known at this point
    QCOMPARE(msgB.data(Imap::Mailbox::RoleMessageUid).toUInt(), 44u);
    // The unread message count should be correct now, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 3);
    if ( askForEnvelopes ) {
        // The ENVELOPE and related fields should be requested now
        cClient(t.mk("UID FETCH 43:44 (" FETCH_METADATA_ITEMS ")\r\n"));
    }

    existsA = 3;
    uidNextA = 46;
    uidMapA << 43 << 44 << 45;
    helperCheckCache();
    helperVerifyUidMapA();

    // Yes, we're counting to one more than what actually is here; that's because we want to prevent a possible out-of-bounds access
    for ( int i = 0; i < static_cast<int>(existsA) + 1; ++i )
        msgListA.model()->index(i, 0, msgListA).data(Imap::Mailbox::RoleMessageSubject);

    QModelIndex uid43 = msgListA.model()->index(0, 0, msgListA);
    Q_ASSERT(uid43.isValid());
    QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageUid).toUInt(), 43u);

    if ( askForEnvelopes ) {
        // Envelopes for A and B already got requested
        // We have to preserve the previous command, too
        QByteArray completionForFirstFetch = t.last("OK fetched\r\n");
        cClient(t.mk("UID FETCH 45 (" FETCH_METADATA_ITEMS ")\r\n"));
        // Simulate out-of-order execution here -- the completion responses for both commands arrive only
        // after the actual data for both of them got pushed

        // Also test the header parsing
        QByteArray headerData("List-Post: <mailto:gentoo-dev@lists.gentoo.org>\r\n"
                              "References: <20121031120002.5C37D5807C@linuxized.com> "
                              "<CAKmKYaDZtfZ9wzKML8WgJ=evVhteyOG0RVfsASpBGViwncsaiQ@mail.gmail.com>\r\n"
                              " <50911AE6.8060402@gmail.com>\r\n"
                              "\r\n");
        cServer("* 1 FETCH (BODY[HEADER.FIELDS (List-Post References fail)]" + asLiteral(headerData) + ")\r\n");

        cServer(helperCreateTrivialEnvelope(1, 43, QLatin1String("A")) +
                           helperCreateTrivialEnvelope(2, 44, QLatin1String("B")) +
                           helperCreateTrivialEnvelope(3, 45, QLatin1String("C")) +
                           completionForFirstFetch +
                           t.last("OK fetched\r\n"));


        QList<QByteArray> receivedReferences = uid43.data(Imap::Mailbox::RoleMessageHeaderReferences).value<QList<QByteArray> >();
        QCOMPARE(receivedReferences,
                 QList<QByteArray>() << "20121031120002.5C37D5807C@linuxized.com"
                 << "CAKmKYaDZtfZ9wzKML8WgJ=evVhteyOG0RVfsASpBGViwncsaiQ@mail.gmail.com" << "50911AE6.8060402@gmail.com");
        QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageHeaderListPost).toList(),
                 QVariantList() << QUrl("mailto:gentoo-dev@lists.gentoo.org"));
        QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageHeaderListPostNo).toBool(), false);
    } else {
        // Requesting all envelopes at once
        cClient(t.mk("UID FETCH 43:45 (" FETCH_METADATA_ITEMS ")\r\n"));

        // test header parsing as well
        QByteArray headerData("List-Post: NO (disabled)\r\n\r\n");
        cServer("* 1 FETCH (BODY[HEADER.FIELDS (References List-Post)]" + asLiteral(headerData) + ")\r\n");

        cServer(helperCreateTrivialEnvelope(1, 43, QLatin1String("A")) +
                           helperCreateTrivialEnvelope(2, 44, QLatin1String("B")) +
                           helperCreateTrivialEnvelope(3, 45, QLatin1String("C")) +
                           t.last("OK [UIDNEXT 46] fetched\r\n"));
        QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageHeaderReferences).value<QList<QByteArray> >(), QList<QByteArray>());
        QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageHeaderListPost).toList(), QVariantList());
        QCOMPARE(uid43.data(Imap::Mailbox::RoleMessageHeaderListPostNo).toBool(), true);
    }
    helperCheckSubjects(QStringList() << QStringLiteral("A") << QStringLiteral("B") << QStringLiteral("C"));
    cEmpty();
    justKeepTask();
    QVERIFY( errorSpy->isEmpty() );
}

/** @short Fake delivery of D to a mailbox which already contains A, B and C */
void  ImapModelSelectedMailboxUpdatesTest::helperGenericTrafficArrive2(bool askForEnvelopes)
{
    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QCOMPARE(static_cast<uint>(oldUidMap.size()), oldState.exists());
    // Fake delivery of D
    cServer(QByteArray("* 4 EXISTS\r\n* 4 RECENT\r\n"));
    // This should trigger a request for flags
    cClient(t.mk("UID FETCH 46:* (FLAGS)\r\n"));
    // The messages sgould be there already
    QVERIFY(msgListA.model()->index(0, 0, msgListA).isValid());
    QModelIndex msgD = msgListA.model()->index(3, 0, msgListA);
    QVERIFY(msgD.isValid());
    QVERIFY( ! msgListA.model()->index(4, 0, msgListA).isValid());
    // We shouldn't have the UID yet
    QCOMPARE(msgD.data(Imap::Mailbox::RoleMessageUid).toUInt(), 0u);

    // Verify various message counts
    // This one is parsed from RECENT
    QCOMPARE(idxA.data(Imap::Mailbox::RoleRecentMessageCount).toInt(), 4);
    // This one is easy, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleTotalMessageCount).toInt(), 4);
    // this one isn't available yet
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 3);

    if ( askForEnvelopes ) {
        // In the meanwhile, ask for ENVELOPE etc -- but it shouldn't be there yet
        QVERIFY( ! msgD.data(Imap::Mailbox::RoleMessageFrom).isValid() );
    }
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);

    // FLAGS arrive, bringing the UID with them
    cServer(QByteArray("* 4 FETCH (UID 46 FLAGS (\\Recent))\r\n") +
                       t.last("OK fetched\r\n"));
    // The UIDs shall be known at this point
    QCOMPARE(msgD.data(Imap::Mailbox::RoleMessageUid).toUInt(), 46u);
    // The unread message count should be correct now, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 4);

    if ( askForEnvelopes ) {
        QCoreApplication::processEvents();
        // The ENVELOPE and related fields should be requested now
        cClient(t.mk("UID FETCH 46 (" FETCH_METADATA_ITEMS ")\r\n"));
    }

    existsA = 4;
    uidNextA = 47;
    uidMapA << 46;
    helperCheckCache();
    helperVerifyUidMapA();

    // Request the subject once again
    msgListA.model()->index(3, 0, msgListA).data(Imap::Mailbox::RoleMessageSubject);
    // In contrast to helperGenericTrafficFirstArrivals, we won't query "message #5",ie. one more than how many are
    // actually there. The main motivation is trying to behave in a different manner than before. Hope this helps.

    if ( askForEnvelopes ) {
        // Envelope for D already got requested -> nothing to do here
    } else {
        // The D got requested by an explicit query above
        cClient(t.mk("UID FETCH 46 (" FETCH_METADATA_ITEMS ")\r\n"));
    }
    cServer(helperCreateTrivialEnvelope(4, 46, QLatin1String("D")) + t.last("OK fetched\r\n"));
    helperCheckSubjects(QStringList() << QStringLiteral("A") << QStringLiteral("B") << QStringLiteral("C") << QStringLiteral("D"));
    cEmpty();
    QVERIFY( errorSpy->isEmpty() );
}

/** @short Check subjects of all messages in a given mailbox

This function will check subjects of all mailboxes in the mailbox A against a list of subjects specified in @arg subjects.
*/
void ImapModelSelectedMailboxUpdatesTest::helperCheckSubjects(const QStringList &subjects)
{
    for ( int i = 0; i < subjects.size(); ++i ) {
        QModelIndex index = msgListA.model()->index(i, 0, msgListA);
        QVERIFY(index.isValid());
        QCOMPARE(index.data(Imap::Mailbox::RoleMessageSubject).toString(), subjects[i]);
    }
    // Me sure that there are no more messages
    QVERIFY( ! msgListA.model()->index(subjects.size(), 0, msgListA).isValid() );
}

/** @short Test what happens when the server tells us that one message got deleted */
void ImapModelSelectedMailboxUpdatesTest::helperDeleteOneMessage(const uint seq, const QStringList &remainingSubjects)
{
    // Fake deleting one message
    --existsA;
    uidMapA.remove(seq);
    Q_ASSERT(remainingSubjects.size() == static_cast<int>(existsA));
    Q_ASSERT(uidMapA.size() == static_cast<int>(existsA));
    cServer(QString::fromUtf8("* %1 EXPUNGE\r\n* %2 RECENT\r\n").arg(QString::number(seq+1), QString::number(existsA)).toUtf8());

    // Verify the model's idea about the current state. The cache is updated immediately.
    helperCheckSubjects(remainingSubjects);
    helperCheckCache();
    helperVerifyUidMapA();
}

/** @short Test what happens when the server tells us that one message got deleted */
void ImapModelSelectedMailboxUpdatesTest::helperDeleteTwoMessages(const uint seq1, const uint seq2, const QStringList &remainingSubjects)
{
    // Fake deleting one message
    existsA -= 2;
    uidMapA.remove(seq1);
    uidMapA.remove(seq2);
    Q_ASSERT(remainingSubjects.size() == static_cast<int>(existsA));
    Q_ASSERT(uidMapA.size() == static_cast<int>(existsA));
    cServer(QString::fromUtf8("* %1 EXPUNGE\r\n* %2 EXPUNGE\r\n* %3 RECENT\r\n").arg(QString::number(seq1+1), QString::number(seq2+1), QString::number(existsA)).toUtf8());

    // Verify the model's idea about the current state. The cache is updated immediately.
    helperCheckSubjects(remainingSubjects);
    helperCheckCache();
    helperVerifyUidMapA();
}

/** @short Fake delivery of a completely new set of messages, the A, B, C and D */
void ImapModelSelectedMailboxUpdatesTest::helperGenericTrafficArrive3(bool askForEnvelopes)
{
    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QCOMPARE(static_cast<uint>(oldUidMap.size()), oldState.exists());
    // Fake the announcement of the delivery
    cServer(QByteArray("* 4 EXISTS\r\n* 4 RECENT\r\n"));
    // This should trigger a request for flags
    cClient(t.mk("UID FETCH 47:* (FLAGS)\r\n"));

    // The messages should be there already
    QVERIFY(msgListA.model()->index(0, 0, msgListA).isValid());
    QModelIndex msgD = msgListA.model()->index(3, 0, msgListA);
    QVERIFY(msgD.isValid());
    QVERIFY( ! msgListA.model()->index(4, 0, msgListA).isValid());
    // We shouldn't have the UID yet
    QCOMPARE(msgD.data(Imap::Mailbox::RoleMessageUid).toUInt(), 0u);

    // Verify various message counts
    // This one is parsed from RECENT
    QCOMPARE(idxA.data(Imap::Mailbox::RoleRecentMessageCount).toInt(), 4);
    // This one is easy, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleTotalMessageCount).toInt(), 4);
    // this one isn't available yet
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 0);

    if ( askForEnvelopes ) {
        // In the meanwhile, ask for ENVELOPE etc -- but it shouldn't be there yet
        QVERIFY( ! msgD.data(Imap::Mailbox::RoleMessageFrom).isValid() );
    }
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);

    // FLAGS arrive, bringing the UID with them
    cServer(QByteArray("* 1 FETCH (UID 47 FLAGS (\\Recent))\r\n"
                                  "* 2 FETCH (UID 48 FLAGS (\\Recent))\r\n") );
    // Try to insert a pause here; this could be used to properly play with preloading in future...
    for ( int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    cServer(QByteArray("* 3 FETCH (UID 49 FLAGS (\\Recent))\r\n"
                                  "* 4 FETCH (UID 50 FLAGS (\\Recent))\r\n") +
                       t.last("OK fetched\r\n"));
    // The UIDs shall be known at this point
    QCOMPARE(msgD.data(Imap::Mailbox::RoleMessageUid).toUInt(), 50u);
    // The unread message count should be correct now, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 4);

    if ( askForEnvelopes ) {
        // The ENVELOPE and related fields should be requested now
        cClient(t.mk("UID FETCH 47:50 (" FETCH_METADATA_ITEMS ")\r\n"));
    }

    existsA = 4;
    uidNextA = 51;
    uidMapA.clear();
    uidMapA << 47 << 48 << 49 << 50;
    helperCheckCache();
    helperVerifyUidMapA();

    if ( askForEnvelopes ) {
        // Envelopes already requested -> nothing to do here
    } else {
        // Not requested yet -> do it now
        QVERIFY( ! msgD.data(Imap::Mailbox::RoleMessageFrom).isValid() );
        cClient(t.mk("UID FETCH 47:50 (" FETCH_METADATA_ITEMS ")\r\n"));
    }
    // This is common for both cases; the data should finally arrive
    cServer(helperCreateTrivialEnvelope(1, 47, QLatin1String("A")) +
                       helperCreateTrivialEnvelope(2, 48, QLatin1String("B")) +
                       helperCreateTrivialEnvelope(3, 49, QLatin1String("C")) +
                       helperCreateTrivialEnvelope(4, 50, QLatin1String("D")) +
                       t.last("OK fetched\r\n"));
    helperCheckSubjects(QStringList() << QStringLiteral("A") << QStringLiteral("B") << QStringLiteral("C") << QStringLiteral("D"));

    // Verify UIDd and cache stuff once again to make sure reading data doesn't mess anything up
    helperCheckCache();
    helperVerifyUidMapA();
    cEmpty();
    QVERIFY( errorSpy->isEmpty() );
}

/** @short Fake delivery of three new messages, E, F and G, to a mailbox with A and D */
void ImapModelSelectedMailboxUpdatesTest::helperGenericTrafficArrive4(bool askForEnvelopes)
{
    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    // Fake the announcement of the delivery
    cServer(QByteArray("* 5 EXISTS\r\n* 5 RECENT\r\n"));
    // This should trigger a request for flags
    cClient(t.mk("UID FETCH 51:* (FLAGS)\r\n"));

    QModelIndex msgE = msgListA.model()->index(2, 0, msgListA);
    QVERIFY(msgE.isValid());
    // We shouldn't have the UID yet
    QCOMPARE(msgE.data(Imap::Mailbox::RoleMessageUid).toUInt(), 0u);

    // Verify various message counts
    // This one is parsed from RECENT
    QCOMPARE(idxA.data(Imap::Mailbox::RoleRecentMessageCount).toInt(), 5);
    // This one is easy, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleTotalMessageCount).toInt(), 5);
    // this one isn't available yet
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 2);

    if ( askForEnvelopes ) {
        // In the meanwhile, ask for ENVELOPE etc -- but it shouldn't be there yet
        QVERIFY( ! msgE.data(Imap::Mailbox::RoleMessageFrom).isValid() );
    }
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);

    // FLAGS arrive, bringing the UID with them
    cServer(QByteArray("* 3 FETCH (UID 52 FLAGS (\\Recent))\r\n"
                                  "* 4 FETCH (UID 53 FLAGS (\\Recent))\r\n"
                                  "* 5 FETCH (UID 63 FLAGS (\\Recent))\r\n") +
                       t.last("OK fetched\r\n"));
    // The UIDs shall be known at this point
    QCOMPARE(msgE.data(Imap::Mailbox::RoleMessageUid).toUInt(), 52u);
    // The unread message count should be correct now, too
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 5);

    if ( askForEnvelopes ) {
        QCoreApplication::processEvents();
        // The ENVELOPE and related fields should be requested now
        cClient(t.mk("UID FETCH 52 (" FETCH_METADATA_ITEMS ")\r\n"));
    }

    existsA = 5;
    uidNextA = 64;
    uidMapA << 52 << 53 << 63;
    helperCheckCache();
    helperVerifyUidMapA();

    if ( askForEnvelopes ) {
        // Envelopes already requested -> nothing to do here
    } else {
        // Not requested yet -> do it now
        QVERIFY( ! msgE.data(Imap::Mailbox::RoleMessageFrom).isValid() );
        cClient(t.mk("UID FETCH 52:53,63 (" FETCH_METADATA_ITEMS ")\r\n"));
    }
    // This is common for both cases; the data should finally arrive
    cServer(helperCreateTrivialEnvelope(3, 52, QLatin1String("E")) +
                       helperCreateTrivialEnvelope(4, 53, QLatin1String("F")) +
                       helperCreateTrivialEnvelope(5, 63, QLatin1String("G")) +
                       t.last("OK fetched\r\n"));
    cEmpty();
    helperCheckSubjects(QStringList() << QStringLiteral("A") << QStringLiteral("D") << QStringLiteral("E") << QStringLiteral("F") << QStringLiteral("G"));
    if (askForEnvelopes) {
        // Well, this subject checking led to a fetch() being issued on all of these messages (through the ::data()).
        // The subjects and a lot of other metadata items were already known for all of them, but the INTERNALDATE in particular
        // was still missing. This means that this message was not considered "fetched".
        cClient(t.mk("UID FETCH 53,63 (" FETCH_METADATA_ITEMS ")\r\n"));
        // and yeah, we're cheating because I'm lazy; the data ain't here
        cServer(t.last("OK fetched\r\n"));
    } else {
        cEmpty();
    }

    // Verify UIDd and cache stuff once again to make sure reading data doesn't mess anything up
    helperCheckCache();
    helperVerifyUidMapA();
    cEmpty();
    QVERIFY( errorSpy->isEmpty() );
}

/** @short Check the UIDs in the mailbox against the uidMapA list */
#define helperCheckUidMapFromModel() \
{ \
    QCOMPARE(model->rowCount(msgListA), uidMapA.size()); \
    Imap::Uids actual; \
    for (int i = 0; i < uidMapA.size(); ++i) { \
        actual << msgListA.model()->index(i, 0, msgListA).data(Imap::Mailbox::RoleMessageUid).toUInt(); \
    } \
    QCOMPARE(actual, uidMapA); \
} \

void ImapModelSelectedMailboxUpdatesTest::testVanishedUpdates()
{
    initialMessages(10);

    // Deleting a message in the middle of the range
    cServer("* VANISHED 9\r\n");
    uidMapA.remove(uidMapA.indexOf(9));
    --existsA;
    helperCheckUidMapFromModel();
    helperCheckCache();

    // Deleting the last one
    cServer("* VANISHED 10\r\n");
    uidMapA.remove(uidMapA.indexOf(10));
    --existsA;
    helperCheckUidMapFromModel();
    helperCheckCache();

    // Dleting three at the very start
    cServer("* VANISHED 1:3\r\n");
    uidMapA.remove(uidMapA.indexOf(1));
    uidMapA.remove(uidMapA.indexOf(2));
    uidMapA.remove(uidMapA.indexOf(3));
    existsA -= 3;
    helperCheckUidMapFromModel();
    helperCheckCache();

    QCOMPARE(uidMapA, Imap::Uids() << 4 << 5 << 6 << 7 << 8);

    // A new arrival...
    cServer("* 6 EXISTS\r\n");
    cClient(t.mk("UID FETCH 11:* (FLAGS)\r\n"));
    cServer("* 6 FETCH (UID 11 FLAGS (x))\r\n" + t.last("OK fetched\r\n"));
    uidMapA << 11;
    ++existsA;
    uidNextA = 12;
    helperCheckUidMapFromModel();
    helperCheckCache();

    // Yet another arrival which, unfortunately, gets removed immediately
    cServer("* 7 EXISTS\r\n* VANISHED 12\r\n");
    cClient(t.mk("UID FETCH 12:* (FLAGS)\r\n"));
    cServer(t.last("OK fetched\r\n"));
    uidNextA = 13;
    helperCheckUidMapFromModel();
    helperCheckCache();

    // Two messages arrive, the server sends UID for the last of them and the first one gets expunged
    cServer("* 8 EXISTS\r\n");
    // the on-disk cache is trashed, that's by design because it would otherwise have to contain zeros
    uidMapA << 0 << 0;
    auto oldUidMap = uidMapA;
    existsA += 2;
    helperCheckUidMapFromModel();
    uidMapA.clear();
    // FIXME: don't call this, existsA != uidMapA.size
    // helperCheckCache();
    uidMapA = oldUidMap;
    cServer("* 8 FETCH (UID 14 FLAGS ())\r\n");
    uidMapA[7] = 14;
    uidNextA = 15;
    helperCheckUidMapFromModel();
    // FIXME: don't call this -- early call to cEmpty()
    // helperCheckCache();
    cServer("* VANISHED 13\r\n");
    uidMapA.remove(6);
    --existsA;
    helperCheckUidMapFromModel();
    // FIXME: don't call this: early call to cEmpty()
    //helperCheckCache();
    cClient(t.mk("UID FETCH 13:* (FLAGS)\r\n"));
    cServer("* 7 FETCH (UID 14 FLAGS (x))\r\n" + t.last("OK fetched\r\n"));
    helperCheckUidMapFromModel();
    helperCheckCache();

    cEmpty();
}

inline bool isOdd(const uint i)
{
    return i & 1;
}

inline bool isEven(const uint i)
{
    return ! isOdd(i);
}

void ImapModelSelectedMailboxUpdatesTest::testVanishedWithNonExisting()
{
    initialMessages(10);
    cServer("* VANISHED 0,2,4,6,6,6,8,2,0,10,12\r\n");
    uidMapA.erase(std::remove_if(uidMapA.begin(), uidMapA.end(), isEven), uidMapA.end());
    existsA = uidMapA.size();
    helperCheckUidMapFromModel();
    helperCheckCache();

    // This one has been already reported
    cServer("* VANISHED 2\r\n");
    helperCheckUidMapFromModel();
    helperCheckCache();

    // This one wasn't there yet at all
    cServer("* VANISHED 666\r\n");
    helperCheckUidMapFromModel();
    helperCheckCache();

    cEmpty();
}

/** @short Test what happens when the server informs about new message arrivals twice in a row */
void ImapModelSelectedMailboxUpdatesTest::testMultipleArrivals()
{
    initialMessages(1);
    auto oldState = model->cache()->mailboxSyncState(("a"));
    auto oldUidMap = model->cache()->uidMapping(QStringLiteral("a"));
    QModelIndex index = model->taskModel()->index(0, 0);
    QPersistentModelIndex keepIndex = index.model()->index(0, 0, index);
    QVERIFY(keepIndex.isValid());
    QCOMPARE(keepIndex.data(Imap::Mailbox::RoleTaskIsVisible), QVariant(false));
    cServer("* 2 EXISTS\r\n* 3 EXISTS\r\n");
    QCOMPARE(keepIndex.data(Imap::Mailbox::RoleTaskIsVisible), QVariant(true));
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);
    QByteArray req1 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
    QByteArray resp1 = t.last("OK fetched\r\n");
    // The UIDs are still unknown at this point, and at the same time the client pessimistically assumes that the first command
    // can easily arrive to the server before it sent the second EXISTS, which is why it has no other choice but repeat
    // essentially the same command once again.
    QByteArray req2 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
    QByteArray resp2 = t.last("OK fetched\r\n");
    cClient(req1 + req2);
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);
    // The server will, however, try to be smart in this case and will send the responses back just one time.
    // It's OK to do so per the relevant standards and Trojita won't care.
    cServer("* 2 FETCH (UID 2 FLAGS (m2))\r\n"
            "* 3 FETCH (UID 3 FLAGS (m3))\r\n")
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);
    cServer(resp1);
    QCOMPARE(model->cache()->mailboxSyncState(QLatin1String("a")), oldState);
    QCOMPARE(model->cache()->uidMapping(QLatin1String("a")), oldUidMap);
    QCOMPARE(keepIndex.data(Imap::Mailbox::RoleTaskIsVisible), QVariant(true));
    cServer(resp2);
    QCOMPARE(keepIndex.data(Imap::Mailbox::RoleTaskIsVisible), QVariant(false));
    uidMapA << 2 << 3;
    existsA = 3;
    uidNextA = 4;
    helperCheckUidMapFromModel();
    helperCheckCache();
    QCOMPARE(static_cast<int>(model->cache()->mailboxSyncState("a").exists()), uidMapA.size());
    QCOMPARE(model->cache()->uidMapping("a"), uidMapA);
    // flags for UID 1 arre determined deep inside the test helpers, better not check that
    QCOMPARE(model->cache()->msgFlags("a", 2), QStringList() << "m2");
    QCOMPARE(model->cache()->msgFlags("a", 3), QStringList() << "m3");
    cEmpty();
}

/** @short Similar to testMultipleArrivals, but also check that a request to go to another task is delayed until everything is synced again */
void ImapModelSelectedMailboxUpdatesTest::testMultipleArrivalsBlockingFurtherActivity()
{
    initialMessages(1);
    cServer("* 2 EXISTS\r\n* 3 EXISTS\r\n");
    QByteArray req1 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
    QByteArray resp1 = t.last("OK fetched\r\n");
    // The UIDs are still unknown at this point, and at the same time the client pessimistically assumes that the first command
    // can easily arrive to the server before it sent the second EXISTS, which is why it has no other choice but repeat
    // essentially the same command once again.
    QByteArray req2 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
    QByteArray resp2 = t.last("OK fetched\r\n");

    // Issue a request to go to another mailbox; it will be queued until the responses are finished
    QCOMPARE(model->rowCount(msgListB), 0);

    cClient(req1 + req2);
    // The server will, however, try to be smart in this case and will send the responses back just one time.
    // It's OK to do so per the relevant standards and Trojita won't care.
    cServer("* 2 FETCH (UID 2 FLAGS (m2))\r\n"
            "* 3 FETCH (UID 3 FLAGS (m3))\r\n"
            + resp1 + resp2);
    uidMapA << 2 << 3;
    existsA = 3;
    uidNextA = 4;
    helperCheckUidMapFromModel();
    QCOMPARE(static_cast<int>(model->cache()->mailboxSyncState("a").exists()), uidMapA.size());
    QCOMPARE(model->cache()->uidMapping("a"), uidMapA);
    // flags for UID 1 arre determined deep inside the test helpers, better not check that
    QCOMPARE(model->cache()->msgFlags("a", 2), QStringList() << "m2");
    QCOMPARE(model->cache()->msgFlags("a", 3), QStringList() << "m3");

    // Only now the second SELECT shall be queued
    cClient(t.mk("SELECT b\r\n"));
    cServer(t.last("OK selected\r\n"));
    helperCheckCache();
    cEmpty();
}

/** @short Make sure that a UIDVALIDITY update which does not actually change is value is handled properly */
void ImapModelSelectedMailboxUpdatesTest::testInnocentUidValidityChange()
{
    initialMessages(6);
    cServer("* OK [UIDVALIDITY " + QByteArray::number(uidValidityA) + "] foo\r\n");
    cEmpty();
    QVERIFY(model->isNetworkOnline());
}

/** @short Check that an unexpected change to the UIDVALIDITY while a mailbox is open is handled */
void ImapModelSelectedMailboxUpdatesTest::testUnexpectedUidValidityChange()
{
    initialMessages(6);
    {
        ExpectSingleErrorHere blocker(this);
        cServer("* OK [UIDVALIDITY " + QByteArray::number(uidValidityA + 333) + "] foo\r\n");
    }
    cClient(t.mk("LOGOUT\r\n"));
    QVERIFY(!model->isNetworkAvailable());
}

void ImapModelSelectedMailboxUpdatesTest::testHighestModseqFlags()
{
    initialMessages(1);
    cServer("* 1 FETCH (UID " + QByteArray::number(uidMapA[0]) + " MODSEQ (666) FLAGS ())\r\n");
    helperCheckCache();
    cServer("* 1 FETCH (UID " + QByteArray::number(uidMapA[0]) + " MODSEQ (668) FLAGS ())\r\n");
    helperCheckCache();
    cEmpty();
}

/** @short Make sure that we handle newly arriving messages correctly even if metadata or part fetch is in progress

See bug 329757 for details -- the old version asserted that the TreeItemMsgList was already marked as fetched, which
is no longer true when new messages arrive.
*/
void ImapModelSelectedMailboxUpdatesTest::testFetchAndConcurrentArrival()
{
    using namespace Imap::Mailbox;
    model->setProperty("trojita-imap-delayed-fetch-part", 0);

    initialMessages(1);
    QModelIndex msg1 = msgListA.model()->index(0, 0, msgListA);
    QVERIFY(msg1.isValid());
    QCOMPARE(model->rowCount(msg1), 0);

    // Check new arrivals while we're fetching the BODYSTRUCTURE
    cClient(t.mk("UID FETCH 1 (" FETCH_METADATA_ITEMS ")\r\n"));
    cServer("* 2 EXISTS\r\n"
            + helperCreateTrivialEnvelope(1, 1, QLatin1String("new"))
            + "* 3 EXISTS\r\n"
            + t.last("OK fetched\r\n"));

    // Well, the current code is slightly substandard here; we *could* remember the number of messages for which
    // we're already asked, and increment our lowest-UID estimate by one for each of them. However, this will not be
    // absolutely safe (we might still get the duplicates because it's UID FETCH, not FETCH, and we're using UIDs as
    // offsets, not sequence numbers), so I'm not doing this right now.
    {
        auto req1 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
        auto resp1 = t.last("OK fetched\r\n");
        auto req2 = t.mk("UID FETCH 2:* (FLAGS)\r\n");
        auto resp2 = t.last("OK fetched\r\n");
        // so yup, these are two identical commands
        cClient(req1 + req2);
        cServer("* 2 FETCH (UID 2 FLAGS ())\r\n"
                "* 3 FETCH (UID 3 FLAGS ())\r\n"
                + resp1 + resp2);
        cEmpty();
    }

    // Now check what happens when that number is incremented *once again* while our request for part data is in flight
    QVERIFY(msg1.parent().data(RoleIsFetched).toBool());
    QVERIFY(msg1.data(RoleMessageSubject).isValid());
    QVERIFY(!msg1.data(RoleIsFetched).toBool()); // not fully fetched yet, though
    QModelIndex msg1p1 = msg1.model()->index(0, 0, msg1);
    QVERIFY(msg1p1.isValid());
    QCOMPARE(msg1p1.data(RolePartData).toByteArray(), QByteArray());
    cClient(t.mk("UID FETCH 1 (BODY.PEEK[1])\r\n"));
    cServer("* 4 EXISTS\r\n"
            "* 1 FETCH (UID 1 BODY[1] ahoj)\r\n"
            "* 5 EXISTS\r\n"
            + t.last("OK fetched\r\n"));
    QCOMPARE(msg1p1.data(RolePartData).toByteArray(), QByteArray("ahoj"));

    // Again, two identical responses -- see above
    {
        auto req1 = t.mk("UID FETCH 4:* (FLAGS)\r\n");
        auto resp1 = t.last("OK fetched\r\n");
        auto req2 = t.mk("UID FETCH 4:* (FLAGS)\r\n");
        auto resp2 = t.last("OK fetched\r\n");
        cClient(req1 + req2);
        cServer("* 4 FETCH (UID 4 FLAGS ())\r\n"
                "* 5 FETCH (UID 5 FLAGS ())\r\n"
                + resp1 + resp2);
        cEmpty();
    }

    justKeepTask();
    cEmpty();
}

/** @short GMail sends the flags spontaneously, and because it doesn't do \Recent, the flag set is empty */
void ImapModelSelectedMailboxUpdatesTest::testGMailSpontaneousFlagsAndNoRecent()
{
    initialMessages(1);
    QSignalSpy numbersWatcher(model, SIGNAL(messageCountPossiblyChanged(QModelIndex)));
    cServer("* 2 EXISTS\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 0);
    cServer("* 2 FETCH (UID 5000 MODSEQ (4201) FLAGS ())\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    // Yes, what we do is suboptimal -- it would be better to postone the UID and FLAGS discovery for a tiny moment
    // of time in order to allow for GMail's preemptive FETCH which does contain both UID and the FLAGS...
    cClient(t.mk("UID FETCH 2:* (FLAGS)\r\n"));
    cServer("* 2 FETCH (UID 5000 MODSEQ (4201) FLAGS ())\r\n"
            + t.last("OK fetched\r\n"));

    // Now let's try what happens when that new arrival is actually something we've seen before
    cServer("* 3 EXISTS\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cServer("* 3 FETCH (UID 6000 MODSEQ (333) FLAGS (\\Seen))\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cClient(t.mk("UID FETCH 5001:* (FLAGS)\r\n"));
    cServer("* 3 FETCH (UID 6000 MODSEQ (333) FLAGS (\\Seen))\r\n"
            + t.last("OK fetched\r\n"));

    justKeepTask();
    cEmpty();
}

/** @short Recalculating the message numbers in face of expunges of possibly unknown messages */
void ImapModelSelectedMailboxUpdatesTest::testFlagsRecalcOnExpunge()
{
    initialMessages(2);
    QSignalSpy numbersWatcher(model, SIGNAL(messageCountPossiblyChanged(QModelIndex)));
    cServer("* 1 FETCH (FLAGS ())\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cServer("* 3 EXISTS\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cClient(t.mk("UID FETCH 3:* (FLAGS)\r\n"));
    cServer("* 3 EXPUNGE\r\n");
    QCOMPARE(numbersWatcher.size(), 1);
    QCOMPARE(numbersWatcher[0][0].toModelIndex(), QModelIndex(idxA));
    numbersWatcher.clear();
    QCOMPARE(idxA.data(Imap::Mailbox::RoleUnreadMessageCount).toInt(), 1);
    cServer(t.last("OK fetched\r\n"));
    justKeepTask();
    cEmpty();
}

/** @short Servers reporting UID 0 are buggy, full stop */
void ImapModelSelectedMailboxUpdatesTest::testUid0()
{
    initialMessages(2);
    {
        ExpectSingleErrorHere blocker(this);
        cServer("* 3 EXISTS\r\n* 3 FETCH (UID 0)\r\n");
    }
}

/** @short Check that marking of all messages as read doesn't report dataChanged about messages with UID 0 */
void ImapModelSelectedMailboxUpdatesTest::testMarkAllConcurrentArrival()
{
    initialMessages(1);
    cServer("* 1 FETCH (FLAGS ())\r\n");
    justKeepTask();
    cEmpty();

    QSignalSpy changedSpy(model, SIGNAL(dataChanged(QModelIndex,QModelIndex)));
    connect(model, &QAbstractItemModel::dataChanged, this, &ImapModelSelectedMailboxUpdatesTest::helperDataChangedUidNonZero);
    QSignalSpy messageCountChangedSpy(model, SIGNAL(messageCountPossiblyChanged(QModelIndex)));
    model->markMailboxAsRead(idxA);
    cClient(t.mk("STORE 1:* +FLAGS.SILENT \\Seen\r\n"));
    cServer("* 2 EXISTS\r\n" + t.last("OK marked\r\n"));

    QCOMPARE(changedSpy.size(), 5);
    // 1st pair of signals is for TreeItemMsgList (and also TreeItemMailbox because it uses data from that layer) due to the EXISTS
    QCOMPARE(changedSpy[0][0].toModelIndex(), QModelIndex(msgListA));
    QCOMPARE(changedSpy[0][1].toModelIndex(), QModelIndex(msgListA));
    QCOMPARE(changedSpy[1][0].toModelIndex(), QModelIndex(idxA));
    QCOMPARE(changedSpy[1][1].toModelIndex(), QModelIndex(idxA));
    // now report each changed message
    QCOMPARE(changedSpy[2][0].toModelIndex(), QModelIndex(msgListA.model()->index(0, 0, msgListA)));
    QCOMPARE(changedSpy[2][1].toModelIndex(), QModelIndex(msgListA.model()->index(0, 0, msgListA)));
    // and now a pair of signals for the (list, mailbox) pair due to the mass-update of indexes
    QCOMPARE(changedSpy[3][0].toModelIndex(), QModelIndex(msgListA));
    QCOMPARE(changedSpy[3][1].toModelIndex(), QModelIndex(msgListA));
    QCOMPARE(changedSpy[4][0].toModelIndex(), QModelIndex(idxA));
    QCOMPARE(changedSpy[4][1].toModelIndex(), QModelIndex(idxA));
    // Two bits, one for the initial EXISTS, second for the flag update
    QCOMPARE(messageCountChangedSpy.size(), 2);
    QCOMPARE(messageCountChangedSpy[0][0].toModelIndex(), QModelIndex(idxA));
    QCOMPARE(messageCountChangedSpy[1][0].toModelIndex(), QModelIndex(idxA));

    // This is a crude thing, but the point is that the cache should not have been called with an update to an unknown message
    QCOMPARE(model->cache()->msgFlags(QLatin1String("a"), 0), QStringList());

    cClient(t.mk("UID FETCH 2:* (FLAGS)\r\n"));
    cServer("* 2 FETCH (UID 1002 FLAGS (foo))\r\n" + t.last("OK done\r\n"));
    QCOMPARE(model->cache()->msgFlags(QLatin1String("a"), 0), QStringList());
    QCOMPARE(model->cache()->msgFlags(QLatin1String("a"), 1002), QStringList() << QLatin1String("foo"));
    justKeepTask();
    cEmpty();
}

/** @short Helper for testMarkAllConcurrentArrival */
void ImapModelSelectedMailboxUpdatesTest::helperDataChangedUidNonZero(const QModelIndex &a, const QModelIndex &b)
{
    QVERIFY(a.isValid());
    QVERIFY(b.isValid());
    if (a.parent() == msgListA) {
        QVERIFY(a.data(Imap::Mailbox::RoleMessageUid).toUInt() != 0);
        QVERIFY(b.data(Imap::Mailbox::RoleMessageUid).toUInt() != 0);
    }
}

void ImapModelSelectedMailboxUpdatesTest::testLogoutClosed()
{
    Imap::Mailbox::DummyNetworkWatcher nm(nullptr, model);
    model->switchToMailbox(idxA);
    cClient(t.mk("SELECT a\r\n"));
    cServer("* 0 EXISTS\r\n"
            + t.last("OK selected\r\n"));
    model->switchToMailbox(idxB);
    cClient(t.mk("SELECT b\r\n"));
    auto okSelectedB = t.last("OK selected\r\n");
    nm.setNetworkOffline();
    cClient(t.mk("LOGOUT\r\n"));
    cServer("* OK [CLOSED] Previous mailbox closed.\r\n");
    cServer("* 1 EXISTS\r\n"
            + okSelectedB);
    cEmpty();
    cServer("* BYE closing now\r\n");
}

/** @short Make sure that incremental upload of message metadata bits works, too */
void ImapModelSelectedMailboxUpdatesTest::testFetchMsgMetadataPerPartes()
{
    initialMessages(1);
    cServer("* 1 FETCH (FLAGS ())\r\n");
    justKeepTask();
    cEmpty();

    auto msg1 = msgListA.model()->index(0, 0, msgListA);
    QVERIFY(msg1.isValid());

    QSignalSpy insertionSpy(model, SIGNAL(rowsInserted(QModelIndex,int,int)));

    QCOMPARE(model->rowCount(msg1), 0);
    cClient(t.mk("UID FETCH 1 (" FETCH_METADATA_ITEMS ")\r\n"));
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    cServer("* 1 FETCH (INTERNALDATE \"15-Jan-2013 12:17:06 +0000\")\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    cServer("* 1 FETCH (ENVELOPE (NIL \"pwn\" NIL NIL NIL NIL NIL NIL NIL NIL))\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    cServer("* 1 FETCH (BODYSTRUCTURE (" + bsPlaintext + "))\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    QCOMPARE(insertionSpy.size(), 1);
    cServer("* 1 FETCH (RFC822.SIZE 666)\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), true);
    cServer(t.last("OK fetched\r\n"));
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), true);
    cServer("* 1 FETCH (BODYSTRUCTURE (" + bsPlaintext + "))\r\n");
    cEmpty();
    QCOMPARE(insertionSpy.size(), 1);
    justKeepTask();
}

class MonitoringCache : public Imap::Mailbox::MemoryCache {
public:
    virtual void setMessageMetadata(const QString &mailbox, const uint uid, const MessageDataBundle &metadata) override
    {
        msgMetadataLog.emplace_back(uid);
        MemoryCache::setMessageMetadata(mailbox, uid, metadata);
    }
    std::vector<uint> msgMetadataLog;
};

/** @short Do we survive duplicate unsolicited BODYSTRUCTURE responses? */
void ImapModelSelectedMailboxUpdatesTest::testFetchMsgDuplicateBodystructure()
{
    auto cacheLog = std::make_shared<MonitoringCache>();
    model->setCache(cacheLog);
    initialMessages(1);
    cServer("* 1 FETCH (FLAGS ())\r\n");
    justKeepTask();
    cEmpty();

    auto msg1 = msgListA.model()->index(0, 0, msgListA);
    QVERIFY(msg1.isValid());

    QCOMPARE(model->rowCount(msg1), 0);
    cClient(t.mk("UID FETCH 1 (" FETCH_METADATA_ITEMS ")\r\n"));
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    QCOMPARE(cacheLog->msgMetadataLog.size(), size_t(0));
    cServer("* 1 FETCH (BODYSTRUCTURE (" + bsPlaintext + "))\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    QCOMPARE(model->rowCount(msg1), 1);
    QCOMPARE(cacheLog->msgMetadataLog.size(), size_t(0));
    // Now send the duplicate data. We shouldn't assert.
    cServer("* 1 FETCH (BODYSTRUCTURE (" + bsPlaintext + "))\r\n");
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    QCOMPARE(model->rowCount(msg1), 1);
    QCOMPARE(cacheLog->msgMetadataLog.size(), size_t(0));
    cServer(t.last("OK fetched\r\n"));
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), false);
    QCOMPARE(model->rowCount(msg1), 1);
    cServer("* 1 FETCH (RFC822.SIZE 1234 INTERNALDATE \"15-Jan-2013 12:17:06 +0000\" "
            "ENVELOPE (NIL \"pwn\" NIL NIL NIL NIL NIL NIL NIL NIL))\r\n");
    QCOMPARE(cacheLog->msgMetadataLog.size(), size_t(1));
    QCOMPARE(msg1.data(Imap::Mailbox::RoleIsFetched).toBool(), true);
    cServer("* 1 FETCH (BODYSTRUCTURE (" + bsPlaintext + "))\r\n");
    QCOMPARE(cacheLog->msgMetadataLog.size(), size_t(1));
    cEmpty();
    justKeepTask();

}

QTEST_GUILESS_MAIN( ImapModelSelectedMailboxUpdatesTest )
