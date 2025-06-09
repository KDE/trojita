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

#include "SQLCache.h"
#include <QIODevice>
#include <QSqlError>
#include <QSqlRecord>
#include <QTimer>
#include "Common/SqlTransactionAutoAborter.h"

//#define CACHE_DEBUG

namespace
{
static int streamVersion = QDataStream::Qt_4_6;
}

namespace Imap
{
namespace Mailbox
{

// This is an arbitrary cutoff point against which we're storing the offset in the database.
// It cannot change without bumping the DB version (but probably shouldn't change at all, at least
// I cannot imagine a reason why that would be necessary).
//
// Yes, we have just invented our own date storage format. That sucks.
// Now, I'm afraid I actually do have an excuse for it. First of all, we're only interested in the data with a rather
// coarse granularity, so we don't really need a type like QDateTime, QDate is enough. However, there's no explicit
// mapping for it in the QtSql's manual page, so we either have to rely on implicit conversions (which don't appear to
// be documented anywhere) or code it ourselves (which is what we do).
//
// Now, an integer looks like a reasonably efficient implementation, so we just have to come up with some cutoff time.
// At first, I wanted to use something "well known" like 1970-01-01, but on a second thought, that looks rather confusing
// -- it's the start of the posix epoch, but the time difference we're storing is in days, not seconds, so it is really
// different from the usual Posix' semantics. That leads me to a conclusion that choosing a "recent" value (at the time this
// was written) is a reasonable approach after all.
//
// But even though the whole code manipulating these data is just a few lines in size, I'm sure there must be a bug lurking
// somewehere -- because there is one each time one codes date-specific functions once again. Please let me know if you're
// hit by it; I apologise in advance.
QDate SQLCache::accessingThresholdDate = QDate(2012, 11, 1);

SQLCache::SQLCache()
    : inTransaction(false)
    , m_updateAccessIfOlder(0)
{
}

void SQLCache::init()
{
#ifdef CACHE_DEBUG
    qDebug() << "SQLCache::init()";
#endif
    delayedCommit.reset(new QTimer());
    delayedCommit->setInterval(10000);
    delayedCommit->setObjectName(QStringLiteral("delayedCommit"));
    QObject::connect(delayedCommit.get(), &QTimer::timeout,
                     delayedCommit.get(), [this](){ this->timeToCommit(); });
    tooMuchTimeWithoutCommit.reset(new QTimer());
    tooMuchTimeWithoutCommit->setInterval(60000);
    tooMuchTimeWithoutCommit->setObjectName(QStringLiteral("tooMuchTimeWithoutCommit"));
    QObject::connect(tooMuchTimeWithoutCommit.get(), &QTimer::timeout,
                     tooMuchTimeWithoutCommit.get(), [this](){ this->timeToCommit(); });
}

SQLCache::~SQLCache()
{
    timeToCommit();
    db.close();
}

#define TROJITA_SQL_CACHE_CREATE_THREADING \
if ( ! q.exec( QLatin1String("CREATE TABLE msg_threading ( " \
                             "mailbox STRING NOT NULL PRIMARY KEY, " \
                             "threading BINARY" \
                             " )") ) ) { \
    emitError( QObject::tr("Can't create table msg_threading"), q ); \
    return false; \
}

#define TROJITA_SQL_CACHE_CREATE_SYNC_STATE \
if ( ! q.exec( QLatin1String("CREATE TABLE mailbox_sync_state ( " \
                             "mailbox STRING NOT NULL PRIMARY KEY, " \
                             "sync_state BINARY " \
                             " )") ) ) { \
    emitError( QObject::tr("Can't create table mailbox_sync_state"), q ); \
    return false; \
}

#define TROJITA_SQL_CACHE_CREATE_MSG_METADATA \
    if (! q.exec(QLatin1String("CREATE TABLE msg_metadata (" \
                               "mailbox STRING NOT NULL, " \
                               "uid INT NOT NULL, " \
                               "data BINARY, " \
                               "lastAccessDate INT, " \
                               "PRIMARY KEY (mailbox, uid)" \
                               ")"))) { \
        emitError(QObject::tr("Can't create table msg_metadata"), q); \
        return false; \
    }

bool SQLCache::open(const QString &name, const QString &fileName)
{
#ifdef CACHE_DEBUG
    qDebug() << "SQLCache::open()";
#endif
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    m_cleanup.name = name;
    db.setDatabaseName(fileName);

    bool ok = db.open();
    if (! ok) {
        emitError(QObject::tr("Can't open database"), db);
        return false;
    }

    Common::SqlTransactionAutoAborter txn(&db);

    QSqlRecord trojitaNames = db.record(QStringLiteral("trojita"));
    if (! trojitaNames.contains(QStringLiteral("version"))) {
        if (! createTables())
            return false;
    }

    QSqlQuery q(QString(), db);

    if (! q.exec(QStringLiteral("SELECT version FROM trojita"))) {
        emitError(QObject::tr("Failed to verify version"), q);
        return false;
    }

    if (! q.first()) {
        // we could probably relax this...
        emitError(QObject::tr("Can't determine version info"), q);
        return false;
    }

    uint version = q.value(0).toUInt();
    if (version == 1) {
        TROJITA_SQL_CACHE_CREATE_THREADING
        version = 2;
        if (! q.exec(QStringLiteral("UPDATE trojita SET version = 2;"))) {
            emitError(QObject::tr("Failed to update cache DB scheme from v1 to v2"), q);
            return false;
        }
    }

    if (version == 2 || version == 3) {
        // There's no difference in table layout between v3 and v4, but the mailbox_sync_state has changed due to the new
        // HIGHESTMODSEQ in Mailbox::SyncState, which is why we throw away the old data unconditionally
        if (!q.exec(QStringLiteral("DROP TABLE mailbox_sync_state;"))) {
            emitError(QObject::tr("Failed to drop old table mailbox_sync_state"));
            return false;
        }
        TROJITA_SQL_CACHE_CREATE_SYNC_STATE;
        version = 4;
        if (! q.exec(QStringLiteral("UPDATE trojita SET version = 4;"))) {
            emitError(QObject::tr("Failed to update cache DB scheme from v2/v3 to v4"), q);
            return false;
        }
    }

    if (version == 4 || version == 5 || version == 6) {
        // No difference in table structure between v4 and v5, but the data stored in msg_metadata is different; the UID
        // got removed and INTERNALDATE was added.
        // V6 has added the References and List-Post headers (i.e. a change in the structure of the blobs stored in the DB,
        // but transparent on the SQL level), and also changed the DB structure by adding a date specifying how recently
        // a given message was accessed (which was needed for cache lifetime management).
        // V7 changed the sizes to quint64 (from uint), so the message metadata again changed
        if (!q.exec(QStringLiteral("DROP TABLE msg_metadata;"))) {
            emitError(QObject::tr("Failed to drop old table msg_metadata"));
            return false;
        }
        TROJITA_SQL_CACHE_CREATE_MSG_METADATA;
        version = 7;
        if (! q.exec(QStringLiteral("UPDATE trojita SET version = 7;"))) {
            emitError(QObject::tr("Failed to update cache DB scheme from v4/v5/v6 to v7"), q);
            return false;
        }
    }

    if (version != 7) {
        emitError(QObject::tr("Unknown version of sqlite cache"));
        return false;
    }

    txn.commit();

    if (! prepareQueries()) {
        return false;
    }
    init();
#ifdef CACHE_DEBUG
    qDebug() << "SQLCache::open() succeeded";
#endif
    return true;
}

bool SQLCache::createTables()
{
    QSqlQuery q(QString(), db);

    if (! q.exec(QStringLiteral("CREATE TABLE trojita ( version STRING NOT NULL )"))) {
        emitError(QObject::tr("Failed to prepare table structures"), q);
        return false;
    }
    if (! q.exec(QStringLiteral("INSERT INTO trojita ( version ) VALUES ( 6 )"))) {
        emitError(QObject::tr("Can't store version info"), q);
        return false;
    }
    if (! q.exec(QStringLiteral(
                     "CREATE TABLE child_mailboxes ( "
                     "mailbox STRING NOT NULL PRIMARY KEY, "
                     "parent STRING NOT NULL, "
                     "separator STRING, "
                     "flags BINARY"
                     ")"
                 ))) {
        emitError(QObject::tr("Can't create table child_mailboxes"));
        return false;
    }

    if (! q.exec(QStringLiteral("CREATE TABLE uid_mapping ( "
                               "mailbox STRING NOT NULL PRIMARY KEY, "
                               "mapping BINARY"
                               " )"))) {
        emitError(QObject::tr("Can't create table uid_mapping"), q);
        return false;
    }

    TROJITA_SQL_CACHE_CREATE_MSG_METADATA;

    if (! q.exec(QStringLiteral("CREATE TABLE flags ("
                               "mailbox STRING NOT NULL, "
                               "uid INT NOT NULL, "
                               "flags BINARY, "
                               "PRIMARY KEY (mailbox, uid)"
                               ")"))) {
        emitError(QObject::tr("Can't create table flags"), q);
    }

    if (! q.exec(QStringLiteral("CREATE TABLE parts ("
                               "mailbox STRING NOT NULL, "
                               "uid INT NOT NULL, "
                               "part_id BINARY, "
                               "data BINARY, "
                               "PRIMARY KEY (mailbox, uid, part_id)"
                               ")"))) {
        emitError(QObject::tr("Can't create table parts"), q);
    }

    TROJITA_SQL_CACHE_CREATE_THREADING;
    TROJITA_SQL_CACHE_CREATE_SYNC_STATE;

    return true;
}

bool SQLCache::prepareQueries()
{
    queryChildMailboxes = QSqlQuery(db);
    if (! queryChildMailboxes.prepare(QStringLiteral("SELECT mailbox, separator, flags FROM child_mailboxes WHERE parent = ?"))) {
        emitError(QObject::tr("Failed to prepare queryChildMailboxes"), queryChildMailboxes);
        return false;
    }

    queryChildMailboxesFresh = QSqlQuery(db);
    if (! queryChildMailboxesFresh.prepare(QStringLiteral("SELECT mailbox FROM child_mailboxes WHERE parent = ? LIMIT 1"))) {
        emitError(QObject::tr("Failed to prepare queryChildMailboxesFresh"), queryChildMailboxesFresh);
        return false;
    }

    queryRemoveChildMailboxes = QSqlQuery(db);
    if (!queryRemoveChildMailboxes.prepare(QStringLiteral("DELETE FROM child_mailboxes WHERE parent = ?"))) {
        emitError(QObject::tr("Failed to prepare queryRemoveChildMailboxes"), queryRemoveChildMailboxes);
        return false;
    }

    querySetChildMailboxes = QSqlQuery(db);
    if (! querySetChildMailboxes.prepare(QStringLiteral("INSERT OR REPLACE INTO child_mailboxes ( mailbox, parent, separator, flags ) VALUES (?, ?, ?, ?)"))) {
        emitError(QObject::tr("Failed to prepare querySetChildMailboxes"), querySetChildMailboxes);
        return false;
    }

    queryMailboxSyncState = QSqlQuery(db);
    if (! queryMailboxSyncState.prepare(QStringLiteral("SELECT sync_state FROM mailbox_sync_state WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryMailboxSyncState"), queryMailboxSyncState);
        return false;
    }

    querySetMailboxSyncState = QSqlQuery(db);
    if (! querySetMailboxSyncState.prepare(QStringLiteral("INSERT OR REPLACE INTO mailbox_sync_state "
                                           "( mailbox, sync_state ) "
                                           "VALUES ( ?, ? )"))) {
        emitError(QObject::tr("Failed to prepare querySetMailboxSyncState"), querySetMailboxSyncState);
        return false;
    }

    queryUidMapping = QSqlQuery(db);
    if (! queryUidMapping.prepare(QStringLiteral("SELECT mapping FROM uid_mapping WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryUidMapping"), queryUidMapping);
        return false;
    }

    querySetUidMapping = QSqlQuery(db);
    if (! querySetUidMapping.prepare(QStringLiteral("INSERT OR REPLACE INTO uid_mapping (mailbox, mapping) VALUES  ( ?, ? )"))) {
        emitError(QObject::tr("Failed to prepare querySetUidMapping"), querySetUidMapping);
        return false;
    }

    queryClearUidMapping = QSqlQuery(db);
    if (! queryClearUidMapping.prepare(QStringLiteral("DELETE FROM uid_mapping WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearUidMapping"), queryClearUidMapping);
        return false;
    }

    queryMessageMetadata = QSqlQuery(db);
    if (! queryMessageMetadata.prepare(QStringLiteral("SELECT data, lastAccessDate FROM msg_metadata WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryMessageMetadata"), queryMessageMetadata);
        return false;
    }

    queryAccessMessageMetadata = QSqlQuery(db);
    if (!queryAccessMessageMetadata.prepare(QStringLiteral("UPDATE msg_metadata SET lastAccessDate = ? WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryAccssMessageMetadata"), queryAccessMessageMetadata);
        return false;
    }

    querySetMessageMetadata = QSqlQuery(db);
    if (! querySetMessageMetadata.prepare(QStringLiteral("INSERT OR REPLACE INTO msg_metadata ( mailbox, uid, data, lastAccessDate ) VALUES ( ?, ?, ?, ? )"))) {
        emitError(QObject::tr("Failed to prepare querySetMessageMetadata"), querySetMessageMetadata);
        return false;
    }

    queryMessageFlags = QSqlQuery(db);
    if (! queryMessageFlags.prepare(QStringLiteral("SELECT flags FROM flags WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryMessageFlags"), queryMessageFlags);
        return false;
    }

    querySetMessageFlags = QSqlQuery(db);
    if (! querySetMessageFlags.prepare(QStringLiteral("INSERT OR REPLACE INTO flags ( mailbox, uid, flags ) VALUES ( ?, ?, ? )"))) {
        emitError(QObject::tr("Failed to prepare querySetMessageFlags"), querySetMessageFlags);
        return false;
    }

    queryClearAllMessages1 = QSqlQuery(db);
    if (! queryClearAllMessages1.prepare(QStringLiteral("DELETE FROM msg_metadata WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearAllMessages1"), queryClearAllMessages1);
        return false;
    }

    queryClearAllMessages2 = QSqlQuery(db);
    if (! queryClearAllMessages2.prepare(QStringLiteral("DELETE FROM flags WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearAllMessages2"), queryClearAllMessages2);
        return false;
    }

    queryClearAllMessages3 = QSqlQuery(db);
    if (! queryClearAllMessages3.prepare(QStringLiteral("DELETE FROM parts WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearAllMessages3"), queryClearAllMessages3);
        return false;
    }

    queryClearAllMessages4 = QSqlQuery(db);
    if (! queryClearAllMessages4.prepare(QStringLiteral("DELETE FROM msg_threading WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearAllMessages4"), queryClearAllMessages4);
        return false;
    }

    queryClearMessage1 = QSqlQuery(db);
    if (! queryClearMessage1.prepare(QStringLiteral("DELETE FROM msg_metadata WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearMessage1"), queryClearMessage1);
        return false;
    }

    queryClearMessage2 = QSqlQuery(db);
    if (! queryClearMessage2.prepare(QStringLiteral("DELETE FROM flags WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearMessage2"), queryClearMessage2);
        return false;
    }

    queryClearMessage3 = QSqlQuery(db);
    if (! queryClearMessage3.prepare(QStringLiteral("DELETE FROM parts WHERE mailbox = ? AND uid = ?"))) {
        emitError(QObject::tr("Failed to prepare queryClearMessage3"), queryClearMessage3);
        return false;
    }

    queryMessagePart = QSqlQuery(db);
    if (! queryMessagePart.prepare(QStringLiteral("SELECT data FROM parts WHERE mailbox = ? AND uid = ? AND part_id = ?"))) {
        emitError(QObject::tr("Failed to prepare queryMessagePart"), queryMessagePart);
        return false;
    }

    querySetMessagePart = QSqlQuery(db);
    if (! querySetMessagePart.prepare(QStringLiteral("INSERT OR REPLACE INTO parts ( mailbox, uid, part_id, data ) VALUES (?, ?, ?, ?)"))) {
        emitError(QObject::tr("Failed to prepare querySetMessagePart"), querySetMessagePart);
        return false;
    }

    queryForgetMessagePart = QSqlQuery(db);
    if (! queryForgetMessagePart.prepare(QStringLiteral("DELETE FROM parts WHERE mailbox = ? AND uid = ? AND part_id = ?"))) {
        emitError(QObject::tr("Failed to prepare queryForgetMessagePart"), queryForgetMessagePart);
        return false;
    }

    queryMessageThreading = QSqlQuery(db);
    if (! queryMessageThreading.prepare(QStringLiteral("SELECT threading FROM msg_threading WHERE mailbox = ?"))) {
        emitError(QObject::tr("Failed to prepare queryMessageThreading"), queryMessageThreading);
        return false;
    }

    querySetMessageThreading = QSqlQuery(db);
    if (! querySetMessageThreading.prepare(QStringLiteral("INSERT OR REPLACE INTO msg_threading (mailbox, threading) VALUES  ( ?, ? )"))) {
        emitError(QObject::tr("Failed to prepare querySetMessageThreading"), querySetMessageThreading);
        return false;
    }

#ifdef CACHE_DEBUG
    qDebug() << "SQLCache::_prepareQueries() succeeded";
#endif
    return true;
}

void SQLCache::emitError(const QString &message, const QSqlQuery &query) const
{
    emitError(QStringLiteral("SQLCache: Query Error: %1: %2").arg(message, query.lastError().text()));
}

void SQLCache::emitError(const QString &message, const QSqlDatabase &database) const
{
    emitError(QStringLiteral("SQLCache: DB Error: %1: %2").arg(message, database.lastError().text()));
}

void SQLCache::emitError(const QString &message) const
{
    qDebug() << message;
    m_errorHandler(message);
}

QList<MailboxMetadata> SQLCache::childMailboxes(const QString &mailbox) const
{
    QList<MailboxMetadata> res;
    queryChildMailboxes.bindValue(0, mailboxName(mailbox));
    if (! queryChildMailboxes.exec()) {
        emitError(QObject::tr("Query queryChildMailboxes failed"), queryChildMailboxes);
        return res;
    }
    while (queryChildMailboxes.next()) {
        MailboxMetadata item;
        item.mailbox = queryChildMailboxes.value(0).toString();
        item.separator = queryChildMailboxes.value(1).toString();
        QDataStream stream(queryChildMailboxes.value(2).toByteArray());
        stream.setVersion(streamVersion);
        stream >> item.flags;
        if (stream.status() != QDataStream::Ok) {
            emitError(QObject::tr("Corrupt data when reading child items for mailbox %1, line %2").arg(mailbox, item.mailbox));
            return QList<MailboxMetadata>();
        }
        res << item;
    }
    return res;
}

bool SQLCache::childMailboxesFresh(const QString &mailbox) const
{
    queryChildMailboxesFresh.bindValue(0, mailboxName(mailbox));
    if (! queryChildMailboxesFresh.exec()) {
        emitError(QObject::tr("Query queryChildMailboxesFresh failed"), queryChildMailboxesFresh);
        return false;
    }
    return queryChildMailboxesFresh.first();
}

void SQLCache::setChildMailboxes(const QString &mailbox, const QList<MailboxMetadata> &data)
{
#ifdef CACHE_DEBUG
    qDebug() << "Setting child mailboxes for" << mailbox;
#endif
    touchingDB();
    QVariantList mailboxFields, parentFields, separatorFields, flagsFelds;
    Q_FOREACH(const MailboxMetadata& item, data) {
        mailboxFields << item.mailbox;
        parentFields << mailboxName(mailbox);
        separatorFields << item.separator;
        QByteArray buf;
        QDataStream stream(&buf, QIODevice::ReadWrite);
        stream.setVersion(streamVersion);
        stream << item.flags;
        flagsFelds << buf;
    }
    queryRemoveChildMailboxes.bindValue(0, mailboxName(mailbox));
    if (!queryRemoveChildMailboxes.exec()) {
        emitError(QObject::tr("Query queryRemoveChildMailboxes failed"), queryRemoveChildMailboxes);
        return;
    }
    querySetChildMailboxes.bindValue(0, mailboxFields);
    querySetChildMailboxes.bindValue(1, parentFields);
    querySetChildMailboxes.bindValue(2, separatorFields);
    querySetChildMailboxes.bindValue(3, flagsFelds);
    if (! querySetChildMailboxes.execBatch()) {
        emitError(QObject::tr("Query querySetChildMailboxes failed"), querySetChildMailboxes);
        return;
    }
}

SyncState SQLCache::mailboxSyncState(const QString &mailbox) const
{
    SyncState res;
    queryMailboxSyncState.bindValue(0, mailboxName(mailbox));
    if (! queryMailboxSyncState.exec()) {
        emitError(QObject::tr("Query queryMailboxSyncState failed"), queryMailboxSyncState);
        return res;
    }
    if (queryMailboxSyncState.first()) {
        QDataStream stream(queryMailboxSyncState.value(0).toByteArray());
        stream.setVersion(streamVersion);
        stream >> res;
    }
    // "No data present" doesn't necessarily imply a problem -- it simply might not be there yet :)
    return res;
}

void SQLCache::setMailboxSyncState(const QString &mailbox, const SyncState &state)
{
#ifdef CACHE_DEBUG
    qDebug() << "Setting sync state for" << mailbox;
#endif
    touchingDB();
    querySetMailboxSyncState.bindValue(0, mailboxName(mailbox));
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::ReadWrite);
    stream.setVersion(streamVersion);
    stream << state;
    querySetMailboxSyncState.bindValue(1, buf);
    if (! querySetMailboxSyncState.exec()) {
        emitError(QObject::tr("Query querySetMailboxSyncState failed"), querySetMailboxSyncState);
        return;
    }
}

Imap::Uids SQLCache::uidMapping(const QString &mailbox) const
{
    Imap::Uids res;
    queryUidMapping.bindValue(0, mailboxName(mailbox));
    if (! queryUidMapping.exec()) {
        emitError(QObject::tr("Query queryUidMapping failed"), queryUidMapping);
        return res;
    }
    if (queryUidMapping.first()) {
        QDataStream stream(qUncompress(queryUidMapping.value(0).toByteArray()));
        stream.setVersion(streamVersion);
        stream >> res;
    }
    // "No data present" doesn't necessarily imply a problem -- it simply might not be there yet :)
    return res;
}

void SQLCache::setUidMapping(const QString &mailbox, const Imap::Uids &seqToUid)
{
#ifdef CACHE_DEBUG
    qDebug() << "Setting UID mapping for" << mailbox;
#endif
    touchingDB();
    querySetUidMapping.bindValue(0, mailboxName(mailbox));
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::ReadWrite);
    stream.setVersion(streamVersion);
    stream << seqToUid;
    querySetUidMapping.bindValue(1, qCompress(buf));
    if (! querySetUidMapping.exec()) {
        emitError(QObject::tr("Query querySetUidMapping failed"), querySetUidMapping);
    }
}

