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
#include <QFontMetrics>
#include <QIcon>
#include <QMimeData>
#include "Imap/Model/DragAndDrop.h"
#include "Imap/Model/MailboxTree.h"
#include "Imap/Model/MailboxModel.h"
#include "Imap/Model/MsgListModel.h"
#include "UiUtils/IconLoader.h"


namespace Imap
{
namespace Mailbox
{

MsgListModel::MsgListModel(QObject *parent, Model *model): QAbstractProxyModel(parent), msgListPtr(0), waitingForMessages(false)
{
    setSourceModel(model);

    // FIXME: will need to be expanded when Model supports more signals...
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &MsgListModel::resetMe);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, &QAbstractItemModel::layoutAboutToBeChanged);
    connect(model, &QAbstractItemModel::layoutChanged, this, &QAbstractItemModel::layoutChanged);
    connect(model, &QAbstractItemModel::dataChanged, this, &MsgListModel::handleDataChanged);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, &MsgListModel::handleRowsAboutToBeRemoved);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &MsgListModel::handleRowsRemoved);
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, &MsgListModel::handleRowsAboutToBeInserted);
    connect(model, &QAbstractItemModel::rowsInserted, this, &MsgListModel::handleRowsInserted);

    connect(this, &QAbstractItemModel::layoutChanged, this, &MsgListModel::indexStateChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &MsgListModel::indexStateChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &MsgListModel::indexStateChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &MsgListModel::indexStateChanged);
}

QHash<int, QByteArray> MsgListModel::roleNames() const
{
    static QHash<int, QByteArray> roleNames;
    if (roleNames.isEmpty()) {
        roleNames[RoleIsFetched] = "isFetched";
        roleNames[RoleIsUnavailable] = "isUnavailable";
        roleNames[RoleMessageUid] = "messageUid";
        roleNames[RoleMessageIsMarkedDeleted] = "isMarkedDeleted";
        roleNames[RoleMessageIsMarkedRead] = "isMarkedRead";
        roleNames[RoleMessageIsMarkedForwarded] = "isMarkedForwarded";
        roleNames[RoleMessageIsMarkedReplied] = "isMarkedReplied";
        roleNames[RoleMessageIsMarkedRecent] = "isMarkedRecent";
        roleNames[RoleMessageIsMarkedFlagged] = "isMarkedFlagged";
        roleNames[RoleMessageIsMarkedJunk] = "isMarkedJunk";
        roleNames[RoleMessageIsMarkedNotJunk] = "isMarkedNotJunk";
        roleNames[RoleMessageDate] = "date";
        roleNames[RoleMessageInternalDate] = "receivedDate";
        roleNames[RoleMessageFrom] = "from";
        roleNames[RoleMessageTo] = "to";
        roleNames[RoleMessageCc] = "cc";
        roleNames[RoleMessageBcc] = "bcc";
        roleNames[RoleMessageSender] = "sender";
        roleNames[RoleMessageReplyTo] = "replyTo";
        roleNames[RoleMessageInReplyTo] = "inReplyTo";
        roleNames[RoleMessageMessageId] = "messageId";
        roleNames[RoleMessageSubject] = "subject";
        roleNames[RoleMessageFlags] = "flags";
        roleNames[RoleMessageSize] = "size";
        roleNames[RoleMessageFuzzyDate] = "fuzzyDate";
        roleNames[RoleMessageHasAttachments] = "hasAttachments";
    }
    return roleNames;
}

void MsgListModel::handleDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    QModelIndex first = mapFromSource(topLeft);
    QModelIndex second = mapFromSource(bottomRight);

    if (! first.isValid() || ! second.isValid()) {
        return;
    }

    second = createIndex(second.row(), COLUMN_COUNT - 1, Model::realTreeItem(second));

    if (first.parent() == second.parent()) {
        emit dataChanged(first, second);
    } else {
        Q_ASSERT(false);
        return;
    }
}

