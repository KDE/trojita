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
#include "MsgListView.h"

#include <QAction>
#include <QApplication>
#include <QDesktopWidget>
#include <QDrag>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QSignalMapper>
#include <QTimer>
#include "MsgItemDelegate.h"
#include "Imap/Model/MsgListModel.h"
#include "Imap/Model/PrettyMsgListModel.h"
#include "Imap/Model/ThreadingMsgListModel.h"

namespace Gui
{

MsgListView::MsgListView(QWidget *parent, Imap::Mailbox::FavoriteTagsModel *m_favoriteTagsModel):
    QTreeView(parent), m_autoActivateAfterKeyNavigation(true), m_autoResizeSections(true)
{
    connect(header(), &QHeaderView::geometriesChanged, this, &MsgListView::slotFixSize);
    connect(this, &QTreeView::expanded, this, &MsgListView::slotExpandWholeSubtree);
    connect(header(), &QHeaderView::sectionCountChanged, this, &MsgListView::slotUpdateHeaderActions);
    header()->setContextMenuPolicy(Qt::ActionsContextMenu);
    headerFieldsMapper = new QSignalMapper(this);
    connect(headerFieldsMapper, static_cast<void (QSignalMapper::*)(int)>(&QSignalMapper::mapped), this, &MsgListView::slotHeaderSectionVisibilityToggled);

    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(ExtendedSelection);
    setDragEnabled(true);
    setRootIsDecorated(false);
    // Some subthreads might be huuuuuuuuuuge, so prevent indenting them too heavily
    setIndentation(15);

    setItemDelegate(new MsgItemDelegate(this, m_favoriteTagsModel));

    setSortingEnabled(true);
    // By default, we don't do any sorting
    header()->setSortIndicator(-1, Qt::AscendingOrder);

    m_naviActivationTimer = new QTimer(this);
    m_naviActivationTimer->setSingleShot(true);
    connect(m_naviActivationTimer, &QTimer::timeout, this, &MsgListView::slotCurrentActivated);
}

// left might collapse a thread, question is whether ending there (on closing the thread) should be
// taken as mail loading request (i don't think so, but it's sth. that needs to be figured over time)
// NOTICE: reasonably Triggers should be a (non strict) subset of Blockers (user changed his mind)

// the list of key events which pot. lead to loading a new message.
static QList<int> gs_naviActivationTriggers = QList<int>() << Qt::Key_Up << Qt::Key_Down << Qt::Key_Right << Qt::Key_Left
                                                           << Qt::Key_PageUp << Qt::Key_PageDown
                                                           << Qt::Key_Home << Qt::Key_End;
// the list of key events which cancel naviActivationTrigger induced action.
static QList<int> gs_naviActivationBlockers = QList<int>() << Qt::Key_Up << Qt::Key_Down << Qt::Key_Left
                                                           << Qt::Key_PageUp << Qt::Key_PageDown
                                                           << Qt::Key_Home << Qt::Key_End;


void MsgListView::keyPressEvent(QKeyEvent *ke)
{
    if (gs_naviActivationBlockers.contains(ke->key()))
        m_naviActivationTimer->stop();
    QTreeView::keyPressEvent(ke);
}

void MsgListView::keyReleaseEvent(QKeyEvent *ke)
{
    if (ke->modifiers() == Qt::NoModifier && gs_naviActivationTriggers.contains(ke->key()))
        m_naviActivationTimer->start(150); // few ms for the user to re-orientate. 150ms is not much
    QTreeView::keyReleaseEvent(ke);
}

bool MsgListView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride
            && !gs_naviActivationBlockers.contains(static_cast<QKeyEvent*>(event)->key())
            && m_naviActivationTimer->isActive()) {
        // Make sure that the delayed timer is broken ASAP when the key looks like something which might possibly be a shortcut
        m_naviActivationTimer->stop();
        slotCurrentActivated();
    }
    return QTreeView::event(event);
}

