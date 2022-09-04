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

#include <sstream>
#include "KeepMailboxOpenTask.h"
#include "Common/InvokeMethod.h"
#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/MailboxTree.h"
#include "Imap/Model/Model.h"
#include "Imap/Model/TaskFactory.h"
#include "Imap/Model/TaskPresentationModel.h"
#include "DeleteMailboxTask.h"
#include "FetchMsgMetadataTask.h"
#include "FetchMsgPartTask.h"
#include "IdleLauncher.h"
#include "OpenConnectionTask.h"
#include "ObtainSynchronizedMailboxTask.h"
#include "OfflineConnectionTask.h"
#include "SortTask.h"
#include "NoopTask.h"
#include "UnSelectTask.h"

namespace Imap
{
namespace Mailbox
{

KeepMailboxOpenTask::KeepMailboxOpenTask(Model *model, const QModelIndex &mailboxIndex, Parser *oldParser) :
    ImapTask(model), mailboxIndex(mailboxIndex), synchronizeConn(0), shouldExit(false), isRunning(Running::NOT_YET),
    shouldRunNoop(false), shouldRunIdle(false), idleLauncher(0), unSelectTask(0),
    m_skippedStateSynces(0), m_performedStateSynces(0), m_syncingTimer(nullptr)
{
    Q_ASSERT(mailboxIndex.isValid());
    Q_ASSERT(mailboxIndex.model() == model);
    TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(static_cast<TreeItem *>(mailboxIndex.internalPointer()));
    Q_ASSERT(mailbox);

    // Now make sure that we at least try to load data from the cache
    Q_ASSERT(mailbox->m_children.size() > 0);
    TreeItemMsgList *list = dynamic_cast<TreeItemMsgList*>(mailbox->m_children[0]);
    Q_ASSERT(list);
    list->fetch(model);

    // We're the latest KeepMailboxOpenTask, so it makes a lot of sense to add us as the active
    // maintainingTask to the target mailbox
    mailbox->maintainingTask = this;

    if (oldParser) {
        // We're asked to re-use an existing connection. Let's see if there's something associated with it

        // We will use its parser, that's for sure already
        parser = oldParser;

        // Find if there's a KeepMailboxOpenTask already associated; if it is, we have to register with it
        if (model->accessParser(parser).maintainingTask) {
            // The parser looks busy -- some task is associated with it and has a mailbox open, so
            // let's just wait till we get a chance to play
            synchronizeConn = model->m_taskFactory->
                              createObtainSynchronizedMailboxTask(model, mailboxIndex, model->accessParser(oldParser).maintainingTask, this);
        } else if (model->accessParser(parser).connState < CONN_STATE_AUTHENTICATED) {
            // The parser is still in the process of being initialized, let's wait until it's completed
            Q_ASSERT(!model->accessParser(oldParser).activeTasks.isEmpty());
            ImapTask *task = model->accessParser(oldParser).activeTasks.front();
            synchronizeConn = model->m_taskFactory->createObtainSynchronizedMailboxTask(model, mailboxIndex, task, this);
        } else {
            // The parser is free, or at least there's no KeepMailboxOpenTask associated with it.
            // There's no mailbox besides us in the game, yet, so we can simply schedule us for immediate execution.
            synchronizeConn = model->m_taskFactory->createObtainSynchronizedMailboxTask(model, mailboxIndex, 0, this);
            // We'll also register with the model, so that all other KeepMailboxOpenTask which could get constructed in future
            // know about us and don't step on our toes.  This means that further KeepMailboxOpenTask which could possibly want
            // to use this connection will have to go through this task at first.
            model->accessParser(parser).maintainingTask = this;
            QTimer::singleShot(0, this, SLOT(slotPerformConnection()));
        }

        // We shall catch destruction of any preexisting tasks so that we can properly launch IDLE etc in response to their termination
        Q_FOREACH(ImapTask *task, model->accessParser(parser).activeTasks) {
            connect(task, &QObject::destroyed, this, &KeepMailboxOpenTask::slotTaskDeleted);
        }
    } else {
        ImapTask *conn = 0;
        if (model->networkPolicy() == NETWORK_OFFLINE) {
            // Well, except that we cannot really open a new connection now
            conn = new OfflineConnectionTask(model);
        } else {
            conn = model->m_taskFactory->createOpenConnectionTask(model);
        }
        parser = conn->parser;
        Q_ASSERT(parser);
        model->accessParser(parser).maintainingTask = this;
        synchronizeConn = model->m_taskFactory->createObtainSynchronizedMailboxTask(model, mailboxIndex, conn, this);
    }

    Q_ASSERT(synchronizeConn);

    // Setup the timer for NOOPing. It won't get started at this time, though.
    noopTimer = new QTimer(this);
    connect(noopTimer, &QTimer::timeout, this, &KeepMailboxOpenTask::slotPerformNoop);
    bool ok;
    int timeout = model->property("trojita-imap-noop-period").toUInt(&ok);
    if (! ok)
        timeout = 2 * 60 * 1000; // once every two minutes
    noopTimer->setInterval(timeout);
    noopTimer->setSingleShot(true);

    fetchPartTimer = new QTimer(this);
    connect(fetchPartTimer, &QTimer::timeout, this, &KeepMailboxOpenTask::slotFetchRequestedParts);
    timeout = model->property("trojita-imap-delayed-fetch-part").toUInt(&ok);
    if (! ok)
        timeout = 50;
    fetchPartTimer->setInterval(timeout);
    fetchPartTimer->setSingleShot(true);

    fetchEnvelopeTimer = new QTimer(this);
    connect(fetchEnvelopeTimer, &QTimer::timeout, this, &KeepMailboxOpenTask::slotFetchRequestedEnvelopes);
    fetchEnvelopeTimer->setInterval(0); // message metadata is pretty important, hence an immediate fetch
    fetchEnvelopeTimer->setSingleShot(true);

    limitBytesAtOnce = model->property("trojita-imap-limit-fetch-bytes-per-group").toUInt(&ok);
    if (! ok)
        limitBytesAtOnce = 1024 * 1024;

    limitMessagesAtOnce = model->property("trojita-imap-limit-fetch-messages-per-group").toInt(&ok);
    if (! ok)
        limitMessagesAtOnce = 300;

    limitParallelFetchTasks = model->property("trojita-imap-limit-parallel-fetch-tasks").toInt(&ok);
    if (! ok)
        limitParallelFetchTasks = 10;

    limitActiveTasks = model->property("trojita-imap-limit-active-tasks").toInt(&ok);
    if (! ok)
        limitActiveTasks = 100;

    CHECK_TASK_TREE
    emit model->mailboxSyncingProgress(mailboxIndex, STATE_WAIT_FOR_CONN);

    /** @short How often to reset the time window (in ms) for determining mass-change mode */
    const int throttlingWindowLength = 1000;

    m_syncingTimer = new QTimer(this);
    m_syncingTimer->setSingleShot(true);
    // This timeout specifies how long we're going to wait for an incoming reply which usually triggers state syncing.
    // If no such response arrives during this time window, the changes are saved on disk; if, however, something does
    // arrive, the rate of saving is only moderated based on the number of reponses which were already received,
    // but which did not result in state saving yet.
    m_syncingTimer->setInterval(throttlingWindowLength);
    connect(m_syncingTimer, &QTimer::timeout, this, &KeepMailboxOpenTask::syncingTimeout);
}

void KeepMailboxOpenTask::slotPerformConnection()
{
    CHECK_TASK_TREE
    Q_ASSERT(synchronizeConn);
    Q_ASSERT(!synchronizeConn->isFinished());
    if (_dead) {
        _failed(tr("Asked to die"));
        synchronizeConn->die(QStringLiteral("KeepMailboxOpenTask died before the sync started"));
        return;
    }

    connect(synchronizeConn, &QObject::destroyed, this, &KeepMailboxOpenTask::slotTaskDeleted);
    synchronizeConn->perform();
}

void KeepMailboxOpenTask::addDependentTask(ImapTask *task)
{
    CHECK_TASK_TREE
    Q_ASSERT(task);

    // FIXME: what about abort()/die() here?

    breakOrCancelPossibleIdle();

    DeleteMailboxTask *deleteTask = qobject_cast<DeleteMailboxTask*>(task);
    if (!deleteTask || deleteTask->mailbox != mailboxIndex.data(RoleMailboxName).toString()) {
        deleteTask = 0;
    }

    if (ObtainSynchronizedMailboxTask *obtainTask = qobject_cast<ObtainSynchronizedMailboxTask *>(task)) {
        // Another KeepMailboxOpenTask would like to replace us, so we shall die, eventually.
        // This branch is reimplemented from ImapTask

        dependentTasks.append(task);
        waitingObtainTasks.append(obtainTask);
        shouldExit = true;
        task->updateParentTask(this);

        // Before we can die, though, we have to accommodate fetch requests for all envelopes and parts queued so far.
        slotFetchRequestedEnvelopes();
        slotFetchRequestedParts();

        if (! hasPendingInternalActions() && (! synchronizeConn || synchronizeConn->isFinished())) {
            QTimer::singleShot(0, this, SLOT(terminate()));
        }

        Q_FOREACH(ImapTask *abortable, abortableTasks) {
            abortable->abort();
        }
    } else if (deleteTask) {
        // Got a request to delete the current mailbox. Fair enough, here we go!

        if (hasPendingInternalActions() || (synchronizeConn && !synchronizeConn->isFinished())) {
            // Hmm, this is bad -- the caller has instructed us to delete the current mailbox, but we still have
            // some pending actions (or have not even started yet). Better reject the request for deleting than lose some data.
            // Alternatively, we might pretend that we're a performance-oriented library and "optimize out" the
            // data transfer by deleting early :)
            deleteTask->mailboxHasPendingActions();
            return;
        }

        m_deleteCurrentMailboxTask = deleteTask;
        shouldExit = true;
        connect(task, &QObject::destroyed, this, &KeepMailboxOpenTask::slotTaskDeleted);
        ImapTask::addDependentTask(task);
        QTimer::singleShot(0, this, SLOT(slotActivateTasks()));

    } else {
        // This branch calls the inherited ImapTask::addDependentTask()
        connect(task, &QObject::destroyed, this, &KeepMailboxOpenTask::slotTaskDeleted);
        ImapTask::addDependentTask(task);
        if (task->needsMailbox()) {
            // it's a task which is tied to a particular mailbox
            dependingTasksForThisMailbox.append(task);
        } else {
            dependingTasksNoMailbox.append(task);
        }
        QTimer::singleShot(0, this, SLOT(slotActivateTasks()));
    }
}

void KeepMailboxOpenTask::slotTaskDeleted(QObject *object)
{
    if (_finished)
        return;

    if (!model) {
        // we're very likely hitting this during some rather unclean destruction -> ignore this and die ASAP
        // See https://bugs.kde.org/show_bug.cgi?id=336090
        return;
    }

    if (!model->m_parsers.contains(parser)) {
        // The parser is gone; we have to get out of here ASAP
        _failed(tr("Parser is gone"));
        die(tr("Parser is gone"));
        return;
    }
    // FIXME: abort/die

    // Now, object is no longer an ImapTask*, as this gets emitted from inside QObject's destructor. However,
    // we can't use the passed pointer directly, and therefore we have to perform the cast here. It is safe
    // to do that here, as we're only interested in raw pointer value.
    if (object) {
        dependentTasks.removeOne(reinterpret_cast<ImapTask *>(object));
        dependingTasksForThisMailbox.removeOne(reinterpret_cast<ImapTask *>(object));
        dependingTasksNoMailbox.removeOne(reinterpret_cast<ImapTask *>(object));
        runningTasksForThisMailbox.removeOne(reinterpret_cast<ImapTask *>(object));
        fetchPartTasks.removeOne(reinterpret_cast<FetchMsgPartTask *>(object));
        fetchMetadataTasks.removeOne(reinterpret_cast<FetchMsgMetadataTask *>(object));
        abortableTasks.removeOne(reinterpret_cast<FetchMsgMetadataTask *>(object));
    }

    if (isReadyToTerminate()) {
        terminate();
    } else if (shouldRunNoop) {
        // A command just completed, and NOOPing is active, so let's schedule/postpone it again
        noopTimer->start();
    } else if (canRunIdleRightNow()) {
        // A command just completed and IDLE is supported, so let's queue/schedule/postpone it
        idleLauncher->enterIdleLater();
    }
    // It's possible that we can start more tasks at this time...
    activateTasks();
}

void KeepMailboxOpenTask::terminate()
{
    if (_aborted) {
        // We've already been there, so we *cannot* proceed towards activating our replacement tasks
        return;
    }
    abort();

    m_syncingTimer->stop();
    syncingTimeout();

    // FIXME: abort/die

    Q_ASSERT(dependingTasksForThisMailbox.isEmpty());
    Q_ASSERT(dependingTasksNoMailbox.isEmpty());
    Q_ASSERT(requestedParts.isEmpty());
    Q_ASSERT(requestedEnvelopes.isEmpty());
    Q_ASSERT(runningTasksForThisMailbox.isEmpty());
    Q_ASSERT(abortableTasks.isEmpty());
    Q_ASSERT(!m_syncingTimer->isActive());
    Q_ASSERT(m_skippedStateSynces == 0);

    // Break periodic activities
    shouldRunIdle = false;
    shouldRunNoop = false;
    isRunning = Running::NOT_ANYMORE;

    // Merge the lists of waiting tasks
    if (!waitingObtainTasks.isEmpty()) {
        ObtainSynchronizedMailboxTask *first = waitingObtainTasks.takeFirst();
        dependentTasks.removeOne(first);
        Q_ASSERT(first);
        Q_ASSERT(first->keepTaskChild);
        Q_ASSERT(first->keepTaskChild->synchronizeConn == first);

        CHECK_TASK_TREE
        // Update the parent information for the moved tasks
        Q_FOREACH(ObtainSynchronizedMailboxTask *movedObtainTask, waitingObtainTasks) {
            Q_ASSERT(movedObtainTask->parentTask);
            movedObtainTask->parentTask->dependentTasks.removeOne(movedObtainTask);
            movedObtainTask->parentTask = first->keepTaskChild;
            first->keepTaskChild->dependentTasks.append(movedObtainTask);
        }
        CHECK_TASK_TREE

        // And launch the replacement
        first->keepTaskChild->waitingObtainTasks = waitingObtainTasks + first->keepTaskChild->waitingObtainTasks;
        model->accessParser(parser).maintainingTask = first->keepTaskChild;
        // make sure that if the SELECT dies uncleanly, such as with a missing [CLOSED], we get killed as well
        connect(first->keepTaskChild, &ImapTask::failed, this, &KeepMailboxOpenTask::finalizeTermination);
        first->keepTaskChild->slotPerformConnection();
    } else {
        Q_ASSERT(dependentTasks.isEmpty());
    }
    if (model->accessParser(parser).connState == CONN_STATE_SELECTING_WAIT_FOR_CLOSE) {
        // we have to be kept busy, otherwise the responses which are still destined for *this* mailbox would
        // get delivered to the new one
    } else {
        finalizeTermination();
    }
    CHECK_TASK_TREE
}

void KeepMailboxOpenTask::perform()
{
    // FIXME: abort/die

    Q_ASSERT(synchronizeConn);
    Q_ASSERT(synchronizeConn->isFinished());
    parser = synchronizeConn->parser;
    synchronizeConn = 0; // will get deleted by Model
    markAsActiveTask();

    isRunning = Running::RUNNING;
    fetchPartTimer->start();
    fetchEnvelopeTimer->start();

    if (!waitingObtainTasks.isEmpty()) {
        shouldExit = true;
    }

    activateTasks();

    if (model->accessParser(parser).capabilitiesFresh && model->accessParser(parser).capabilities.contains(QStringLiteral("IDLE"))) {
        shouldRunIdle = true;
    } else {
        shouldRunNoop = true;
    }

    if (shouldRunNoop) {
        noopTimer->start();
    } else if (shouldRunIdle) {
        idleLauncher = new IdleLauncher(this);
        if (canRunIdleRightNow()) {
            // There's no task yet, so we have to start IDLE now
            idleLauncher->enterIdleLater();
        }
    }
}

void KeepMailboxOpenTask::resynchronizeMailbox()
{
    // FIXME: abort/die

    if (isRunning != Running::NOT_YET) {
        // Instead of wild magic with re-creating synchronizeConn, it's way easier to
        // just have us replaced by another KeepMailboxOpenTask
        model->m_taskFactory->createKeepMailboxOpenTask(model, mailboxIndex, parser);
    } else {
        // We aren't running yet, which means that the sync hadn't happened yet, and therefore
        // we don't have to do it "once again" -- it will happen automatically later on.
    }
}

#define CHECK_IS_RUNNING \
    switch (isRunning) { \
    case Running::NOT_YET: \
        return false; \
    case Running::NOT_ANYMORE: \
        /* OK, a lost reply -- we're already switching to another mailbox, and even though this might seem */ \
        /* to be a safe change, we just cannot react to this right now :(. */ \
        /* Also, don't eat further replies once we're dead :) */ \
        return model->accessParser(parser).connState == CONN_STATE_SELECTING_WAIT_FOR_CLOSE; \
    case Running::RUNNING: \
        /* normal state -> handle this */ \
        break; \
    }

bool KeepMailboxOpenTask::handleNumberResponse(const Imap::Responses::NumberResponse *const resp)
{
    if (_dead) {
        _failed(tr("Asked to die"));
        return true;
    }

    if (dieIfInvalidMailbox())
        return true;

    CHECK_IS_RUNNING

    TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
    Q_ASSERT(mailbox);
    TreeItemMsgList *list = dynamic_cast<TreeItemMsgList *>(mailbox->m_children[0]);
    Q_ASSERT(list);
    // FIXME: tests!
    if (resp->kind == Imap::Responses::EXPUNGE) {
        mailbox->handleExpunge(model, *resp);
        mailbox->syncState.setExists(mailbox->syncState.exists() - 1);
        saveSyncStateNowOrLater(mailbox);
        return true;
    } else if (resp->kind == Imap::Responses::EXISTS) {

        if (resp->number == static_cast<uint>(list->m_children.size())) {
            // no changes
            return true;
        }

        mailbox->handleExists(model, *resp);

        breakOrCancelPossibleIdle();

        Q_ASSERT(list->m_children.size());
        uint highestKnownUid = 0;
        for (int i = list->m_children.size() - 1; ! highestKnownUid && i >= 0; --i) {
            highestKnownUid = static_cast<const TreeItemMessage *>(list->m_children[i])->uid();
            //qDebug() << "UID disco: trying seq" << i << highestKnownUid;
        }
        breakOrCancelPossibleIdle();
        newArrivalsFetch.append(parser->uidFetch(Sequence::startingAt(
                                                // Did the UID walk return a usable number?
                                                highestKnownUid ?
                                                // Yes, we've got at least one message with a UID known -> ask for higher
                                                // but don't forget to compensate for an pre-existing UIDNEXT value
                                                qMax(mailbox->syncState.uidNext(), highestKnownUid + 1)
                                                :
                                                // No messages, or no messages with valid UID -> use the UIDNEXT from the syncing state
                                                // but prevent a possible invalid 0:*
                                                qMax(mailbox->syncState.uidNext(), 1u)
                                            ), QList<QByteArray>() << "FLAGS"));
        model->m_taskModel->slotTaskMighHaveChanged(this);
        return true;
    } else if (resp->kind == Imap::Responses::RECENT) {
        mailbox->syncState.setRecent(resp->number);
        list->m_recentMessageCount = resp->number;
        model->emitMessageCountChanged(mailbox);
        saveSyncStateNowOrLater(mailbox);
        return true;
    } else {
        return false;
    }
}

bool KeepMailboxOpenTask::handleVanished(const Responses::Vanished *const resp)
{
    if (_dead) {
        _failed(tr("Asked to die"));
        return true;
    }

    if (dieIfInvalidMailbox())
        return true;

    CHECK_IS_RUNNING

    if (resp->earlier != Responses::Vanished::NOT_EARLIER)
        return false;

    TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
    Q_ASSERT(mailbox);

    mailbox->handleVanished(model, *resp);
    saveSyncStateNowOrLater(mailbox);
    return true;
}

bool KeepMailboxOpenTask::handleFetch(const Imap::Responses::Fetch *const resp)
{
    if (dieIfInvalidMailbox())
        return true;

    if (_dead) {
        _failed(tr("Asked to die"));
        return true;
    }

    CHECK_IS_RUNNING

    TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
    Q_ASSERT(mailbox);
    model->genericHandleFetch(mailbox, resp);
    return true;
}

void KeepMailboxOpenTask::slotPerformNoop()
{
    // FIXME: abort/die
    model->m_taskFactory->createNoopTask(model, this);
}

bool KeepMailboxOpenTask::handleStateHelper(const Imap::Responses::State *const resp)
{
    // FIXME: abort/die

    if (handleResponseCodeInsideState(resp))
        return true;

    // FIXME: checks for shouldExit and proper boundaries?

    if (resp->respCode == Responses::CLOSED) {
        switch (model->accessParser(parser).connState) {
        case CONN_STATE_SELECTING:
        case CONN_STATE_SELECTING_WAIT_FOR_CLOSE:
            model->changeConnectionState(parser, CONN_STATE_SELECTING);
            finalizeTermination();
            break;
        case CONN_STATE_LOGOUT:
            finalizeTermination();
            break;
        default:
            throw UnexpectedResponseReceived("No other mailbox is being selected, but got a [CLOSED] response", *resp);
        }
    }

    if (resp->tag.isEmpty())
        return false;

    if (resp->tag == tagIdle) {

        Q_ASSERT(idleLauncher);
        if (resp->kind == Responses::OK) {
            // The IDLE got terminated for whatever reason, so we should schedule its restart
            idleLauncher->idleCommandCompleted();
            if (canRunIdleRightNow()) {
                idleLauncher->enterIdleLater();
            }
        } else {
            // The IDLE command has failed. Let's assume it's a permanent error and don't request it in future.
            log(QStringLiteral("The IDLE command has failed"));
            shouldRunIdle = false;
            idleLauncher->idleCommandFailed();
            idleLauncher->deleteLater();
            idleLauncher = 0;
        }
        tagIdle.clear();
        // IDLE is special because it's not really a native Task. Therefore, we have to duplicate the check for its completion
        // and possible termination request here.
        // FIXME: maybe rewrite IDLE to be a native task and get all the benefits for free? Any drawbacks?
        if (shouldExit && ! hasPendingInternalActions() && (! synchronizeConn || synchronizeConn->isFinished())) {
            terminate();
        }
        return true;
    } else if (newArrivalsFetch.contains(resp->tag)) {
        newArrivalsFetch.removeOne(resp->tag);

        if (newArrivalsFetch.isEmpty() && mailboxIndex.isValid()) {
            // No pending commands for fetches of the mailbox state -> we have a consistent and accurate, up-to-date view
            // -> we should save this
            TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(static_cast<TreeItem *>(mailboxIndex.internalPointer()));
            Q_ASSERT(mailbox);
            mailbox->saveSyncStateAndUids(model);
        }

        if (resp->kind != Responses::OK) {
            _failed(QLatin1String("FETCH of new arrivals failed: ") + resp->message);
        }
        // Don't forget to resume IDLE, if desired; that's easiest by simply behaving as if a "task" has just finished
        slotTaskDeleted(0);
        model->m_taskModel->slotTaskMighHaveChanged(this);
        return true;
    } else if (resp->tag == tagClose) {
        tagClose.clear();
        model->changeConnectionState(parser, CONN_STATE_AUTHENTICATED);
        if (m_deleteCurrentMailboxTask) {
            m_deleteCurrentMailboxTask->perform();
        }
        if (resp->kind != Responses::OK) {
            _failed(QLatin1String("CLOSE failed: ") + resp->message);
        }
        terminate();
        return true;
    } else {
        return false;
    }
}

/** @short Reimplemented from ImapTask

This function's semantics is slightly shifted from ImapTask::abort(). It gets called when the KeepMailboxOpenTask has decided to
terminate and its biggest goals are to:

- Prevent any further activity from hitting this parser. We're here to "guard" access to it, and we're about to terminate, so the
  tasks shall negotiate their access through some other KeepMailboxOpenTask.
- Terminate our internal code which might want to access the connection (NoopTask, IdleLauncher,...)
*/
void KeepMailboxOpenTask::abort()
{
    if (noopTimer)
        noopTimer->stop();
    if (idleLauncher)
        idleLauncher->die();

    detachFromMailbox();

    _aborted = true;
    // We do not want to propagate the signal to the child tasks, though -- the KeepMailboxOpenTask::abort() is used in the course
    // of the regular "hey, free this connection and pass it to another KeepMailboxOpenTask" situations.
}

/** @short Stop working as a maintaining task */
void KeepMailboxOpenTask::detachFromMailbox()
{
    if (mailboxIndex.isValid()) {
        // Mark current mailbox as "orphaned by the housekeeping task"
        TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(static_cast<TreeItem *>(mailboxIndex.internalPointer()));
        Q_ASSERT(mailbox);

        // We're already obsolete -> don't pretend to accept new tasks
        if (mailbox->maintainingTask == this)
            mailbox->maintainingTask = 0;
    }
    if (model->m_parsers.contains(parser) && model->accessParser(parser).maintainingTask == this) {
        model->accessParser(parser).maintainingTask = 0;
    }
}

/** @short Reimplemented from ImapTask

We're aksed to die right now, so we better take any depending stuff with us. That poor tasks are not going to outlive me!
*/
void KeepMailboxOpenTask::die(const QString &message)
{
    if (shouldExit) {
        // OK, we're done, and getting killed. This is fine; just don't emit failed()
        // because we aren't actually failing.
        // This is a speciality of the KeepMailboxOpenTask because it's the only task
        // this has a very long life.
        _finished = true;
    }
    ImapTask::die(message);
    detachFromMailbox();
}

/** @short Kill all pending tasks -- both the regular one and the replacement ObtainSynchronizedMailboxTask instances

Reimplemented from the ImapTask.
*/
void KeepMailboxOpenTask::killAllPendingTasks(const QString &message)
{
    Q_FOREACH(ImapTask *task, dependingTasksForThisMailbox) {
        task->die(message);
    }
    Q_FOREACH(ImapTask *task, dependingTasksNoMailbox) {
        task->die(message);
    }
    Q_FOREACH(ImapTask *task, waitingObtainTasks) {
        task->die(message);
    }
}

QString KeepMailboxOpenTask::debugIdentification() const
{
    if (! mailboxIndex.isValid())
        return QStringLiteral("[invalid mailboxIndex]");

    TreeItemMailbox *mailbox = dynamic_cast<TreeItemMailbox *>(static_cast<TreeItem *>(mailboxIndex.internalPointer()));
    Q_ASSERT(mailbox);
    return QStringLiteral("attached to %1%2%3").arg(mailbox->mailbox(),
            (synchronizeConn && ! synchronizeConn->isFinished()) ? QStringLiteral(" [syncConn unfinished]") : QString(),
            shouldExit ? QStringLiteral(" [shouldExit]") : QString()
                                                       );
}

/** @short The user wants us to go offline */
void KeepMailboxOpenTask::stopForLogout()
{
    abort();
    breakOrCancelPossibleIdle();
    killAllPendingTasks(tr("Logging off..."));

    // We're supposed to go offline. Given that we're a long-running task, I do not consider this a "failure".
    // In particular, if the initial SELECT has not finished yet, the ObtainSynchronizedMailboxTask would get
    // killed as well, and hence the mailboxSyncFailed() signal will get emitted.
    // The worst thing which can possibly happen is that we're in the middle of checking the new arrivals.
    // That's bad, because we've got unknown UIDs in our in-memory map, which is going to hurt during the next sync
    // -- but that's something which should be handled elsewhere, IMHO.
    // Therefore, make sure a subsequent call to die() doesn't propagate a failure.
    shouldExit = true;
}

bool KeepMailboxOpenTask::handleFlags(const Imap::Responses::Flags *const resp)
{
    if (dieIfInvalidMailbox())
        return true;

    // Well, there isn't much point in keeping track of these flags, but given that
    // IMAP servers are happy to send these responses even after the initial sync, we
    // better handle them explicitly here.
    TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
    Q_ASSERT(mailbox);
    mailbox->syncState.setFlags(resp->flags);
    return true;
}

void KeepMailboxOpenTask::activateTasks()
{
    // FIXME: abort/die

    if (isRunning != Running::RUNNING)
        return;

    breakOrCancelPossibleIdle();

    if (m_deleteCurrentMailboxTask) {
        closeMailboxDestructively();
        return;
    }

    slotFetchRequestedEnvelopes();
    slotFetchRequestedParts();

    while (!dependingTasksForThisMailbox.isEmpty() && model->accessParser(parser).activeTasks.size() < limitActiveTasks) {
        breakOrCancelPossibleIdle();
        ImapTask *task = dependingTasksForThisMailbox.takeFirst();
        runningTasksForThisMailbox.append(task);
        dependentTasks.removeOne(task);
        task->perform();
    }
    while (!dependingTasksNoMailbox.isEmpty() && model->accessParser(parser).activeTasks.size() < limitActiveTasks) {
        breakOrCancelPossibleIdle();
        ImapTask *task = dependingTasksNoMailbox.takeFirst();
        dependentTasks.removeOne(task);
        task->perform();
    }

    if (idleLauncher && canRunIdleRightNow())
        idleLauncher->enterIdleLater();
}

void KeepMailboxOpenTask::requestPartDownload(const uint uid, const QByteArray &partId, const uint estimatedSize)
{
    requestedParts[uid].insert(partId);
    requestedPartSizes[uid] += estimatedSize;
    if (!fetchPartTimer->isActive()) {
        fetchPartTimer->start();
    }
}

void KeepMailboxOpenTask::requestEnvelopeDownload(const uint uid)
{
    requestedEnvelopes.append(uid);
    if (!fetchEnvelopeTimer->isActive()) {
        fetchEnvelopeTimer->start();
    }
}

void KeepMailboxOpenTask::slotFetchRequestedParts()
{
    // FIXME: abort/die

    if (requestedParts.isEmpty())
        return;

    breakOrCancelPossibleIdle();

    auto it = requestedParts.begin();
    auto parts = *it;

    // When asked to exit, do as much as possible and die
    while (shouldExit || fetchPartTasks.size() < limitParallelFetchTasks) {
        Imap::Uids uids;
        uint totalSize = 0;
        while (uids.size() < limitMessagesAtOnce && it != requestedParts.end() && totalSize < limitBytesAtOnce) {
            if (parts != *it)
                break;
            parts = *it;
            uids << it.key();
            totalSize += requestedPartSizes.take(it.key());
            it = requestedParts.erase(it);
        }
        if (uids.isEmpty())
            return;

        fetchPartTasks << model->m_taskFactory->createFetchMsgPartTask(model, mailboxIndex, uids, parts.values());
    }
}

void KeepMailboxOpenTask::slotFetchRequestedEnvelopes()
{
    // FIXME: abort/die

    if (requestedEnvelopes.isEmpty())
        return;

    breakOrCancelPossibleIdle();

    Imap::Uids fetchNow;
    if (shouldExit) {
        fetchNow = requestedEnvelopes;
        requestedEnvelopes.clear();
    } else {
        const int amount = qMin(requestedEnvelopes.size(), limitMessagesAtOnce); // FIXME: add an extra limit?
        fetchNow = requestedEnvelopes.mid(0, amount);
        requestedEnvelopes.erase(requestedEnvelopes.begin(), requestedEnvelopes.begin() + amount);
    }
    fetchMetadataTasks << model->m_taskFactory->createFetchMsgMetadataTask(model, mailboxIndex, fetchNow);
}

void KeepMailboxOpenTask::breakOrCancelPossibleIdle()
{
    if (idleLauncher) {
        idleLauncher->finishIdle();
    }
}

bool KeepMailboxOpenTask::handleResponseCodeInsideState(const Imap::Responses::State *const resp)
{
    switch (resp->respCode) {
    case Responses::UIDNEXT:
    {
        if (dieIfInvalidMailbox())
            return resp->tag.isEmpty();

        TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
        Q_ASSERT(mailbox);
        const Responses::RespData<uint> *const num = dynamic_cast<const Responses::RespData<uint>* const>(resp->respCodeData.data());
        if (num) {
            mailbox->syncState.setUidNext(num->data);
            saveSyncStateNowOrLater(mailbox);
            // We shouldn't eat tagged responses from this context
            return resp->tag.isEmpty();
        } else {
            throw CantHappen("State response has invalid UIDNEXT respCodeData", *resp);
        }
        break;
    }
    case Responses::PERMANENTFLAGS:
        // Another useless one, but we want to consume it now to prevent a warning about
        // an unhandled message
    {
        if (dieIfInvalidMailbox())
            return resp->tag.isEmpty();

        TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
        Q_ASSERT(mailbox);
        const Responses::RespData<QStringList> *const num = dynamic_cast<const Responses::RespData<QStringList>* const>(resp->respCodeData.data());
        if (num) {
            mailbox->syncState.setPermanentFlags(num->data);
            // We shouldn't eat tagged responses from this context
            return resp->tag.isEmpty();
        } else {
            throw CantHappen("State response has invalid PERMANENTFLAGS respCodeData", *resp);
        }
        break;
    }
    case Responses::HIGHESTMODSEQ:
    {
        if (dieIfInvalidMailbox())
            return resp->tag.isEmpty();

        TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
        Q_ASSERT(mailbox);
        const Responses::RespData<quint64> *const num = dynamic_cast<const Responses::RespData<quint64>* const>(resp->respCodeData.data());
        Q_ASSERT(num);
        mailbox->syncState.setHighestModSeq(num->data);
        saveSyncStateNowOrLater(mailbox);
        return resp->tag.isEmpty();
    }
    case Responses::UIDVALIDITY:
    {
        if (dieIfInvalidMailbox())
            return resp->tag.isEmpty();

        TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
        Q_ASSERT(mailbox);
        const Responses::RespData<uint> *const num = dynamic_cast<const Responses::RespData<uint>* const>(resp->respCodeData.data());
        Q_ASSERT(num);
        if (mailbox->syncState.uidValidity() == num->data) {
            // this is a harmless and useless message
            return resp->tag.isEmpty();
        } else {
            // On the other hand, this a serious condition -- the server is telling us that the UIDVALIDITY has changed while
            // a mailbox is open. There isn't much we could do here; having code for handling this gracefuly is just too much
            // work for little to no benefit.
            // The sane thing is to disconnect from this mailbox.
            EMIT_LATER(model, imapError, Q_ARG(QString, tr("The UIDVALIDITY has changed while mailbox is open. Please reconnect.")));
            model->setNetworkPolicy(NETWORK_OFFLINE);
            return resp->tag.isEmpty();
        }
    }
    default:
        // Do nothing here
        break;
    }
    return false;
}

void KeepMailboxOpenTask::slotUnselected()
{
    switch (model->accessParser(parser).connState) {
    case CONN_STATE_SYNCING:
    case CONN_STATE_SELECTED:
    case CONN_STATE_FETCHING_PART:
    case CONN_STATE_FETCHING_MSG_METADATA:
        model->changeConnectionState(parser, CONN_STATE_AUTHENTICATED);
        break;
    default:
        // no need to do anything from here
        break;
    }
    detachFromMailbox();
    isRunning = Running::RUNNING; // WTF?
    shouldExit = true;
    _failed(tr("UNSELECTed"));
}

bool KeepMailboxOpenTask::dieIfInvalidMailbox()
{
    if (mailboxIndex.isValid())
        return false;

    if (m_deleteCurrentMailboxTask) {
        // The current mailbox was supposed to be deleted; don't try to UNSELECT from this context
        return true;
    }

    // See ObtainSynchronizedMailboxTask::dieIfInvalidMailbox() for details
    if (!unSelectTask && isRunning == Running::RUNNING) {
        unSelectTask = model->m_taskFactory->createUnSelectTask(model, this);
        connect(unSelectTask, &ImapTask::completed, this, &KeepMailboxOpenTask::slotUnselected);
        unSelectTask->perform();
    }

    return true;
}

bool KeepMailboxOpenTask::hasPendingInternalActions() const
{
    bool hasToWaitForIdleTermination = idleLauncher ? idleLauncher->waitingForIdleTaggedTermination() : false;
    return !(dependingTasksForThisMailbox.isEmpty() && dependingTasksNoMailbox.isEmpty() && runningTasksForThisMailbox.isEmpty() &&
             requestedParts.isEmpty() && requestedEnvelopes.isEmpty() && newArrivalsFetch.isEmpty()) || hasToWaitForIdleTermination;
}

/** @short Returns true if this task can be safely terminated

FIXME: document me
*/
bool KeepMailboxOpenTask::isReadyToTerminate() const
{
    return shouldExit && !hasPendingInternalActions() && (!synchronizeConn || synchronizeConn->isFinished());
}

/** @short Return true if we're configured to run IDLE and if there's no ongoing activity */
bool KeepMailboxOpenTask::canRunIdleRightNow() const
{
    bool res = shouldRunIdle && dependingTasksForThisMailbox.isEmpty() &&
            dependingTasksNoMailbox.isEmpty() && newArrivalsFetch.isEmpty();

    // If there's just one active tasks, it's the "this" one. If there are more of them, let's see if it's just one more
    // and that one more thing is a SortTask which is in the "just updating" mode.
    // If that is the case, we can still allow further IDLE, that task will abort idling when it needs to.
    // Nifty, isn't it?
    if (model->accessParser(parser).activeTasks.size() > 1) {
        if (model->accessParser(parser).activeTasks.size() == 2 &&
                dynamic_cast<SortTask*>(model->accessParser(parser).activeTasks[1]) &&
                dynamic_cast<SortTask*>(model->accessParser(parser).activeTasks[1])->isJustUpdatingNow()) {
            // This is OK, so no need to clear the "OK" flag
        } else {
            // Too bad, cannot IDLE
            res = false;
        }
    }

    if (!res)
        return false;

    Q_ASSERT(model->accessParser(parser).activeTasks.front() == this);
    return true;
}

QVariant KeepMailboxOpenTask::taskData(const int role) const
{
    // FIXME
    Q_UNUSED(role);
    return QVariant();
}

/** @short The specified task can be abort()ed when the mailbox shall be vacanted

It's an error to call this on a task which we aren't tracking already.
*/
void KeepMailboxOpenTask::feelFreeToAbortCaller(ImapTask *task)
{
    abortableTasks.append(task);
}

/** @short It's time to reset the counters and perform the sync */
void KeepMailboxOpenTask::syncingTimeout()
{
    if (!mailboxIndex.isValid()) {
        return;
    }
    if (m_skippedStateSynces == 0) {
        // This means that state syncing it not being throttled, i.e. we're just resetting the window
        // which determines the rate of requests which would normally trigger saving
        m_performedStateSynces = 0;
    } else {
        // there's been no fresh arrivals for our timeout period -> let's flush the pending events
        TreeItemMailbox *mailbox = Model::mailboxForSomeItem(mailboxIndex);
        Q_ASSERT(mailbox);
        saveSyncStateIfPossible(mailbox);
    }
}

void KeepMailboxOpenTask::saveSyncStateNowOrLater(Imap::Mailbox::TreeItemMailbox *mailbox)
{
    bool saveImmediately = true;

    /** @short After processing so many responses immediately, switch to a delayed mode where the saving is deferred */
    const uint throttlingThreshold = 100;

    /** @short Flush the queue after postponing this number of messages.

    It's "ridiculously high", but it's still a number so that our integer newer wraps.
    */
    const uint maxDelayedResponses = 10000;

    if (m_skippedStateSynces > 0) {
        // we're actively throttling
        if (m_skippedStateSynces >= maxDelayedResponses) {
            // ...but we've been throttling too many responses, let's flush them now
            Q_ASSERT(m_syncingTimer->isActive());
            // do not go back to 0, otherwise there will be a lot of immediate events within each interval
            m_performedStateSynces = throttlingThreshold;
            saveImmediately = true;
        } else {
            ++m_skippedStateSynces;
            saveImmediately = false;
        }
    } else {
        // no throttling, cool
        if (m_performedStateSynces >= throttlingThreshold) {
            // ...but we should start throttling now
            ++m_skippedStateSynces;
            saveImmediately = false;
        } else {
            ++m_performedStateSynces;
            if (!m_syncingTimer->isActive()) {
                // reset the sliding window
                m_syncingTimer->start();
            }
            saveImmediately = true;
        }
    }

    if (saveImmediately) {
        saveSyncStateIfPossible(mailbox);
    } else {
        m_performedStateSynces = 0;
        m_syncingTimer->start();
    }
}

void KeepMailboxOpenTask::saveSyncStateIfPossible(TreeItemMailbox *mailbox)
{
    m_skippedStateSynces = 0;
    TreeItemMsgList *list = static_cast<TreeItemMsgList*>(mailbox->m_children[0]);
    if (list->fetched()) {
        mailbox->saveSyncStateAndUids(model);
    } else {
        list->setFetchStatus(Imap::Mailbox::TreeItem::LOADING);
    }
}

void KeepMailboxOpenTask::closeMailboxDestructively()
{
    tagClose = parser->close();
}

/** @short Is this task on its own keeping the connection busy?

Right now, only fetching of new arrivals is being done in the context of this KeepMailboxOpenTask task.
*/
bool KeepMailboxOpenTask::hasItsOwnActivity() const
{
    return !newArrivalsFetch.isEmpty();
}

/** @short Signal the final termination of this task */
void KeepMailboxOpenTask::finalizeTermination()
{
    if (!_finished) {
        _finished = true;
        emit completed(this);
    }
    CHECK_TASK_TREE;
}

}
}