void SQLCache::clearUidMapping(const QString &mailbox)
{
#ifdef CACHE_DEBUG
    qDebug() << "Clearing UID mapping for" << mailbox;
#endif
    touchingDB();
    queryClearUidMapping.bindValue(0, mailboxName(mailbox));
    if (! queryClearUidMapping.exec()) {
        emitError(QObject::tr("Query queryClearUidMapping failed"), queryClearUidMapping);
    }
}

void SQLCache::clearAllMessages(const QString &mailbox)
{
#ifdef CACHE_DEBUG
    qDebug() << "Clearing all messages from" << mailbox;
#endif
    touchingDB();
    queryClearAllMessages1.bindValue(0, mailboxName(mailbox));
    queryClearAllMessages2.bindValue(0, mailboxName(mailbox));
    queryClearAllMessages3.bindValue(0, mailboxName(mailbox));
    queryClearAllMessages4.bindValue(0, mailboxName(mailbox));
    if (! queryClearAllMessages1.exec()) {
        emitError(QObject::tr("Query queryClearAllMessages1 failed"), queryClearAllMessages1);
    }
    if (! queryClearAllMessages2.exec()) {
        emitError(QObject::tr("Query queryClearAllMessages2 failed"), queryClearAllMessages2);
    }
    if (! queryClearAllMessages3.exec()) {
        emitError(QObject::tr("Query queryClearAllMessages3 failed"), queryClearAllMessages3);
    }
    if (! queryClearAllMessages4.exec()) {
        emitError(QObject::tr("Query queryClearAllMessages4 failed"), queryClearAllMessages4);
    }
    clearUidMapping(mailbox);
}

