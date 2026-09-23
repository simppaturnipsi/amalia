#include "JournalWidget.h"
#include "ApiClient.h"
#include "JournalSocket.h"
#include "OfflineQueue.h"

#include <QComboBox>
#include <QHeaderView>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

JournalWidget::JournalWidget(ApiClient *api, OfflineQueue *queue, JournalSocket *socket, QWidget *parent)
    : QWidget(parent), m_api(api), m_queue(queue), m_socket(socket) {
    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    m_tasks = new QComboBox(this);
    auto *refresh = new QPushButton(tr("Päivitä"), this);
    auto *create = new QPushButton(tr("Uusi tehtävä"), this);
    auto *csv = new QPushButton(tr("CSV"), this);
    auto *pdf = new QPushButton(tr("PDF"), this);
    auto *remove = new QPushButton(tr("Poista tehtävä…"), this);
    toolbar->addWidget(new QLabel(tr("Tehtävä:"), this));
    toolbar->addWidget(m_tasks, 1);
    for (auto *button : {refresh, create, csv, pdf, remove}) toolbar->addWidget(button);
    layout->addLayout(toolbar);
    m_entries = new QTableWidget(0, 4, this);
    m_entries->setHorizontalHeaderLabels({tr("#"), tr("Aikaleima"), tr("Käyttäjä"), tr("Merkintä")});
    m_entries->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_entries->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entries->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_entries, 1);
    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(tr("Kirjoita uusi muuttumaton päiväkirjamerkintä…"));
    m_text->setMaximumBlockCount(1000);
    layout->addWidget(m_text);
    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(tr("Valmis"), this);
    auto *send = new QPushButton(tr("Lisää merkintä"), this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(send);
    layout->addLayout(bottom);

    connect(refresh, &QPushButton::clicked, this, &JournalWidget::reloadTasks);
    connect(create, &QPushButton::clicked, this, &JournalWidget::createTask);
    connect(csv, &QPushButton::clicked, this, [this] { exportTask(QStringLiteral("csv")); });
    connect(pdf, &QPushButton::clicked, this, [this] { exportTask(QStringLiteral("pdf")); });
    connect(remove, &QPushButton::clicked, this, &JournalWidget::deleteTask);
    connect(send, &QPushButton::clicked, this, &JournalWidget::queueEntry);
    connect(m_tasks, &QComboBox::currentIndexChanged, this, &JournalWidget::switchTask);
    connect(m_api, &ApiClient::tasksLoaded, this, [this](const QJsonArray &tasks) {
        const auto selected = currentTaskId();
        m_tasks->blockSignals(true);
        m_tasks->clear();
        for (const auto &value : tasks) {
            const auto task = value.toObject();
            m_tasks->addItem(QStringLiteral("%1 – %2").arg(task["task_number"].toString(), task["name"].toString()),
                             task["id"].toInteger());
        }
        const int index = m_tasks->findData(selected);
        if (index >= 0) m_tasks->setCurrentIndex(index);
        m_tasks->blockSignals(false);
        switchTask();
    });
    connect(m_api, &ApiClient::taskCreated, this, [this] { reloadTasks(); });
    connect(m_api, &ApiClient::entriesLoaded, this, [this](qint64 taskId, const QJsonArray &entries) {
        if (taskId != currentTaskId()) return;
        for (const auto &entry : entries) addEntry(entry.toObject());
    });
    connect(m_api, &ApiClient::entryAccepted, this, [this](const QString &key, const QJsonObject &entry) {
        m_queue->markSynced(key, entry["id"].toInteger());
        m_syncing = false;
        if (entry["task_id"].toInteger() == currentTaskId()) addEntry(entry);
        syncNext();
    });
    connect(m_api, &ApiClient::entryFailed, this, [this](const QString &key, const QString &error, bool retryable) {
        m_queue->markPending(key, error);
        m_syncing = false;
        m_status->setText(retryable ? tr("Offline: merkintä odottaa lähetystä") : error);
        if (retryable) QTimer::singleShot(5000, this, &JournalWidget::syncNext);
    });
    connect(m_queue, &OfflineQueue::countChanged, this, [this](int count) {
        if (count) m_status->setText(tr("%1 merkintää odottaa synkronointia").arg(count));
        else m_status->setText(tr("Kaikki merkinnät synkronoitu"));
    });
    connect(m_socket, &JournalSocket::entryReceived, this, &JournalWidget::addEntry);
    connect(m_socket, &JournalSocket::connectedChanged, this, [this](bool connected) {
        if (connected) { m_api->loadEntries(currentTaskId(), m_sequences.isEmpty() ? 0 : *std::max_element(m_sequences.begin(), m_sequences.end())); syncNext(); }
    });
    connect(m_api, &ApiClient::requestFailed, this, [this](const QString &error) { m_status->setText(error); });
    connect(m_api, &ApiClient::authenticationReady, this, [this] {
        reloadTasks();
        syncNext();
    });
    connect(m_api, &ApiClient::taskDeleted, this, [this] { reloadTasks(); });
    connect(m_api, &ApiClient::exportReady, this, [this](const QByteArray &content, const QString &type, const QString &) {
        const QString suffix = type.startsWith(QStringLiteral("application/pdf")) ? QStringLiteral("pdf") : QStringLiteral("csv");
        const QString path = QFileDialog::getSaveFileName(this, tr("Tallenna export"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/amalia-export.") + suffix);
        if (path.isEmpty()) return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(content) == content.size())
            m_status->setText(tr("Export tallennettu"));
        else QMessageBox::critical(this, tr("Export"), tr("Tiedostoa ei voitu tallentaa."));
    });
    reloadTasks();
    syncNext();
}

qint64 JournalWidget::currentTaskId() const { return m_tasks->currentData().toLongLong(); }
void JournalWidget::reloadTasks() { m_api->loadTasks(); }

void JournalWidget::switchTask() {
    m_entries->setRowCount(0);
    m_sequences.clear();
    const auto taskId = currentTaskId();
    if (taskId > 0) { m_api->loadEntries(taskId); m_socket->watchTask(taskId); }
    else m_socket->stop();
}

void JournalWidget::addEntry(const QJsonObject &entry) {
    const int sequence = entry["sequence_number"].toInt();
    if (sequence <= 0 || m_sequences.contains(sequence)) return;
    m_sequences.insert(sequence);
    int row = 0;
    while (row < m_entries->rowCount() && m_entries->item(row, 0)->text().toInt() < sequence) ++row;
    m_entries->insertRow(row);
    m_entries->setItem(row, 0, new QTableWidgetItem(QString::number(sequence)));
    m_entries->setItem(row, 1, new QTableWidgetItem(entry["server_timestamp"].toString()));
    m_entries->setItem(row, 2, new QTableWidgetItem(entry["display_name"].toString()));
    m_entries->setItem(row, 3, new QTableWidgetItem(entry["text"].toString()));
}

void JournalWidget::queueEntry() {
    const auto taskId = currentTaskId();
    const auto text = m_text->toPlainText().trimmed();
    if (taskId <= 0 || text.isEmpty()) return;
    if (text.size() > 10000) { QMessageBox::warning(this, tr("Merkintä"), tr("Merkintä on liian pitkä.")); return; }
    const auto key = m_queue->enqueue(taskId, text, qEnvironmentVariable("COMPUTERNAME", qEnvironmentVariable("HOSTNAME", "unknown")));
    if (key.isEmpty()) { QMessageBox::critical(this, tr("Offline-jono"), tr("Merkintää ei voitu tallentaa paikalliseen jonoon.")); return; }
    m_text->clear();
    syncNext();
}

void JournalWidget::syncNext() {
    if (m_syncing) return;
    const auto rows = m_queue->pending(1);
    if (rows.isEmpty()) return;
    m_syncing = true;
    m_queue->markSending(rows.first().idempotencyKey);
    m_api->appendEntry(rows.first().taskId, rows.first().payload);
}

void JournalWidget::createTask() {
    bool ok = false;
    const auto number = QInputDialog::getText(this, tr("Uusi tehtävä"), tr("Tehtävänumero:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || number.isEmpty()) return;
    const auto name = QInputDialog::getText(this, tr("Uusi tehtävä"), tr("Tehtävän nimi:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (ok && !name.isEmpty()) m_api->createTask(name, number);
}

void JournalWidget::exportTask(const QString &format) {
    if (currentTaskId() > 0) m_api->exportTask(currentTaskId(), format);
}

void JournalWidget::deleteTask() {
    if (currentTaskId() <= 0) return;
    if (QMessageBox::warning(this, tr("Poista tehtäväpäiväkirja"),
        tr("Poisto onnistuu vain palvelimella onnistuneen exportin jälkeen. Yksittäisiä rivejä ei poisteta. Jatketaanko?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        m_api->deleteTask(currentTaskId());
}
