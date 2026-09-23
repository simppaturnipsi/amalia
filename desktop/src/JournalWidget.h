#pragma once

#include <QJsonObject>
#include <QSet>
#include <QWidget>

class ApiClient;
class JournalSocket;
class OfflineQueue;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTableWidget;

class JournalWidget final : public QWidget {
    Q_OBJECT
public:
    JournalWidget(ApiClient *api, OfflineQueue *queue, JournalSocket *socket, QWidget *parent = nullptr);

private:
    void reloadTasks();
    void switchTask();
    void addEntry(const QJsonObject &entry);
    void queueEntry();
    void syncNext();
    void createTask();
    void exportTask(const QString &format);
    void deleteTask();
    qint64 currentTaskId() const;

    ApiClient *m_api;
    OfflineQueue *m_queue;
    JournalSocket *m_socket;
    QComboBox *m_tasks;
    QTableWidget *m_entries;
    QPlainTextEdit *m_text;
    QLabel *m_status;
    QSet<int> m_sequences;
    bool m_syncing{false};
};
