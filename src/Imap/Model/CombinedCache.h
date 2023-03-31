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

#ifndef IMAP_MODEL_COMBINEDCACHE_H
#define IMAP_MODEL_COMBINEDCACHE_H

#include <memory>
#include "Cache.h"

namespace Imap
{

namespace Mailbox
{

class SQLCache;
class DiskPartCache;


/** @short A hybrid cache, using both SQLite and on-disk format

This cache servers as a thin wrapper around the SQLCache. It uses
the SQL facilities for most of the actual caching, but changes to
a file-based cache when items are bigger than a certain threshold.

In future, this should be extended with an in-memory cache (but
only after the MemoryCache rework) which should only speed-up certain
operations. This will likely be implemented when we will switch from
storing the actual data in the various TreeItem* instances.
*/
class CombinedCache : public AbstractCache
{
public:
    /** @short Constructor

      Create new instance, using the @arg name as the name for the database connection.
      Store all data into the @arg cacheDir directory. Actual opening of the DB connection
      is deferred till a call to the load() method.
    */
    CombinedCache(const QString &name, const QString &cacheDir);

    virtual ~CombinedCache();

    QList<MailboxMetadata> childMailboxes(const QString &mailbox) const override;
    bool childMailboxesFresh(const QString &mailbox) const override;
    void setChildMailboxes(const QString &mailbox, const QList<MailboxMetadata> &data) override;

    SyncState mailboxSyncState(const QString &mailbox) const override;
    void setMailboxSyncState(const QString &mailbox, const SyncState &state) override;

    void setUidMapping(const QString &mailbox, const Imap::Uids &seqToUid) override;
    void clearUidMapping(const QString &mailbox) override;
    Imap::Uids uidMapping(const QString &mailbox) const override;

    void clearAllMessages(const QString &mailbox) override;
    void clearMessage(const QString mailbox, const uint uid) override;

    MessageDataBundle messageMetadata(const QString &mailbox, const uint uid) const override;
    void setMessageMetadata(const QString &mailbox, const uint uid, const MessageDataBundle &metadata) override;

    QStringList msgFlags(const QString &mailbox, const uint uid) const override;
    void setMsgFlags(const QString &mailbox, const uint uid, const QStringList &flags) override;

    QByteArray messagePart(const QString &mailbox, const uint uid, const QByteArray &partId) const override;
    void setMsgPart(const QString &mailbox, const uint uid, const QByteArray &partId, const QByteArray &data) override;
    void forgetMessagePart(const QString &mailbox, const uint uid, const QByteArray &partId) override;

    QVector<Imap::Responses::ThreadingNode> messageThreading(const QString &mailbox) override;
    void setMessageThreading(const QString &mailbox, const QVector<Imap::Responses::ThreadingNode> &threading) override;

    void setRenewalThreshold(const int days) override;

    /** @short Open a connection to the cache */
    bool open();

private:
    /** @short Name of the DB connection */
    QString name;
    /** @short Directory to serve as a cache root */
    QString cacheDir;
    /** @short The SQL-based cache */
    std::unique_ptr<SQLCache> sqlCache;
    /** @short Cache for bigger message parts */
    std::unique_ptr<DiskPartCache> diskPartCache;
};

}

}

#endif /* IMAP_MODEL_COMBINEDCACHE_H */
