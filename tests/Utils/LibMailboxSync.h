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

#ifndef TEST_IMAP_LIBMAILBOXSYNC
#define TEST_IMAP_LIBMAILBOXSYNC

#include <QtTest>
#include "Common/MetaTypes.h"
#include "Imap/Model/Model.h"
#include "Streams/SocketFactory.h"
#include "TagGenerator.h"

/** @short What items are expected to be requested when asking for "message metadata" */
#define FETCH_METADATA_ITEMS "ENVELOPE INTERNALDATE BODYSTRUCTURE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (References List-Post)]"

class QSignalSpy;

namespace Imap {
namespace Mailbox {
class ThreadingMsgListModel;
bool operator==(const SyncState &a, const SyncState &b);
}
}

class LibMailboxSync;

/** @short Helper which checks that an error is thrown */
class ExpectSingleErrorHere
{
public:
    ExpectSingleErrorHere(LibMailboxSync *syncer);
    ~ExpectSingleErrorHere();
private:
    LibMailboxSync *m_syncer;
};

class LibMailboxSync : public QObject
{
    Q_OBJECT
public:
    LibMailboxSync();
    virtual ~LibMailboxSync();

    static QModelIndex findIndexByPosition(const QAbstractItemModel *model, const QString &where);

    static void setModelNetworkPolicy(Imap::Mailbox::Model *model, const Imap::Mailbox::NetworkPolicy policy);

    static QByteArray asLiteral(const QByteArray &data);

protected:
    void setupLogging();

protected slots:
    virtual void init();
    virtual void cleanup();
    virtual void cleanupTestCase();
    virtual void initTestCase();

    void modelSignalsError(const QString &message);
    void modelLogged(uint parserId, const Common::LogMessage &message);

protected:
    virtual void helperSyncAWithMessagesEmptyState();
    virtual void helperSyncAFullSync();
    virtual void helperSyncANoMessagesCompleteState();
    virtual void helperSyncBNoMessages();
    virtual void helperSyncAWithMessagesNoArrivals();
    virtual void helperSyncFlags();
    virtual void helperSyncASomeNew( int number );
    virtual void helperQresyncAInitial(Imap::Mailbox::SyncState &syncState);

    virtual void helperFakeExistsUidValidityUidNext();
    virtual void helperFakeUidSearch( uint start=0 );
    virtual void helperVerifyUidMapA();
    virtual void helperCheckCache(bool ignoreUidNext=false);

    virtual void helperOneFlagUpdate( const QModelIndex &message );
    QByteArray helperCreateTrivialEnvelope(const uint seq, const uint uid, const QString &subject);
    QByteArray helperCreateTrivialEnvelope(const uint seq, const uint uid, const QString &subject, const QString &from, const QString &bodyStructure);

    void helperInitialListing();
    void initialMessages(const uint exists);
    void justKeepTask();
    void checkNoTasks();

    Imap::Mailbox::Model* model;
    Imap::Mailbox::MsgListModel *msgListModel;
    Imap::Mailbox::ThreadingMsgListModel *threadingModel;
    Streams::FakeSocketFactory* factory;
    Imap::Mailbox::TestingTaskFactory* taskFactoryUnsafe;
    QMap<QString,QStringList> fakeListChildMailboxesMap;
    QSignalSpy* errorSpy;
    QSignalSpy* netErrorSpy;

    QPersistentModelIndex idxA, idxB, idxC, msgListA, msgListB, msgListC;
    TagGenerator t;
    uint existsA, uidValidityA, uidNextA;
    Imap::Uids uidMapA;
    bool m_verbose;
    bool m_expectsError;
    bool m_fakeListCommand;
    bool m_fakeOpenTask;
    bool m_startTlsRequired;
    Imap::ConnectionState m_initialConnectionState;

    friend class ExpectSingleErrorHere;
};

class NetDataRegexp
{
public:
    static NetDataRegexp fromPattern(const QByteArray &pattern);
    static NetDataRegexp fromData(const QByteArray &data);
    QByteArray raw;
    QByteArray pattern;
    bool isPattern;
private:
    NetDataRegexp(const QByteArray &raw, const QByteArray &pattern, const bool isPattern);
};

bool operator==(const NetDataRegexp &a, const NetDataRegexp &b);

namespace QTest {

/** @short Helper for pretty-printing in QCOMPARE */
template<>
char *toString(const NetDataRegexp &x);

}

#define SOCK static_cast<Streams::FakeSocket*>( factory->lastSocket() )

#define cServer(data) \
{ \
    SOCK->fakeReading(data); \
    for (int i=0; i<4; ++i) \
        QCoreApplication::processEvents(); \
}

#define TROJITA_CLIENT_LOOP \
    for (int i=0; i<5; ++i) \
        QCoreApplication::processEvents();

#define cClient(data) \
{ \
    TROJITA_CLIENT_LOOP \
    QCOMPARE(QString::fromUtf8(SOCK->writtenStuff()), QString::fromUtf8(data));\
}

// Be careful with this; although we now use QRegularExpression, the underlying code is still based on QRegExp's limitation to single-line patterns.
#define cClientRegExp(pattern) \
{ \
    TROJITA_CLIENT_LOOP \
    QCOMPARE(NetDataRegexp::fromData(SOCK->writtenStuff()), NetDataRegexp::fromPattern(pattern));\
}

#define cEmpty() \
{ \
    TROJITA_CLIENT_LOOP \
    QCOMPARE(QString::fromUtf8(SOCK->writtenStuff()), QString()); \
}

#define requestAndCheckSubject(OFFSET, SUBJECT) \
{ \
    QModelIndex index = msgListA.model()->index(OFFSET, 0, msgListA); \
    Q_ASSERT(index.isValid()); \
    uint uid = index.data(Imap::Mailbox::RoleMessageUid).toUInt(); \
    Q_ASSERT(uid); \
    QCOMPARE(index.data(Imap::Mailbox::RoleMessageSubject).toString(), QString()); \
    cClient(t.mk(QString::fromUtf8("UID FETCH %1 (" FETCH_METADATA_ITEMS ")\r\n").arg(QString::number(uid)).toUtf8())); \
    cServer(helperCreateTrivialEnvelope(OFFSET + 1, uid, SUBJECT) + t.last("OK fetched\r\n")); \
    QCOMPARE(index.data(Imap::Mailbox::RoleMessageSubject).toString(), QString::fromUtf8(SUBJECT)); \
}

#define checkCachedSubject(OFFSET, SUBJECT) \
{ \
    QModelIndex index = msgListA.model()->index(OFFSET, 0, msgListA); \
    Q_ASSERT(index.isValid()); \
    Q_ASSERT(index.data(Imap::Mailbox::RoleMessageUid).toUInt()); \
    QCOMPARE(index.data(Imap::Mailbox::RoleMessageSubject).toString(), QString::fromUtf8(SUBJECT)); \
}

namespace QTest {

/** @short Debug data dumper for QList<uint> */
template<>
char *toString(const QList<uint> &list);

template<>
char *toString(const Imap::Mailbox::SyncState &syncState);

/** @short Debug data dumper for the MessageDataBundle */
template<>
char *toString(const Imap::Mailbox::AbstractCache::MessageDataBundle &bundle);

}

#endif