void MsgListView::slotCurrentActivated()
{
    if (currentIndex().isValid() && m_autoActivateAfterKeyNavigation) {
        // The "current index" is the one with that funny dot which only triggers the read/unread status toggle.
        // If we don't do anything, subsequent pressing of key_up or key_down will move the cursor up/down one row
        // while preserving the column which will lead to toggling the read/unread state of *that* message.
        // That's unexpected; the key shall just move the cursor and change the current message.
        emit activated(currentIndex().sibling(currentIndex().row(), Imap::Mailbox::MsgListModel::SUBJECT));
    }
}

int MsgListView::sizeHintForColumn(int column) const
{
    QFont boldFont = font();
    boldFont.setBold(true);
    QFontMetrics metric(boldFont);
    switch (column) {
    case Imap::Mailbox::MsgListModel::SEEN:
        return 0;
    case Imap::Mailbox::MsgListModel::FLAGGED:
    case Imap::Mailbox::MsgListModel::ATTACHMENT:
        return style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, nullptr);
    case Imap::Mailbox::MsgListModel::SUBJECT:
        return metric.size(Qt::TextSingleLine, QStringLiteral("Blesmrt Trojita Foo Bar Random Text")).width();
    case Imap::Mailbox::MsgListModel::FROM:
    case Imap::Mailbox::MsgListModel::TO:
    case Imap::Mailbox::MsgListModel::CC:
    case Imap::Mailbox::MsgListModel::BCC:
        return metric.size(Qt::TextSingleLine, QStringLiteral("Blesmrt Trojita")).width();
    case Imap::Mailbox::MsgListModel::DATE:
    case Imap::Mailbox::MsgListModel::RECEIVED_DATE:
        return metric.size(Qt::TextSingleLine,
                           //: Translators: use a text which is returned for e-mails older than one day but newer than one week
                           //: (see UiUtils::Formatting::prettyDate() for the string formats); the idea here
                           //: is to have a text which is "wide enough" in a typical UI font.
                           //: The English version uses "Mon" because of the M letter; you should use something similar.
                           tr("Mon 10")).width();
    case Imap::Mailbox::MsgListModel::SIZE:
        return metric.size(Qt::TextSingleLine, tr("88.8 kB")).width();
    default:
        return QTreeView::sizeHintForColumn(column);
    }
}

/** @short Reimplemented to show custom pixmap during drag&drop

  Qt's model-view classes don't provide any means of interfering with the
  QDrag's pixmap so we just rip off QAbstractItemView::startDrag and provide
  our own QPixmap.
*/
void MsgListView::startDrag(Qt::DropActions supportedActions)
{
    // indexes for column 0, i.e. subject
    QModelIndexList baseIndexes;

    Q_FOREACH(const QModelIndex &index, selectedTree()) {
        if (model()->flags(index) & Qt::ItemIsDragEnabled) {
            Q_ASSERT(index.column() == Imap::Mailbox::MsgListModel::SUBJECT);
            baseIndexes << index;
        }
    }

    if (!baseIndexes.isEmpty()) {
        QMimeData *data = model()->mimeData(baseIndexes);
        if (!data)
            return;

        // use screen width and itemDelegate()->sizeHint() to determine size of the pixmap
        int screenWidth = QApplication::desktop()->screenGeometry(this).width();
        int maxWidth = qMax(400, screenWidth / 4);
        QSize size(maxWidth, 0);

        // Show a "+ X more items" text after so many entries
        const int maxItems = 20;

        QStyleOptionViewItem opt;
        opt.initFrom(this);
        opt.rect.setWidth(maxWidth);
        opt.rect.setHeight(itemDelegate()->sizeHint(opt, baseIndexes.at(0)).height());
        size.setHeight(qMin(maxItems + 1, baseIndexes.size()) * opt.rect.height());
        // State_Selected provides for nice background of the items
        opt.state |= QStyle::State_Selected;

        // paint list of selected items using itemDelegate() to be consistent with style
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);

        for (int i = 0; i < baseIndexes.size(); ++i) {
            opt.rect.moveTop(i * opt.rect.height());
            if (i == maxItems) {
                p.fillRect(opt.rect, palette().color(QPalette::Disabled, QPalette::Highlight));
                p.setBrush(palette().color(QPalette::Disabled, QPalette::HighlightedText));
                p.drawText(opt.rect, Qt::AlignRight, tr("+ %n additional item(s)", 0, baseIndexes.size() - maxItems));
                break;
            }
            itemDelegate()->paint(&p, opt, baseIndexes.at(i));
        }

        QDrag *drag = new QDrag(this);
        drag->setPixmap(pixmap);
        drag->setMimeData(data);
        drag->setHotSpot(QPoint(0, 0));

        Qt::DropAction dropAction = Qt::IgnoreAction;
        if (defaultDropAction() != Qt::IgnoreAction && (supportedActions & defaultDropAction()))
            dropAction = defaultDropAction();
        else if (supportedActions & Qt::CopyAction && dragDropMode() != QAbstractItemView::InternalMove)
            dropAction = Qt::CopyAction;
        if (drag->exec(supportedActions, dropAction) == Qt::MoveAction) {
            // QAbstractItemView::startDrag calls d->clearOrRemove() here
            if (!dragDropOverwriteMode()) {
                Q_FOREACH(const QModelIndex &index, baseIndexes) {
                    model()->removeRow(index.row(), index.parent());
                }
            } else {
                // we can't remove the rows so reset the items (i.e. the view is like a table)
                QModelIndexList list = selectedIndexes();
                for (int i = 0; i < list.size(); ++i) {
                    QModelIndex index = list.at(i);
                    QMap<int, QVariant> roles = model()->itemData(index);
                    for (QMap<int, QVariant>::Iterator it = roles.begin(); it != roles.end(); ++it)
                        it.value() = QVariant();
                    model()->setItemData(index, roles);
                }
            }
        }
    }
}