void SQLCache::clearMessage(const QString mailbox, uint uid)
{
#ifdef CACHE_DEBUG
    qDebug() << "Clearing message" << uid << "from" << mailbox;
#endif
    touchingDB();
    queryClearMessage1.bindValue(0, mailboxName(mailbox));
    queryClearMessage1.bindValue(1, uid);
    queryClearMessage2.bindValue(0, mailboxName(mailbox));
    queryClearMessage2.bindValue(1, uid);
    queryClearMessage3.bindValue(0, mailboxName(mailbox));
    queryClearMessage3.bindValue(1, uid);
    if (! queryClearMessage1.exec()) {
        emitError(QObject::tr("Query queryClearMessage1 failed"), queryClearMessage1);
    }
    if (! queryClearMessage2.exec()) {
        emitError(QObject::tr("Query queryClearMessage2 failed"), queryClearMessage2);
    }
    if (! queryClearMessage3.exec()) {
        emitError(QObject::tr("Query queryClearMessage3 failed"), queryClearMessage3);
    }
}

QStringList SQLCache::msgFlags(const QString &mailbox, const uint uid) const
{
    QStringList res;
    queryMessageFlags.bindValue(0, mailboxName(mailbox));
    queryMessageFlags.bindValue(1, uid);
    if (! queryMessageFlags.exec()) {
        emitError(QObject::tr("Query queryMessageFlags failed"), queryMessageFlags);
        return res;
    }
    if (queryMessageFlags.first()) {
        QDataStream stream(queryMessageFlags.value(0).toByteArray());
        stream.setVersion(streamVersion);
        stream >> res;
    }
    // "Not found" is not an error here
    return res;
}

