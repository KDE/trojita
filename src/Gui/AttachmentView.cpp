/* Copyright (C) 2006 - 2013 Jan Kundrát <jkt@flaska.net>

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
#include "AttachmentView.h"
#include "IconLoader.h"
#include "MessageView.h" // so that the compiler knows it's a QObject
#include "Common/DeleteAfter.h"
#include "Common/Paths.h"
#include "Imap/Network/FileDownloadManager.h"
#include "Imap/Model/MailboxTree.h"
#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/Utils.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDrag>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QLabel>
#include <QStyle>
#include <QStyleOption>
#include <QTemporaryFile>
#include <QToolButton>
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#  include <QMimeDatabase>
#else
#  include "mimetypes-qt4/include/QMimeDatabase"
#endif

namespace Gui
{

AttachmentView::AttachmentView(QWidget *parent, Imap::Network::MsgPartNetAccessManager *manager,
                               const QModelIndex &partIndex, MessageView *messageView, QWidget *contentWidget):
    QFrame(parent), m_partIndex(partIndex), m_messageView(messageView), m_downloadAttachment(0),
    m_openAttachment(0), m_showHideAttachment(0), m_netAccess(manager), m_tmpFile(0),
    m_contentWidget(contentWidget)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFrameStyle(QFrame::NoFrame);

    QStyleOption opt;
    opt.initFrom(this); // not actually required by styles may assume the parameter and segfault otherwise

    const int padding = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &opt, this);
    setContentsMargins(padding, 0, padding, 0);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->setContentsMargins(0,0,0,0);

    // should be PM_LayoutHorizontalSpacing, but is not implemented by many Qt4 styles -including oxygen- for other conflicts
    const int spacing = style()->pixelMetric(QStyle::PM_DefaultLayoutSpacing, &opt, this);
    layout->setSpacing(0);

    // Icon on the left
    m_icon = new QToolButton(this);
    m_icon->setAttribute(Qt::WA_NoMousePropagation, false); // inform us for DnD
    m_icon->setAutoRaise(true);
    m_icon->setIconSize(QSize(22,22));
    m_icon->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_menu = new QMenu(this);
    m_downloadAttachment = m_menu->addAction(loadIcon(QLatin1String("document-save-as")), tr("Download"));
    m_openAttachment = m_menu->addAction(tr("Open Directly"));
    connect(m_downloadAttachment, SIGNAL(triggered()), this, SLOT(slotDownloadAttachment()));
    connect(m_openAttachment, SIGNAL(triggered()), this, SLOT(slotOpenAttachment()));
    if (m_contentWidget) {
        m_showHideAttachment = m_menu->addAction(loadIcon(QLatin1String("view-preview")), tr("Show Preview"));
        m_showHideAttachment->setCheckable(true);
        m_showHideAttachment->setChecked(!m_contentWidget->isHidden());
        connect(m_showHideAttachment, SIGNAL(triggered(bool)), m_contentWidget, SLOT(setVisible(bool)));
    }

    m_icon->setDefaultAction(m_downloadAttachment);

    QString mimeDescription = partIndex.data(Imap::Mailbox::RolePartMimeType).toString();
    QString rawMime = mimeDescription;
    QMimeType mimeType = QMimeDatabase().mimeTypeForName(mimeDescription);
    if (mimeType.isValid() && !mimeType.isDefault()) {
        mimeDescription = mimeType.comment();
        QIcon icn = QIcon::fromTheme(mimeType.iconName(),
                                     QIcon::fromTheme(mimeType.genericIconName(), loadIcon(QLatin1String("mail-attachment")))
                                     );
        m_icon->setIcon(icn);
    } else {
        m_icon->setIcon(loadIcon(QLatin1String("mail-attachment")));
    }
    layout->addWidget(m_icon);

    // space between icon and label
    layout->addSpacing(spacing);

    QVBoxLayout *subLayout = new QVBoxLayout;
    subLayout->setContentsMargins(0,0,0,0);
    // The file name shall be mouse-selectable
    QLabel *lbl = new QLabel(this);
    lbl->setTextFormat(Qt::PlainText);
    lbl->setText(partIndex.data(Imap::Mailbox::RolePartFileName).toString());
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    subLayout->addWidget(lbl);
    // Some metainformation -- the MIME type and the file size
    lbl = new QLabel(tr("%2, %3").arg(mimeDescription,
                                      Imap::Mailbox::PrettySize::prettySize(partIndex.data(Imap::Mailbox::RolePartOctets).toUInt(),
                                                                            Imap::Mailbox::PrettySize::WITH_BYTES_SUFFIX)));
    if (rawMime != mimeDescription) {
        lbl->setToolTip(rawMime);
    }
    QFont f(lbl->font());
    f.setItalic(true);
    if (f.pointSize() > -1) // don't try the below on pixel fonts ever.
        f.setPointSizeF(f.pointSizeF() * 0.8);
    lbl->setFont(f);
    subLayout->addWidget(lbl);
    layout->addLayout(subLayout);

    // space between label and arrow
    layout->addSpacing(spacing);

    // Arrow
    QToolButton *arrow = new QToolButton(this);
    connect(arrow, SIGNAL(pressed()), SLOT(showMenu()));
    arrow->setArrowType(Qt::DownArrow);
    arrow->setAutoRaise(true);

    layout->addWidget(arrow);

    // push to left against non expanding labels
    layout->addStretch(100);

    QVBoxLayout *contentLayout = new QVBoxLayout(this);
    contentLayout->addLayout(layout);
    if (m_contentWidget) {
        contentLayout->addWidget(m_contentWidget);
    }
}

void AttachmentView::slotDownloadAttachment()
{
    QIcon icn = m_icon->icon(); // set to the default action by ::setEnabled
    m_downloadAttachment->setEnabled(false);
    m_icon->setIcon(icn);

    Imap::Network::FileDownloadManager *manager = new Imap::Network::FileDownloadManager(this, m_netAccess, m_partIndex);
    connect(manager, SIGNAL(fileNameRequested(QString*)), this, SLOT(slotFileNameRequested(QString*)));
    connect(manager, SIGNAL(transferError(QString)), m_messageView, SIGNAL(transferError(QString)));
    connect(manager, SIGNAL(transferError(QString)), this, SLOT(enableDownloadAgain()));
    connect(manager, SIGNAL(transferError(QString)), manager, SLOT(deleteLater()));
    connect(manager, SIGNAL(cancelled()), this, SLOT(enableDownloadAgain()));
    connect(manager, SIGNAL(cancelled()), manager, SLOT(deleteLater()));
    connect(manager, SIGNAL(succeeded()), this, SLOT(enableDownloadAgain()));
    connect(manager, SIGNAL(succeeded()), manager, SLOT(deleteLater()));
    manager->downloadPart();
}

void AttachmentView::slotOpenAttachment()
{
    m_openAttachment->setEnabled(false);

    Imap::Network::FileDownloadManager *manager = new Imap::Network::FileDownloadManager(this, m_netAccess, m_partIndex);
    connect(manager, SIGNAL(fileNameRequested(QString*)), this, SLOT(slotFileNameRequestedOnOpen(QString*)));
    connect(manager, SIGNAL(transferError(QString)), m_messageView, SIGNAL(transferError(QString)));
    connect(manager, SIGNAL(transferError(QString)), this, SLOT(onOpenFailed()));
    connect(manager, SIGNAL(transferError(QString)), manager, SLOT(deleteLater()));
    // we aren't connecting to cancelled() as it cannot really happen -- the filename is never empty
    connect(manager, SIGNAL(succeeded()), this, SLOT(openDownloadedAttachment()));
    connect(manager, SIGNAL(succeeded()), manager, SLOT(deleteLater()));
    manager->downloadPart();
}

void AttachmentView::slotFileNameRequestedOnOpen(QString *fileName)
{
    Q_ASSERT(!m_tmpFile);
    m_tmpFile = new QTemporaryFile(QDir::tempPath() + QLatin1String("/trojita-attachment-XXXXXX-") +
                                   fileName->replace(QLatin1Char('/'), QLatin1Char('_')));
    m_tmpFile->open();
    *fileName = m_tmpFile->fileName();
}

void AttachmentView::slotFileNameRequested(QString *fileName)
{
    QString fileLocation = QDir(Common::writablePath(Common::LOCATION_DOWNLOAD)).filePath(*fileName);
    *fileName = QFileDialog::getSaveFileName(this, tr("Save Attachment"), fileLocation, QString(), 0, QFileDialog::HideNameFilterDetails);
}

void AttachmentView::enableDownloadAgain()
{
    QIcon icn = m_icon->icon(); // set to the default action by ::setEnabled
    m_downloadAttachment->setEnabled(true);
    m_icon->setIcon(icn);
}

void AttachmentView::onOpenFailed()
{
    delete m_tmpFile;
    m_tmpFile = 0;
    m_openAttachment->setEnabled(true);
}

void AttachmentView::openDownloadedAttachment()
{
    Q_ASSERT(m_tmpFile);

    // Make sure that the file is read-only so that the launched application does not attempt to modify it
    m_tmpFile->setPermissions(QFile::ReadOwner);

    QDesktopServices::openUrl(QUrl::fromLocalFile(m_tmpFile->fileName()));

    // This will delete the temporary file in ten seconds. It should give the application plenty of time to start and also prevent
    // leaving cruft behind.
    new Common::DeleteAfter(m_tmpFile, 10000);
    m_tmpFile = 0;
    m_openAttachment->setEnabled(true);
}

void AttachmentView::showMenu()
{
    static_cast<QToolButton*>(sender())->setDown(false);
    QPoint p = QCursor::pos();
    p.rx() -= m_menu->width()/2;
    m_menu->popup(p);
}

void AttachmentView::mousePressEvent(QMouseEvent *event)
{
    m_dragStartPos = event->pos();
    QFrame::mousePressEvent(event);
}

void AttachmentView::mouseMoveEvent(QMouseEvent *event)
{
    QFrame::mouseMoveEvent(event);

    if ((m_dragStartPos - event->pos()).manhattanLength() <  QApplication::startDragDistance())
        return;

    if (m_partIndex.data(Imap::Mailbox::RoleMessageUid) == 0) {
        return;
    }

    QByteArray buf;
    QDataStream stream(&buf, QIODevice::WriteOnly);
    stream << m_partIndex.data(Imap::Mailbox::RoleMailboxName).toString() <<
              m_partIndex.data(Imap::Mailbox::RoleMailboxUidValidity).toUInt() <<
              m_partIndex.data(Imap::Mailbox::RoleMessageUid).toUInt() <<
              m_partIndex.data(Imap::Mailbox::RolePartId).toString() <<
              m_partIndex.data(Imap::Mailbox::RolePartPathToPart).toString();

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(QLatin1String("application/x-trojita-imap-part"), buf);
    QDrag *drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setHotSpot(event->pos());
    drag->exec(Qt::CopyAction, Qt::CopyAction);
}

QString AttachmentView::quoteMe() const
{
    const AbstractPartWidget *widget = dynamic_cast<const AbstractPartWidget *>(m_contentWidget);
    return widget && !m_contentWidget->isHidden() ? widget->quoteMe() : QString();
}

void AttachmentView::reloadContents()
{
    if (AbstractPartWidget *w = dynamic_cast<AbstractPartWidget*>(m_contentWidget))
        w->reloadContents();
}

}

