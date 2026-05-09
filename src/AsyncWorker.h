#pragma once
#include <QObject>
#include <QThreadPool>
#include <QRunnable>
#include <QAtomicInt>
#include <functional>
#include <memory>

// Phase 3: Core Architecture Refactor
// AsyncWorker provides a thread pool with configurable concurrency,
// exponential backoff retry, and progress reporting.
// ALL network requests, file I/O, and heavy parsing go through this.

class AsyncTask : public QRunnable {
public:
    using TaskFunc = std::function<void()>;

    explicit AsyncTask(TaskFunc func) : m_func(std::move(func)) {}
    void run() override {
        if (m_func) m_func();
    }

private:
    TaskFunc m_func;
};

class AsyncWorker : public QObject {
    Q_OBJECT
public:
    static AsyncWorker& instance();

    // Submit a task to the thread pool with automatic priority
    void submit(std::function<void()> task, int priority = 0);

    // Submit a batch of tasks (for parallel downloads/searches)
    void submitBatch(const QVector<std::function<void()>>& tasks, int maxConcurrent = 5);

    // Set max thread count dynamically
    void setMaxThreads(int count);

    // Get current thread count
    int maxThreads() const;

    // Exponential backoff retry: retries the task up to `maxRetries` times
    // with increasing delay if it returns false.
    static void retryWithBackoff(
        std::function<bool()> task,
        int maxRetries = 3,
        int baseDelayMs = 1000
    );

    // Wait for all tasks to complete (blocks calling thread)
    void waitForAll();

signals:
    void allTasksFinished();
    void taskStarted(const QString& description);
    void taskProgress(const QString& id, int percent);
    void taskFinished(const QString& id, bool success, const QString& error);

private:
    AsyncWorker();
    ~AsyncWorker() = default;
    AsyncWorker(const AsyncWorker&) = delete;
    AsyncWorker& operator=(const AsyncWorker&) = delete;

    QThreadPool* m_pool;
    QAtomicInt m_activeTasks{0};
};