void MsgListModel::checkPersistentIndex() const
{
    if (!msgList.isValid()) {
        if (msgListPtr) {
            emit const_cast<MsgListModel*>(this)->indexStateChanged();
        }
        msgListPtr = 0;
    }
}

QModelIndex MsgListModel::index(int row, int column, const QModelIndex &parent) const
{
    checkPersistentIndex();

    if (! msgListPtr)
        return QModelIndex();

    if (parent.isValid())
        return QModelIndex();

    if (column < 0 || column >= COLUMN_COUNT)
        return QModelIndex();

    if (row >= msgListPtr->m_children.size() || row < 0)
        return QModelIndex();

    return createIndex(row, column, msgListPtr->m_children[row]);
}

QModelIndex MsgListModel::parent(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return QModelIndex();
}

bool MsgListModel::hasChildren(const QModelIndex &parent) const
{
    return ! parent.isValid();
}

int MsgListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    checkPersistentIndex();
    if (! msgListPtr)
        return 0;

    return msgListPtr->rowCount(dynamic_cast<Model *>(sourceModel()));
}

int MsgListModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    if (parent.column() != 0 && parent.column() != -1)
        return 0;
    return COLUMN_COUNT;
}

QModelIndex MsgListModel::mapToSource(const QModelIndex &proxyIndex) const
{
    checkPersistentIndex();
    if (!msgListPtr || !proxyIndex.isValid())
        return QModelIndex();

    if (proxyIndex.parent().isValid())
        return QModelIndex();

    Model *model = dynamic_cast<Model *>(sourceModel());
    Q_ASSERT(model);
    return model->createIndex(proxyIndex.row(), 0, msgListPtr->m_children[proxyIndex.row()]);
}

QModelIndex MsgListModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    checkPersistentIndex();
    if (! msgListPtr)
        return QModelIndex();

    if (sourceIndex.model() != sourceModel())
        return QModelIndex();
    if (dynamic_cast<TreeItemMessage *>(Model::realTreeItem(sourceIndex))) {
        return index(sourceIndex.row(), 0, QModelIndex());
    } else {
        return QModelIndex();
    }
}

QVariant MsgListModel::data(const QModelIndex &proxyIndex, int role) const
{
    checkPersistentIndex();
    if (! msgListPtr)
        return QVariant();

    if (! proxyIndex.isValid() || proxyIndex.model() != this)
        return QVariant();

    TreeItemMessage *message = dynamic_cast<TreeItemMessage *>(Model::realTreeItem(proxyIndex));
    Q_ASSERT(message);

    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (proxyIndex.column()) {
        case SUBJECT:
            return QAbstractProxyModel::data(proxyIndex, Qt::DisplayRole);
        case FROM:
            return QLatin1String("[from]");
        case TO:
            return QLatin1String("[to]");
        case CC:
            return QLatin1String("[cc]");
        case BCC:
            return QLatin1String("[bcc]");
        case DATE:
            return message->envelope(static_cast<Model *>(sourceModel())).date;
        case RECEIVED_DATE:
            return message->internalDate(static_cast<Model *>(sourceModel()));
        case SIZE:
            return QVariant::fromValue(message->size(static_cast<Model *>(sourceModel())));
        default:
            return QVariant();
        }

    case RoleIsFetched:
    case RoleIsUnavailable:
    case RoleMessageUid:
    case RoleMessageIsMarkedDeleted:
    case RoleMessageIsMarkedRead:
    case RoleMessageIsMarkedForwarded:
    case RoleMessageIsMarkedReplied:
    case RoleMessageIsMarkedRecent:
    case RoleMessageIsMarkedFlagged:
    case RoleMessageIsMarkedJunk:
    case RoleMessageIsMarkedNotJunk:
    case RoleMessageDate:
    case RoleMessageFrom:
    case RoleMessageTo:
    case RoleMessageCc:
    case RoleMessageBcc:
    case RoleMessageSender:
    case RoleMessageReplyTo:
    case RoleMessageInReplyTo:
    case RoleMessageMessageId:
    case RoleMessageSubject:
    case RoleMessageFlags:
    case RoleMessageSize:
    case RoleMessageHeaderReferences:
    case RoleMessageHeaderListPost:
    case RoleMessageHeaderListPostNo:
    case RoleMessageHasAttachments:
        return dynamic_cast<TreeItemMessage *>(Model::realTreeItem(
                proxyIndex))->data(static_cast<Model *>(sourceModel()), role);
    default:
        return QAbstractProxyModel::data(createIndex(proxyIndex.row(), 0, proxyIndex.internalPointer()), role);
    }
}