void SQLCache::setMsgFlags(const QString &mailbox, const uint uid, const QStringList &flags)
{
#ifdef CACHE_DEBUG
    qDebug() << "Updating flags for" << mailbox << uid;
#endif
    touchingDB();
    querySetMessageFlags.bindValue(0, mailboxName(mailbox));
    querySetMessageFlags.bindValue(1, uid);
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::ReadWrite);
    stream.setVersion(streamVersion);
    stream << flags;
    querySetMessageFlags.bindValue(2, buf);
    if (! querySetMessageFlags.exec()) {
        emitError(QObject::tr("Query querySetMessageFlags failed"), querySetMessageFlags);
    }
}

AbstractCache::MessageDataBundle SQLCache::messageMetadata(const QString &mailbox, uint uid) const
{
    AbstractCache::MessageDataBundle res;
    queryMessageMetadata.bindValue(0, mailboxName(mailbox));
    queryMessageMetadata.bindValue(1, uid);
    if (! queryMessageMetadata.exec()) {
        emitError(QObject::tr("Query queryMessageMetadata failed"), queryMessageMetadata);
        return res;
    }
    if (queryMessageMetadata.first()) {
        res.uid = uid;
        QDataStream stream(qUncompress(queryMessageMetadata.value(0).toByteArray()));
        stream.setVersion(streamVersion);
        stream >> res.envelope >> res.internalDate >> res.size >> res.serializedBodyStructure >> res.hdrReferences
                  >> res.hdrListPost >> res.hdrListPostNo;

        if (m_updateAccessIfOlder) {
            int lastAccessTimestamp = queryMessageMetadata.value(1).toInt();
            int currentDiff = accessingThresholdDate.daysTo(QDate::currentDate());
            if (lastAccessTimestamp < currentDiff - m_updateAccessIfOlder) {
                queryAccessMessageMetadata.bindValue(0, currentDiff);
                queryAccessMessageMetadata.bindValue(1, mailboxName(mailbox));
                queryAccessMessageMetadata.bindValue(2, uid);
                if (!queryAccessMessageMetadata.exec()) {
                    emitError(QObject::tr("Query queryAccessMessageMetadata failed"), queryAccessMessageMetadata);
                }
            }
        }
    }
    // "Not found" is not an error here
    return res;
}