QModelIndexList MsgListView::selectedTree() const
{
    QModelIndexList indexes;
    QModelIndexList selected = selectedIndexes();
    const int originalItems = selected.length(); // only check collapsed/expanded status on original selection
    for (int i = 0; i < selected.length(); ++i) {
        const QModelIndex item = selected[i];
        if (item.column() != 0 || !item.data(Imap::Mailbox::RoleMessageUid).isValid())
            continue;
        indexes << item;
        // Now see if this is a collapsed thread and include all the collapsed items as needed
        // Also note that this is recursive - each child found is run through this same item loop for validity/child checks as well
        if (i >= originalItems || !isExpanded(item)) {
            for (int j = 0; j < item.model()->rowCount(item); ++j) {
                selected << item.model()->index(j, 0, item); // Make sure this is run through the main loop as well - don't add it directly
            }
        }
    }
    return indexes;
}

void MsgListView::slotFixSize()
{
    if (!m_autoResizeSections)
        return;

    if (header()->visualIndex(Imap::Mailbox::MsgListModel::SUBJECT) == -1) {
        // calling setResizeMode() would assert()
        return;
    }

    header()->setStretchLastSection(false);
    for (int i = 0; i < Imap::Mailbox::MsgListModel::COLUMN_COUNT; ++i) {
        QHeaderView::ResizeMode resizeMode = resizeModeForColumn(i);
        header()->setSectionResizeMode(i, resizeMode);
        setColumnWidth(i, sizeHintForColumn(i));
    }
}

QHeaderView::ResizeMode MsgListView::resizeModeForColumn(const int column) const
{
    switch (column) {
    case Imap::Mailbox::MsgListModel::SUBJECT:
        return QHeaderView::Stretch;
    case Imap::Mailbox::MsgListModel::SEEN:
    case Imap::Mailbox::MsgListModel::FLAGGED:
    case Imap::Mailbox::MsgListModel::ATTACHMENT:
        return QHeaderView::Fixed;
    default:
        return QHeaderView::Interactive;
    }
}

void MsgListView::slotExpandWholeSubtree(const QModelIndex &rootIndex)
{
    if (rootIndex.parent().isValid())
        return;

    QVector<QModelIndex> queue(1, rootIndex);
    for (int i = 0; i < queue.size(); ++i) {
        const QModelIndex currentIndex = queue[i];
        // Append all children to the queue...
        for (int j = 0; j < currentIndex.model()->rowCount(currentIndex); ++j)
            queue.append(currentIndex.model()->index(j, 0, currentIndex));
        // ...and expand the current index
        if (currentIndex.model()->hasChildren(currentIndex))
            expand(currentIndex);
    }
}