QVariant MsgListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DecorationRole && section == ATTACHMENT)
        return UiUtils::loadIcon(QStringLiteral("mail-attachment"));

    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractItemModel::headerData(section, orientation, role);

    switch (section) {
    case SUBJECT:
        return tr("Subject");
    case FROM:
        return tr("From");
    case TO:
        return tr("To");
    case CC:
        return tr("Cc");
    case BCC:
        return tr("Bcc");
    case DATE:
        return tr("Date");
    case RECEIVED_DATE:
        return tr("Received");
    case SIZE:
        return tr("Size");
    default:
        return QVariant();
    }
}

Qt::ItemFlags MsgListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    TreeItemMessage *message = dynamic_cast<TreeItemMessage *>(Model::realTreeItem(index));
    Q_ASSERT(message);

    if (!message->uid())
        return Qt::NoItemFlags;

    return Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
}

Qt::DropActions MsgListModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions MsgListModel::supportedDropActions() const
{
    // Right now the GUI code doesn't allow dropping items into mailbox by dragging them
    // to a listing of mailbox' content, but if we just return Qt::IgnoreAction form here,
    // stuff breaks on Qt prior to 5.4.0 because on old Qt, the proxy models call
    // supportedDropActions() unless supportedDragActions is reimplemented on each level.
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0)
    return Qt::CopyAction | Qt::MoveAction;
#else
    return Qt::IgnoreAction;
#endif
}

QStringList MsgListModel::mimeTypes() const
{
    return QStringList() << MimeTypes::xTrojitaMessageList;
}

QMimeData *MsgListModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return 0;

    QMimeData *res = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_4_6);

    TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(Model::realTreeItem(
                                   indexes.front())->parent()->parent());
    Q_ASSERT(mailbox);
    stream << mailbox->mailbox() << mailbox->syncState.uidValidity();

    QList<uint> uids;
    for (QModelIndexList::const_iterator it = indexes.begin(); it != indexes.end(); ++it) {
        TreeItemMessage *message = dynamic_cast<TreeItemMessage *>(Model::realTreeItem(*it));
        Q_ASSERT(message);
        Q_ASSERT(message->parent()->parent() == mailbox);
        Q_ASSERT(message->uid() > 0);
        uids << message->uid();
    }
    uids = uids.toSet().values();
    stream << uids;
    res->setData(MimeTypes::xTrojitaMessageList, encodedData);
    return res;
}

void MsgListModel::resetMe()
{
    beginResetModel();
    msgListPtr = 0;
    msgList = QModelIndex();
    endResetModel();
}

void MsgListModel::handleRowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    if (! parent.isValid()) {
        // Top-level items are tricky :(. As a quick hack, let's just die.
        resetMe();
        return;
    }

    checkPersistentIndex();
    if (! msgListPtr)
        return;

    TreeItem *item = Model::realTreeItem(parent);
    TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(item);
    TreeItemMsgList *newList = dynamic_cast<TreeItemMsgList *>(item);

    if (parent.isValid()) {
        Q_ASSERT(parent.model() == sourceModel());
    } else {
        // a top-level mailbox might have been deleted, so we gotta setup proper pointer
        mailbox = static_cast<Model *>(sourceModel())->m_mailboxes;
        Q_ASSERT(mailbox);
    }

    if (newList) {
        if (newList == msgListPtr) {
            beginRemoveRows(mapFromSource(parent), start, end);
            for (int i = start; i <= end; ++i)
                emit messageRemoved(msgListPtr->m_children[i]);
        }
    } else if (mailbox) {
        Q_ASSERT(start > 0);
        // if we're below it, we're gonna die
        for (int i = start; i <= end; ++i) {
            const Model *model = 0;
            QModelIndex translatedParent;
            Model::realTreeItem(parent, &model, &translatedParent);
            // FIXME: this assumes that no rows were removed by the proxy model
            TreeItemMailbox *m = dynamic_cast<TreeItemMailbox *>(static_cast<TreeItem *>(model->index(i, 0, translatedParent).internalPointer()));
            Q_ASSERT(m);
            TreeItem *up = msgListPtr->parent();
            while (up) {
                if (m == up) {
                    resetMe();
                    return;
                }
                up = up->parent();
            }
        }
    }
}