void SQLCache::setMessageMetadata(const QString &mailbox, const uint uid, const MessageDataBundle &metadata)
{
#ifdef CACHE_DEBUG
    qDebug() << "Setting message metadata for" << uid << mailbox;
#endif
    touchingDB();
    // Order of values: mailbox, uid, data
    querySetMessageMetadata.bindValue(0, mailboxName(mailbox));
    querySetMessageMetadata.bindValue(1, uid);
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::ReadWrite);
    stream.setVersion(streamVersion);
    stream << metadata.envelope << metadata.internalDate << metadata.size << metadata.serializedBodyStructure
           << metadata.hdrReferences << metadata.hdrListPost << metadata.hdrListPostNo;
    querySetMessageMetadata.bindValue(2, qCompress(buf));
    querySetMessageMetadata.bindValue(3, accessingThresholdDate.daysTo(QDate::currentDate()));
    if (! querySetMessageMetadata.exec()) {
        emitError(QObject::tr("Query querySetMessageMetadata failed"), querySetMessageMetadata);
    }
}

QByteArray SQLCache::messagePart(const QString &mailbox, const uint uid, const QByteArray &partId) const
{
    QByteArray res;
    queryMessagePart.bindValue(0, mailboxName(mailbox));
    queryMessagePart.bindValue(1, uid);
    queryMessagePart.bindValue(2, partId);
    if (! queryMessagePart.exec()) {
        emitError(QObject::tr("Query queryMessagePart failed"), queryMessagePart);
        return res;
    }
    if (queryMessagePart.first()) {
        res = qUncompress(queryMessagePart.value(0).toByteArray());
        queryMessagePart.finish();
    }
    return res;
}