void MsgListView::slotUpdateHeaderActions()
{
    Q_ASSERT(header());
    // At first, remove all actions
    QList<QAction *> actions = header()->actions();
    Q_FOREACH(QAction *action, actions) {
        header()->removeAction(action);
        headerFieldsMapper->removeMappings(action);
        action->deleteLater();
    }
    actions.clear();
    // Now add them again
    for (int i = 0; i < header()->count(); ++i) {
        QString message = header()->model() ? header()->model()->headerData(i, Qt::Horizontal).toString() : QString::number(i);
        QAction *action = new QAction(message, this);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, headerFieldsMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));
        headerFieldsMapper->setMapping(action, i);
        header()->addAction(action);

        // Next, add some special handling of certain columns
        switch (i) {
        case Imap::Mailbox::MsgListModel::SEEN:
            // This column doesn't have a textual description
            action->setText(tr("Seen status"));
            break;
        case Imap::Mailbox::MsgListModel::FLAGGED:
            action->setText(tr("Flagged status"));
            break;
        case Imap::Mailbox::MsgListModel::ATTACHMENT:
            action->setText(tr("Attachment"));
            break;
        case Imap::Mailbox::MsgListModel::TO:
        case Imap::Mailbox::MsgListModel::CC:
        case Imap::Mailbox::MsgListModel::BCC:
        case Imap::Mailbox::MsgListModel::RECEIVED_DATE:
            // And these should be hidden by default
            action->toggle();
            break;
        default:
            break;
        }
    }

    // Make sure to kick the header again so that it shows reasonable sizing
    slotFixSize();
}

/** @short Handle columns added to MsgListModel and set their default properties
 *
 * When a new version of the underlying model got a new column, the old saved state of the GUI might only contain data for the old columns.
 * Therefore it is important to explicitly restore the default for new columns, if any.
 */
void MsgListView::slotHandleNewColumns(int oldCount, int newCount)
{
    for (int i = oldCount; i < newCount; ++i) {
        switch(i) {
        case Imap::Mailbox::MsgListModel::FLAGGED:
            header()->moveSection(i,0);
            break;
        case Imap::Mailbox::MsgListModel::ATTACHMENT:
            header()->moveSection(i,0);
            break;
        }

        setColumnWidth(i, sizeHintForColumn(i));
    }
}

void MsgListView::slotHeaderSectionVisibilityToggled(int section)
{
    QList<QAction *> actions = header()->actions();
    if (section >= actions.size() || section < 0)
        return;
    bool hide = ! actions[section]->isChecked();

    if (hide && header()->hiddenSectionCount() == header()->count() - 1) {
        // This would hide the very last section, which would hide the whole header view
        actions[section]->setChecked(true);
    } else {
        header()->setSectionHidden(section, hide);
    }
}

void MsgListView::updateActionsAfterRestoredState()
{
    m_autoResizeSections = false;
    QList<QAction *> actions = header()->actions();
    for (int i = 0; i < actions.size(); ++i) {
        actions[i]->setChecked(!header()->isSectionHidden(i));
    }
}

/** @short Overridden from QTreeView::setModel

The whole point is that we have to listen for sortingPreferenceChanged to update your header view when sorting is requested
but cannot be fulfilled.
*/
void MsgListView::setModel(QAbstractItemModel *model)
{
    if (this->model()) {
        if (Imap::Mailbox::PrettyMsgListModel *prettyModel = findPrettyMsgListModel(this->model())) {
            disconnect(prettyModel, &Imap::Mailbox::PrettyMsgListModel::sortingPreferenceChanged,
                       this, &MsgListView::slotHandleSortCriteriaChanged);
            disconnect(qobject_cast<Imap::Mailbox::ThreadingMsgListModel*>(prettyModel->sourceModel())->sourceModel(),
                    &QAbstractItemModel::rowsAboutToBeRemoved,
                    this, &MsgListView::slotMsgListModelRowsAboutToBeRemoved);
        }
    }
    QTreeView::setModel(model);
    if (Imap::Mailbox::PrettyMsgListModel *prettyModel = findPrettyMsgListModel(model)) {
        connect(prettyModel, &Imap::Mailbox::PrettyMsgListModel::sortingPreferenceChanged,
                this, &MsgListView::slotHandleSortCriteriaChanged);
        connect(qobject_cast<Imap::Mailbox::ThreadingMsgListModel*>(prettyModel->sourceModel())->sourceModel(),
                &QAbstractItemModel::rowsAboutToBeRemoved,
                this, &MsgListView::slotMsgListModelRowsAboutToBeRemoved);
    }
}