void MsgListModel::handleRowsRemoved(const QModelIndex &parent, int start, int end)
{
    Q_UNUSED(start);
    Q_UNUSED(end);

    checkPersistentIndex();
    if (! msgListPtr)
        return;

    if (! parent.isValid()) {
        // already handled by resetMe() in handleRowsAboutToBeRemoved()
        return;
    }

    if (dynamic_cast<TreeItemMsgList *>(Model::realTreeItem(parent)) == msgListPtr)
        endRemoveRows();
}

void MsgListModel::handleRowsAboutToBeInserted(const QModelIndex &parent, int start, int end)
{
    checkPersistentIndex();
    if (! parent.isValid())
        return;

    TreeItemMsgList *newList = dynamic_cast<TreeItemMsgList *>(Model::realTreeItem(parent));
    if (msgListPtr && msgListPtr == newList) {
        beginInsertRows(mapFromSource(parent), start, end);
    }
}

void MsgListModel::handleRowsInserted(const QModelIndex &parent, int start, int end)
{
    checkPersistentIndex();
    if (! parent.isValid())
        return;

    Q_UNUSED(start);
    Q_UNUSED(end);
    TreeItemMsgList *newList = dynamic_cast<TreeItemMsgList *>(Model::realTreeItem(parent));
    if (msgListPtr && msgListPtr == newList) {
        endInsertRows();
    }

    if (waitingForMessages) {
        waitingForMessages = false;
        emit messagesAvailable();
    }
}

void MsgListModel::setMailbox(const QModelIndex &index)
{
    if (!index.isValid() || !index.data(Imap::Mailbox::RoleMailboxIsSelectable).toBool())
        return;

    waitingForMessages = true;

    const Model *model = 0;
    TreeItemMailbox *mbox = dynamic_cast<TreeItemMailbox *>(Model::realTreeItem(index, &model));
    Q_ASSERT(mbox);
    TreeItemMsgList *newList = dynamic_cast<TreeItemMsgList *>(
                                   mbox->child(0, const_cast<Model *>(model)));
    Q_ASSERT(newList);
    checkPersistentIndex();
    if (newList != msgListPtr) {
        msgListPtr = newList;
        msgList = msgListPtr->toIndex(const_cast<Model*>(model));
        msgListPtr->resetWasUnreadState();
        beginResetModel();
        endResetModel();
        emit mailboxChanged(index);
    }

    // We want to tell the Model that it should consider starting the IDLE command.
    // This shall happen regardless on what this model's idea about a "current mailbox" is because the IMAP connection might have
    // switched to another mailbox behind the scenes.
    const_cast<Model *>(model)->switchToMailbox(index);
}

/** @short Change mailbox to the one specified by its name */
void MsgListModel::setMailbox(const QString &mailboxName)
{
    Model *model = dynamic_cast<Model*>(sourceModel());
    Q_ASSERT(model);
    TreeItemMailbox *mailboxPtr = model->findMailboxByName(mailboxName);
    if (mailboxPtr)
        setMailbox(mailboxPtr->toIndex(model));
}

QModelIndex MsgListModel::currentMailbox() const
{
    checkPersistentIndex();
    return msgList.parent();
}

bool MsgListModel::itemsValid() const
{
    return currentMailbox().isValid();
}

}
}