void SQLCache::setMsgPart(const QString &mailbox, const uint uid, const QByteArray &partId, const QByteArray &data)
{
#ifdef CACHE_DEBUG
    qDebug() << "Saving message part" << partId << uid << mailbox;
#endif
    touchingDB();
    querySetMessagePart.bindValue(0, mailboxName(mailbox));
    querySetMessagePart.bindValue(1, uid);
    querySetMessagePart.bindValue(2, partId);
    querySetMessagePart.bindValue(3, qCompress(data));
    if (! querySetMessagePart.exec()) {
        emitError(QObject::tr("Query querySetMessagePart failed"), querySetMessagePart);
    }
}

void SQLCache::forgetMessagePart(const QString &mailbox, const uint uid, const QByteArray &partId)
{
#ifdef CACHE_DEBUG
    qDebug() << "Forgetting message part" << partId << uid << mailbox;
#endif
    touchingDB();
    queryForgetMessagePart.bindValue(0, mailboxName(mailbox));
    queryForgetMessagePart.bindValue(1, uid);
    queryForgetMessagePart.bindValue(2, partId);
    if (! queryForgetMessagePart.exec()) {
        emitError(QObject::tr("Query queryForgetMessagePart failed"), queryForgetMessagePart);
    }
}

QVector<Imap::Responses::ThreadingNode> SQLCache::messageThreading(const QString &mailbox)
{
    QVector<Imap::Responses::ThreadingNode> res;
    queryMessageThreading.bindValue(0, mailboxName(mailbox));
    if (! queryMessageThreading.exec()) {
        emitError(QObject::tr("Query queryMessageThreading failed"), queryMessageThreading);
        return res;
    }
    if (queryMessageThreading.first()) {
        QDataStream stream(qUncompress(queryMessageThreading.value(0).toByteArray()));
        stream.setVersion(streamVersion);
        stream >> res;
    }
    return res;
}

