#include "AsyncWorker.h"
#include <QThread>
#include <QTimer>
#include <QDebug>

AsyncWorker& AsyncWorker::instance() {
    static AsyncWorker inst;
    return inst;
}

AsyncWorker::AsyncWorker()
    : QObject(nullptr)
    , m_pool(QThreadPool::globalInstance())
{
    m_pool->setMaxThreadCount(QThread::idealThreadCount());
}

void AsyncWorker::submit(std::function<void()> task, int /*priority*/) {
    m_activeTasks.ref();
    auto* runnable = new AsyncTask([this, task = std::move(task)]() {
        task();
        if (m_activeTasks.deref() == 0) {
            // All tasks done - use queued connection to emit from main thread
            QMetaObject::invokeMethod(this, [this]() {
                emit allTasksFinished();
            }, Qt::QueuedConnection);
        }
    });
    runnable->setAutoDelete(true);
    m_pool->start(runnable);
}

void AsyncWorker::submitBatch(const QVector<std::function<void()>>& tasks, int maxConcurrent) {
    int oldMax = m_pool->maxThreadCount();
    m_pool->setMaxThreadCount(maxConcurrent);

    for (auto& task : tasks) {
        submit(std::move(task));
    }

    // Restore original max after all submitted
    m_pool->setMaxThreadCount(oldMax);
}

void AsyncWorker::setMaxThreads(int count) {
    m_pool->setMaxThreadCount(std::max(1, count));
}

int AsyncWorker::maxThreads() const {
    return m_pool->maxThreadCount();
}

void AsyncWorker::retryWithBackoff(
    std::function<bool()> task,
    int maxRetries,
    int baseDelayMs
) {
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (task()) return; // success
        if (attempt < maxRetries - 1) {
            // Exponential backoff: 1s, 2s, 4s, ...
            int delay = baseDelayMs * (1 << attempt);
            QThread::msleep(delay);
        }
    }
}

void AsyncWorker::waitForAll() {
    m_pool->waitForDone();
    emit allTasksFinished();
}