/** @short Get ThreadingMsgListModel index and call the next handler */
void MsgListView::slotMsgListModelRowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    Q_ASSERT(!parent.isValid());

    auto threadingModel = qobject_cast<Imap::Mailbox::ThreadingMsgListModel*>(findPrettyMsgListModel(model())->sourceModel());
    for (int i = start; i <= end; ++i) {
        QModelIndex index = threadingModel->sourceModel()->index(i, 0, parent);
        Q_ASSERT(index.isValid());
        QModelIndex translated = threadingModel->mapFromSource(index);

        if (translated.isValid())
            slotThreadingMsgListModelRowAboutToBeRemoved(translated);
    }
}

/** @short Keep the cursor in place for better keyboard usability

In the worst case this is an O(log n). Such a worst case is when messages are removed in descending
order starting from last one of the view. But in practice, there are many cases when it performs
well better.

A performance-wise approach could be to hook signal layoutAboutToBeChanged, but the underlying model
has removed the rows by then and it makes everything complicated.
*/
void MsgListView::slotThreadingMsgListModelRowAboutToBeRemoved(const QModelIndex &index)
{
    Imap::Mailbox::PrettyMsgListModel *prettyModel = findPrettyMsgListModel(model());
    Q_ASSERT(!index.isValid() || index.model() == qobject_cast<Imap::Mailbox::ThreadingMsgListModel*>(prettyModel->sourceModel()));
    QModelIndex current = currentIndex();
    if (current.isValid() && prettyModel->mapFromSource(index) == current) {
        setCurrentIndexToNextValid(current);
    }
}

/** @short Try to move the cursor to next message

Used when the current message disappearing.
*/
void MsgListView::setCurrentIndexToNextValid(const QModelIndex &current)
{
    Q_ASSERT(current.isValid());
    Imap::Mailbox::PrettyMsgListModel *prettyModel = findPrettyMsgListModel(model());
    Q_ASSERT(current.model() == prettyModel);
    for (bool forward : {true,false}) {
        QModelIndex walker = forward ? indexBelow(current) : indexAbove(current);
        while (walker.isValid()) {
            // Queued for pruning..?
            if (prettyModel->data(walker, Imap::Mailbox::RoleMessageUid).isValid()) {
                // Do not activate, just keep the cursor in place.
                selectionModel()->setCurrentIndex(walker, QItemSelectionModel::NoUpdate);
                // It has won. For now.
                return;
            }
            walker = forward ? indexBelow(walker) : indexAbove(walker);
        }
    }
}

void MsgListView::slotHandleSortCriteriaChanged(int column, Qt::SortOrder order)
{
    // The if-clause is needed to prevent infinite recursion
    if (header()->sortIndicatorSection() != column || header()->sortIndicatorOrder() != order) {
        header()->setSortIndicator(column, order);
    }
}

/** @short Walk the hierarchy of proxy models up until we stop at the PrettyMsgListModel or the first non-proxy model */
Imap::Mailbox::PrettyMsgListModel *MsgListView::findPrettyMsgListModel(QAbstractItemModel *model)
{
    while (QAbstractProxyModel *proxy = qobject_cast<QAbstractProxyModel*>(model)) {
        Imap::Mailbox::PrettyMsgListModel *prettyModel = qobject_cast<Imap::Mailbox::PrettyMsgListModel*>(proxy);
        if (prettyModel)
            return prettyModel;
        else
            model = proxy->sourceModel();
    }
    return 0;
}

void MsgListView::setAutoActivateAfterKeyNavigation(bool enabled)
{
    m_autoActivateAfterKeyNavigation = enabled;
}

}


