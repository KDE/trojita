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
#include <QDebug>
#include <QStringList>
#include <QTimer>

#include "MsgPartNetworkReply.h"
#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/MailboxTree.h"
#include "Imap/Model/Model.h"
#include "Imap/Network/MsgPartNetAccessManager.h"

namespace Imap
{

namespace Network
{

MsgPartNetworkReply::MsgPartNetworkReply(MsgPartNetAccessManager *parent, const QPersistentModelIndex &part):
    QNetworkReply(parent), part(part)
{
    QUrl url;
    url.setScheme(QStringLiteral("trojita-imap"));
    url.setHost(QStringLiteral("msg"));
    url.setPath(part.data(Imap::Mailbox::RolePartPathToPart).toString());
    setUrl(url);

    setOpenMode(QIODevice::ReadOnly | QIODevice::Unbuffered);
    Q_ASSERT(part.isValid());

    connect(part.model(), &QAbstractItemModel::dataChanged, this, &MsgPartNetworkReply::slotModelDataChanged);

    // We have to ask for contents before we check whether it's already fetched
    part.data(Imap::Mailbox::RolePartData);

    // The part data might be already unavailable or already fetched
    QTimer::singleShot(0, this, SLOT(slotMyDataChanged()));

    QByteArray* bufferPtr = part.data(Imap::Mailbox::RolePartBufferPtr).value<QByteArray*>();
    Q_ASSERT(bufferPtr);
    buffer.setBuffer(bufferPtr);
    buffer.open(QIODevice::ReadOnly);
}

/** @short Check to see whether the data which concern this object has arrived already */
void MsgPartNetworkReply::slotModelDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    Q_UNUSED(bottomRight);
    // FIXME: use bottomRight as well!
    if (topLeft.model() != part.model()) {
        return;
    }
    if (topLeft == part) {
        slotMyDataChanged();
    }
}

/** @short Data for the current message part are available now */
void MsgPartNetworkReply::slotMyDataChanged()
{
    if (part.data(Mailbox::RoleIsUnavailable).toBool()) {
        if (!part.data(Mailbox::RoleIsNetworkOffline).isValid()) {
            setError(UnknownProxyError, tr("Cannot access data"));
        } else {
            if (part.data(Mailbox::RoleIsNetworkOffline).toBool()) {
                setError(TimeoutError, tr("Uncached data not available when offline"));
            } else {
                setError(ContentNotFoundError, tr("Error downloading data"));
            }
        }
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
        return;
    }

    if (!part.data(Mailbox::RoleIsFetched).toBool())
        return;

    MsgPartNetAccessManager *netAccess = qobject_cast<MsgPartNetAccessManager*>(manager());
    Q_ASSERT(netAccess);
    QString mimeType = netAccess->translateToSupportedMimeType(part.data(Mailbox::RolePartMimeType).toString());
    QString charset = part.data(Mailbox::RolePartCharset).toString();
    if (mimeType.startsWith(QLatin1String("text/"))) {
        setHeader(QNetworkRequest::ContentTypeHeader,
                  charset.isEmpty() ? mimeType : QStringLiteral("%1; charset=%2").arg(mimeType, charset)
                 );
    } else {
        setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    }
    setFinished(true);
    emit readyRead();
    emit finished();
}

/** @short QIODevice compatibility */
void MsgPartNetworkReply::abort()
{
    close();
}

/** @short QIODevice compatibility */
void MsgPartNetworkReply::close()
{
    disconnectBufferIfVanished();
    buffer.close();
}

/** @short QIODevice compatibility */
qint64 MsgPartNetworkReply::bytesAvailable() const
{
    disconnectBufferIfVanished();
    return buffer.bytesAvailable() + QNetworkReply::bytesAvailable();
}

/** @short QIODevice compatibility */
qint64 MsgPartNetworkReply::readData(char *data, qint64 maxSize)
{
    disconnectBufferIfVanished();
    return buffer.read(data, maxSize);
}


/** @short Cut the buffer connection in case the message got removed */
void MsgPartNetworkReply::disconnectBufferIfVanished() const
{
    if (!part.isValid()) {
        buffer.close();
        buffer.setBuffer(0);
    }
}

}
}