void SQLCache::setMessageThreading(const QString &mailbox, const QVector<Imap::Responses::ThreadingNode> &threading)
{
#ifdef CACHE_DEBUG
    qDebug() << "Setting threading for" << mailbox;
#endif
    touchingDB();
    querySetMessageThreading.bindValue(0, mailboxName(mailbox));
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::ReadWrite);
    stream.setVersion(streamVersion);
    stream << threading;
    querySetMessageThreading.bindValue(1, qCompress(buf));
    if (! querySetMessageThreading.exec()) {
        emitError(QObject::tr("Query querySetMessageThreading failed"), querySetMessageThreading);
    }

}

void SQLCache::touchingDB()
{
    delayedCommit->start();
    if (! inTransaction) {
#ifdef CACHE_DEBUG
        qDebug() << "Starting transaction";
#endif
        inTransaction = true;
        db.transaction();
        tooMuchTimeWithoutCommit->start();
    }
}

void SQLCache::timeToCommit()
{
    if (inTransaction) {
#ifdef CACHE_DEBUG
        qDebug() << "Commit";
#endif
        inTransaction = false;
        db.commit();
    }
}

void SQLCache::setRenewalThreshold(const int days)
{
    m_updateAccessIfOlder = days;
}

/** @short Return a proper represenation of the mailbox name to be used in the SQL queries

A null QString is represented as NIL, which makes our cache unhappy.
*/
QString SQLCache::mailboxName(const QString &mailbox)
{
    return mailbox.isEmpty() ? QLatin1String("") : mailbox;
}

DbConnectionCleanup::~DbConnectionCleanup()
{
    QSqlDatabase::removeDatabase(name);
}

}
}
