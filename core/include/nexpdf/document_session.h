#pragma once

#include "nexpdf/types.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

namespace nexpdf {

class DocumentSession final : public QObject {
    Q_OBJECT

public:
    explicit DocumentSession(QObject *parent = nullptr);
    ~DocumentSession() override;

    DocumentSession(const DocumentSession &) = delete;
    DocumentSession &operator=(const DocumentSession &) = delete;

    void open(const QString &path, const OpenOptions &options = {});
    void close();
    void requestRender(const RenderRequest &request);
    void cancelRender(quint64 requestId);
    void search(const QString &text);
    void extractText(int pageIndex, const QRectF &bounds);
    void applyEdit(const EditOperation &operation);
    void addWatermark(const WatermarkSpec &spec);
    void scanWatermarks();
    void removeWatermarks(const QStringList &candidateIds);
    void saveAs(const QString &path, const SaveOptions &options = {});
    void undo();
    void redo();
    // Includes mutations queued on the document worker, before stateChanged is delivered.
    [[nodiscard]] bool hasUnsavedChanges() const;

signals:
    void opened(const nexpdf::DocumentInfo &info);
    void closed();
    void passwordRequired(const QString &path);
    void renderReady(const nexpdf::RenderResult &result);
    void searchFinished(const QVector<nexpdf::SearchHit> &hits);
    void textExtracted(int pageIndex, const QRectF &bounds, const QString &text);
    void watermarksScanned(const QVector<nexpdf::WatermarkCandidate> &candidates);
    void saved(const QString &path, const QString &fingerprint);
    void stateChanged(quint64 revision, bool modified, bool canUndo, bool canRedo);
    void pageCountChanged(int pageCount);
    void progressChanged(const QString &operation, int current, int total);
    void failed(const nexpdf::OperationError &error);

private:
    void enqueueMutation(std::function<void()> mutation);
    class Impl;
    std::atomic_uint pendingMutations_{0};
    std::unique_ptr<Impl> impl_;
    QObject *workerContext_ = nullptr;
    QThread documentThread_;
};

} // namespace nexpdf
