#include "main_window.h"

#include "app_icons.h"
#include "pdf_canvas.h"
#include "version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPolygonF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), chineseTranslator_(this)
{
    setAcceptDrops(true);
    resize(1320, 860);
    setWindowIcon(nexpdf::icons::applicationIcon());
    buildUi();
    buildMenus();
    buildToolPanel();

    connect(&session_, &nexpdf::DocumentSession::opened, this,
            [this](const nexpdf::DocumentInfo &info) {
        currentPath_ = info.path;
        pageCount_ = info.pageCount;
        revision_ = info.revision;
        signedDocument_ = info.signedDocument;
        pendingObjectBounds_ = {};
        pendingObjectPage_ = -1;
        modified_ = false;
        canvas_->setDocument(info);
        resetThumbnails();
        thumbnailList_->setCurrentRow(0);
        pageLabel_->setText(QStringLiteral("1 / %1").arg(pageCount_));
        setWindowTitle(QStringLiteral("%1 — nexPDF").arg(info.title));
        statusLabel_->setText(tr("Ready"));
        if (signedDocument_) {
            QMessageBox::warning(this, tr("Signed document"),
                tr("Editing this document invalidates its digital signatures. It must be saved to a new file."));
        }
    });
    connect(&session_, &nexpdf::DocumentSession::closed, this, [this] {
        currentPath_.clear();
        pageCount_ = 0;
        modified_ = false;
        canvas_->clearDocument();
        thumbnailList_->clear();
        thumbnailRequests_.clear();
        pendingThumbnailPages_.clear();
        candidateList_->clear();
        pendingObjectBounds_ = {};
        pendingObjectPage_ = -1;
        setWindowTitle(QStringLiteral("nexPDF"));
    });
    connect(&session_, &nexpdf::DocumentSession::passwordRequired, this,
            [this](const QString &path) {
        bool accepted = false;
        const QString password = QInputDialog::getText(this, tr("Password required"),
            tr("Enter the PDF password:"), QLineEdit::Password, {}, &accepted);
        if (accepted) {
            openPath(path, password);
        }
    });
    connect(&session_, &nexpdf::DocumentSession::failed,
            this, &MainWindow::showError);
    connect(&session_, &nexpdf::DocumentSession::saved, this, [this](const QString &path) {
        currentPath_ = path;
        modified_ = false;
        statusLabel_->setText(tr("Ready"));
    });
    connect(&session_, &nexpdf::DocumentSession::stateChanged, this,
            [this](const quint64 revision, const bool modified, const bool canUndo, const bool canRedo) {
        const bool changedRevision = revision_ != 0 && revision != revision_;
        revision_ = revision;
        modified_ = modified;
        undoAction_->setEnabled(canUndo);
        redoAction_->setEnabled(canRedo);
        if (changedRevision && pageCount_ > 0) {
            nexpdf::DocumentInfo info;
            info.path = currentPath_;
            info.title = QFileInfo(currentPath_).completeBaseName();
            info.pageCount = pageCount_;
            info.revision = revision_;
            canvas_->setDocument(info);
            resetThumbnails();
        }
        setWindowModified(modified_);
    });
    connect(&session_, &nexpdf::DocumentSession::pageCountChanged, this, [this](const int count) {
        pageCount_ = count;
        resetThumbnails();
    });
    connect(&session_, &nexpdf::DocumentSession::renderReady,
            this, &MainWindow::acceptThumbnailRender);
    connect(&session_, &nexpdf::DocumentSession::progressChanged, this,
            [this](const QString &operation, const int current, const int total) {
        statusLabel_->setText(QStringLiteral("%1 %2/%3").arg(operation).arg(current).arg(total));
    });
    connect(&session_, &nexpdf::DocumentSession::searchFinished, this,
            [this](const QVector<nexpdf::SearchHit> &hits) {
        statusLabel_->setText(QStringLiteral("%1: %2").arg(tr("Search")).arg(hits.size()));
        if (!hits.isEmpty()) {
            canvas_->goToPage(hits.first().pageIndex);
        }
    });
    connect(&session_, &nexpdf::DocumentSession::textExtracted, this,
            [this](const int, const QRectF &, const QString &text) {
        if (text.isEmpty()) {
            statusLabel_->setText(tr("No text in selection"));
            return;
        }
        QApplication::clipboard()->setText(text);
        statusLabel_->setText(tr("Selected text copied to clipboard"));
    });
    connect(&session_, &nexpdf::DocumentSession::watermarksScanned, this,
            [this](const QVector<nexpdf::WatermarkCandidate> &candidates) {
        candidateList_->clear();
        for (const auto &candidate : candidates) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 — %2 — %3%")
                    .arg(candidate.label)
                    .arg(candidate.pages.size())
                    .arg(qRound(candidate.confidence * 100.0)), candidateList_);
            item->setData(Qt::UserRole, candidate.id);
            item->setCheckState(Qt::Unchecked);
            if (candidate.safety == nexpdf::WatermarkRemovalSafety::Unsupported) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
        }
        if (candidates.isEmpty()) {
            QMessageBox::information(this, tr("Watermark"), tr("No watermark candidates were found."));
        }
    });
    connect(canvas_, &PdfCanvas::currentPageChanged, this, [this](const int page) {
        thumbnailList_->setCurrentRow(page);
        pageLabel_->setText(QStringLiteral("%1 / %2").arg(page + 1).arg(pageCount_));
    });
    connect(canvas_, &PdfCanvas::regionSelected, this,
            [this](const int page, const QRectF &bounds) {
        const QAction *tool = selectionActionGroup_->checkedAction();
        if (tool == nullptr) {
            session_.extractText(page, bounds);
            return;
        }
        const auto kind = static_cast<nexpdf::EditKind>(tool->data().toInt());
        if (kind == nexpdf::EditKind::AddInk) {
            return;
        }
        if (kind == nexpdf::EditKind::MoveObject
            || kind == nexpdf::EditKind::ResizeObject) {
            if (pendingObjectPage_ < 0) {
                pendingObjectPage_ = page;
                pendingObjectBounds_ = bounds;
                statusLabel_->setText(tr("Object selected. Drag the destination rectangle."));
                return;
            }
            if (pendingObjectPage_ != page) {
                pendingObjectPage_ = page;
                pendingObjectBounds_ = bounds;
                statusLabel_->setText(tr("Object selected. Drag the destination rectangle."));
                return;
            }
        }
        statusLabel_->setText(QStringLiteral("%1: %2 × %3")
            .arg(tool->text()).arg(qRound(bounds.width())).arg(qRound(bounds.height())));
        nexpdf::EditOperation operation;
        operation.kind = kind;
        operation.pageIndex = page;
        if (kind == nexpdf::EditKind::MoveObject
            || kind == nexpdf::EditKind::ResizeObject) {
            operation.sourceBounds = pendingObjectBounds_;
            pendingObjectBounds_ = {};
            pendingObjectPage_ = -1;
        }
        operation.bounds = bounds;
        operation.color = Qt::black;
        session_.applyEdit(operation);
    });
    connect(canvas_, &PdfCanvas::pathSelected, this,
            [this](const int page, const QVector<QPointF> &points) {
        const QAction *tool = selectionActionGroup_->checkedAction();
        if (tool == nullptr
            || static_cast<nexpdf::EditKind>(tool->data().toInt()) != nexpdf::EditKind::AddInk
            || points.size() < 2) {
            return;
        }
        nexpdf::EditOperation operation;
        operation.kind = nexpdf::EditKind::AddInk;
        operation.pageIndex = page;
        operation.points = points;
        operation.bounds = QPolygonF(points).boundingRect();
        operation.color = Qt::red;
        session_.applyEdit(operation);
    });

    QSettings settings;
    const QString language = settings.value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
    setChinese(language == QStringLiteral("zh")
        || (language == QStringLiteral("system") && QLocale::system().language() == QLocale::Chinese));
}

void MainWindow::openFile(const QString &path)
{
    if (!path.isEmpty() && confirmDiscard()) {
        openPath(path);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscard()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()
        && event->mimeData()->urls().first().toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->urls().isEmpty() && confirmDiscard()) {
        openPath(event->mimeData()->urls().first().toLocalFile());
        event->acceptProposedAction();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MainWindow::resetThumbnails()
{
    thumbnailRequests_.clear();
    pendingThumbnailPages_.clear();
    thumbnailList_->clear();
    for (int page = 0; page < pageCount_; ++page) {
        thumbnailList_->addItem(QString::number(page + 1));
    }
    if (pageCount_ > 0) thumbnailList_->setCurrentRow(std::min(canvas_->currentPage(), pageCount_ - 1));
    QTimer::singleShot(0, this, [this] { requestVisibleThumbnails(); });
}

void MainWindow::requestVisibleThumbnails()
{
    if (pageCount_ <= 0 || revision_ == 0) return;
    int first = thumbnailList_->indexAt(QPoint(2, 2)).row();
    int last = thumbnailList_->indexAt(
        QPoint(2, std::max(2, thumbnailList_->viewport()->height() - 2))).row();
    if (first < 0) first = 0;
    if (last < 0) last = std::min(pageCount_ - 1, first + 12);
    first = std::max(0, first - 3);
    last = std::min(pageCount_ - 1, last + 3);
    for (int page = first; page <= last; ++page) {
        QListWidgetItem *item = thumbnailList_->item(page);
        if (item == nullptr || !item->icon().isNull() || pendingThumbnailPages_.contains(page)) continue;
        const quint64 requestId = nextThumbnailRequestId_++;
        thumbnailRequests_.insert(requestId, page);
        pendingThumbnailPages_.insert(page);
        nexpdf::RenderRequest request;
        request.requestId = requestId;
        request.revision = revision_;
        request.pageIndex = page;
        request.scale = 0.14;
        request.priority = -5;
        session_.requestRender(request);
    }
}

void MainWindow::acceptThumbnailRender(const nexpdf::RenderResult &result)
{
    const auto found = thumbnailRequests_.find(result.requestId);
    if (found == thumbnailRequests_.end()) return;
    const int page = found.value();
    thumbnailRequests_.erase(found);
    pendingThumbnailPages_.remove(page);
    if (result.revision != revision_ || result.image.isNull()) return;
    if (QListWidgetItem *item = thumbnailList_->item(page)) {
        item->setIcon(QIcon(QPixmap::fromImage(result.image).scaled(
            thumbnailList_->iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
}

void MainWindow::buildUi()
{
    auto *splitter = new QSplitter(this);
    thumbnailList_ = new QListWidget(splitter);
    thumbnailList_->setMaximumWidth(110);
    thumbnailList_->setMinimumWidth(72);
    thumbnailList_->setIconSize(QSize(84, 116));
    thumbnailList_->setUniformItemSizes(true);
    thumbnailList_->setAccessibleName(tr("Page"));
    scrollArea_ = new QScrollArea(splitter);
    canvas_ = new PdfCanvas(&session_, scrollArea_);
    scrollArea_->setWidget(canvas_);
    scrollArea_->setWidgetResizable(false);
    scrollArea_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    splitter->addWidget(thumbnailList_);
    splitter->addWidget(scrollArea_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    connect(thumbnailList_, &QListWidget::currentRowChanged, canvas_, &PdfCanvas::goToPage);
    connect(thumbnailList_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] { requestVisibleThumbnails(); });

    statusLabel_ = new QLabel(tr("Ready"), this);
    pageLabel_ = new QLabel(QStringLiteral("0 / 0"), this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(pageLabel_);
}

void MainWindow::buildMenus()
{
    openAction_ = new QAction(this);
    saveAsAction_ = new QAction(this);
    encryptAction_ = new QAction(this);
    decryptAction_ = new QAction(this);
    closeAction_ = new QAction(this);
    exitAction_ = new QAction(this);
    undoAction_ = new QAction(this);
    redoAction_ = new QAction(this);
    insertPageAction_ = new QAction(this);
    importPagesAction_ = new QAction(this);
    deletePageAction_ = new QAction(this);
    movePageUpAction_ = new QAction(this);
    movePageDownAction_ = new QAction(this);
    rotateLeftAction_ = new QAction(this);
    rotateRightAction_ = new QAction(this);
    zoomInAction_ = new QAction(this);
    zoomOutAction_ = new QAction(this);
    actualSizeAction_ = new QAction(this);
    previousPageAction_ = new QAction(this);
    nextPageAction_ = new QAction(this);
    addTextAction_ = new QAction(this);
    addImageAction_ = new QAction(this);
    highlightAction_ = new QAction(this);
    underlineAction_ = new QAction(this);
    strikeOutAction_ = new QAction(this);
    rectangleAction_ = new QAction(this);
    ellipseAction_ = new QAction(this);
    inkAction_ = new QAction(this);
    moveObjectAction_ = new QAction(this);
    resizeObjectAction_ = new QAction(this);
    deleteObjectAction_ = new QAction(this);
    redactionPreviewAction_ = new QAction(this);
    applyRedactionsAction_ = new QAction(this);
    englishAction_ = new QAction(this);
    chineseAction_ = new QAction(this);
    aboutAction_ = new QAction(this);

    using nexpdf::icons::Kind;
    using nexpdf::icons::actionIcon;
    openAction_->setIcon(actionIcon(Kind::Open));
    saveAsAction_->setIcon(actionIcon(Kind::SaveAs));
    encryptAction_->setIcon(actionIcon(Kind::Encrypt));
    decryptAction_->setIcon(actionIcon(Kind::Decrypt));
    undoAction_->setIcon(actionIcon(Kind::Undo));
    redoAction_->setIcon(actionIcon(Kind::Redo));
    insertPageAction_->setIcon(actionIcon(Kind::InsertPage));
    importPagesAction_->setIcon(actionIcon(Kind::ImportPages));
    deletePageAction_->setIcon(actionIcon(Kind::DeletePage));
    movePageUpAction_->setIcon(actionIcon(Kind::PageUp));
    movePageDownAction_->setIcon(actionIcon(Kind::PageDown));
    rotateLeftAction_->setIcon(actionIcon(Kind::RotateLeft));
    rotateRightAction_->setIcon(actionIcon(Kind::RotateRight));
    zoomInAction_->setIcon(actionIcon(Kind::ZoomIn));
    zoomOutAction_->setIcon(actionIcon(Kind::ZoomOut));
    actualSizeAction_->setIcon(actionIcon(Kind::ActualSize));
    previousPageAction_->setIcon(actionIcon(Kind::PreviousPage));
    nextPageAction_->setIcon(actionIcon(Kind::NextPage));
    addTextAction_->setIcon(actionIcon(Kind::AddText));
    addImageAction_->setIcon(actionIcon(Kind::AddImage));
    highlightAction_->setIcon(actionIcon(Kind::Highlight));
    underlineAction_->setIcon(actionIcon(Kind::Underline));
    strikeOutAction_->setIcon(actionIcon(Kind::StrikeOut));
    rectangleAction_->setIcon(actionIcon(Kind::Rectangle));
    ellipseAction_->setIcon(actionIcon(Kind::Ellipse));
    inkAction_->setIcon(actionIcon(Kind::Ink));
    moveObjectAction_->setIcon(actionIcon(Kind::Move));
    resizeObjectAction_->setIcon(actionIcon(Kind::Resize));
    deleteObjectAction_->setIcon(actionIcon(Kind::DeleteObject));
    redactionPreviewAction_->setIcon(actionIcon(Kind::RedactionPreview));
    applyRedactionsAction_->setIcon(actionIcon(Kind::ApplyRedactions));

    openAction_->setShortcut(QKeySequence::Open);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    englishAction_->setCheckable(true);
    chineseAction_->setCheckable(true);
    selectionActionGroup_ = new QActionGroup(this);
    selectionActionGroup_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    const QList<QPair<QAction *, nexpdf::EditKind>> selectionActions = {
        {highlightAction_, nexpdf::EditKind::AddHighlight},
        {underlineAction_, nexpdf::EditKind::AddUnderline},
        {strikeOutAction_, nexpdf::EditKind::AddStrikeOut},
        {rectangleAction_, nexpdf::EditKind::AddRectangle},
        {ellipseAction_, nexpdf::EditKind::AddEllipse},
        {inkAction_, nexpdf::EditKind::AddInk},
        {moveObjectAction_, nexpdf::EditKind::MoveObject},
        {resizeObjectAction_, nexpdf::EditKind::ResizeObject},
        {deleteObjectAction_, nexpdf::EditKind::DeleteObject},
        {redactionPreviewAction_, nexpdf::EditKind::AddRedactionPreview}
    };
    for (const auto &[action, kind] : selectionActions) {
        action->setCheckable(true);
        action->setData(static_cast<int>(kind));
        selectionActionGroup_->addAction(action);
    }
    connect(selectionActionGroup_, &QActionGroup::triggered, this, [this] {
        pendingObjectBounds_ = {};
        pendingObjectPage_ = -1;
    });
    auto *languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);
    languageGroup->addAction(englishAction_);
    languageGroup->addAction(chineseAction_);

    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseOpen);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveAs(); });
    connect(encryptAction_, &QAction::triggered, this, &MainWindow::createEncryptedCopy);
    connect(decryptAction_, &QAction::triggered, this, &MainWindow::createDecryptedCopy);
    connect(closeAction_, &QAction::triggered, this, [this] { if (confirmDiscard()) session_.close(); });
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    connect(undoAction_, &QAction::triggered, &session_, &nexpdf::DocumentSession::undo);
    connect(redoAction_, &QAction::triggered, &session_, &nexpdf::DocumentSession::redo);
    connect(insertPageAction_, &QAction::triggered, this,
            [this] { applyPageOperation(nexpdf::EditKind::InsertBlankPage); });
    connect(importPagesAction_, &QAction::triggered, this, &MainWindow::importPages);
    connect(addTextAction_, &QAction::triggered, this, &MainWindow::addTextObject);
    connect(addImageAction_, &QAction::triggered, this, &MainWindow::addImageObject);
    connect(deletePageAction_, &QAction::triggered, this,
            [this] { applyPageOperation(nexpdf::EditKind::DeletePage); });
    connect(movePageUpAction_, &QAction::triggered, this, [this] { movePage(-1); });
    connect(movePageDownAction_, &QAction::triggered, this, [this] { movePage(1); });
    connect(rotateLeftAction_, &QAction::triggered, this,
            [this] { applyPageOperation(nexpdf::EditKind::RotatePage, 270); });
    connect(rotateRightAction_, &QAction::triggered, this,
            [this] { applyPageOperation(nexpdf::EditKind::RotatePage, 90); });
    connect(zoomInAction_, &QAction::triggered, this, [this] { canvas_->setZoom(canvas_->zoom() * 1.2); });
    connect(zoomOutAction_, &QAction::triggered, this, [this] { canvas_->setZoom(canvas_->zoom() / 1.2); });
    connect(actualSizeAction_, &QAction::triggered, this, [this] { canvas_->setZoom(1.0); });
    connect(previousPageAction_, &QAction::triggered, this,
            [this] { canvas_->goToPage(std::max(0, canvas_->currentPage() - 1)); });
    connect(nextPageAction_, &QAction::triggered, this,
            [this] { canvas_->goToPage(std::min(pageCount_ - 1, canvas_->currentPage() + 1)); });
    connect(applyRedactionsAction_, &QAction::triggered, this, [this] {
        if (QMessageBox::warning(this, tr("Permanent redaction"),
                tr("Applying redactions permanently removes overlapping content. Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            applyPageOperation(nexpdf::EditKind::ApplyRedactions);
        }
    });
    connect(englishAction_, &QAction::triggered, this, [this] { setChinese(false); });
    connect(chineseAction_, &QAction::triggered, this, [this] { setChinese(true); });
    connect(aboutAction_, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About nexPDF"),
            tr("nexPDF %1\nLocal PDF processing with no telemetry or document upload.\nLicensed under AGPL-3.0-or-later.")
                .arg(QString::fromLatin1(NEXPDF_VERSION)));
    });

    auto *fileMenu = menuBar()->addMenu(QString());
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    fileMenu->addActions({openAction_, saveAsAction_, encryptAction_, decryptAction_});
    fileMenu->addSeparator();
    fileMenu->addActions({closeAction_, exitAction_});
    auto *editMenu = menuBar()->addMenu(QString());
    editMenu->setObjectName(QStringLiteral("editMenu"));
    editMenu->addActions({undoAction_, redoAction_});
    editMenu->addSeparator();
    editMenu->addActions({importPagesAction_, insertPageAction_, deletePageAction_, movePageUpAction_,
                          movePageDownAction_, rotateLeftAction_, rotateRightAction_});
    editMenu->addSeparator();
    editMenu->addActions({addTextAction_, addImageAction_, highlightAction_, underlineAction_,
                          strikeOutAction_, rectangleAction_, ellipseAction_, inkAction_, moveObjectAction_,
                          resizeObjectAction_, deleteObjectAction_, redactionPreviewAction_, applyRedactionsAction_});
    auto *viewMenu = menuBar()->addMenu(QString());
    viewMenu->setObjectName(QStringLiteral("viewMenu"));
    viewMenu->addActions({zoomInAction_, zoomOutAction_, actualSizeAction_, previousPageAction_, nextPageAction_});
    auto *settingsMenu = menuBar()->addMenu(QString());
    settingsMenu->setObjectName(QStringLiteral("settingsMenu"));
    auto *languageMenu = settingsMenu->addMenu(QString());
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    languageMenu->addActions({englishAction_, chineseAction_});
    auto *helpMenu = menuBar()->addMenu(QString());
    helpMenu->setObjectName(QStringLiteral("helpMenu"));
    helpMenu->addAction(aboutAction_);

    auto *toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const auto addLabeledAction = [](QToolBar *target, QAction *action,
                                     const QString &objectName, const Qt::ToolButtonStyle style) {
        auto *button = new QToolButton(target);
        button->setObjectName(objectName);
        button->setDefaultAction(action);
        button->setToolButtonStyle(style);
        button->setAutoRaise(true);
        target->addWidget(button);
        return button;
    };
    addLabeledAction(toolbar, openAction_, QStringLiteral("openToolButton"), Qt::ToolButtonTextBesideIcon);
    addLabeledAction(toolbar, saveAsAction_, QStringLiteral("saveAsToolButton"), Qt::ToolButtonTextBesideIcon);
    addLabeledAction(toolbar, encryptAction_, QStringLiteral("encryptToolButton"), Qt::ToolButtonTextBesideIcon);
    addLabeledAction(toolbar, decryptAction_, QStringLiteral("decryptToolButton"), Qt::ToolButtonTextBesideIcon);
    toolbar->addSeparator();
    toolbar->addActions({undoAction_, redoAction_});
    toolbar->addSeparator();
    toolbar->addActions({previousPageAction_, nextPageAction_, zoomOutAction_, zoomInAction_, actualSizeAction_});
    toolbar->addSeparator();
    searchEdit_ = new QLineEdit(toolbar);
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setMaximumWidth(240);
    searchEdit_->addAction(nexpdf::icons::actionIcon(Kind::Search), QLineEdit::LeadingPosition);
    toolbar->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::returnPressed, this, [this] { session_.search(searchEdit_->text()); });

    addToolBarBreak(Qt::TopToolBarArea);
    auto *editToolbar = addToolBar(QStringLiteral("edit"));
    editToolbar->setObjectName(QStringLiteral("editToolbar"));
    editToolbar->setIconSize(QSize(24, 24));
    editToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addLabeledAction(editToolbar, importPagesAction_, QStringLiteral("importPagesToolButton"),
                     Qt::ToolButtonTextUnderIcon);
    editToolbar->addActions({insertPageAction_, deletePageAction_, movePageUpAction_, movePageDownAction_,
                             rotateLeftAction_, rotateRightAction_});
    editToolbar->addSeparator();
    addLabeledAction(editToolbar, addTextAction_, QStringLiteral("addTextToolButton"),
                     Qt::ToolButtonTextUnderIcon);
    addLabeledAction(editToolbar, addImageAction_, QStringLiteral("addImageToolButton"),
                     Qt::ToolButtonTextUnderIcon);
    editToolbar->addSeparator();
    editToolbar->addActions({highlightAction_, underlineAction_, strikeOutAction_, rectangleAction_,
                             ellipseAction_, inkAction_, moveObjectAction_, resizeObjectAction_,
                             deleteObjectAction_, redactionPreviewAction_});
    editToolbar->addSeparator();
    addLabeledAction(editToolbar, applyRedactionsAction_, QStringLiteral("applyRedactionsToolButton"),
                     Qt::ToolButtonTextUnderIcon);

    retranslateUi();
}

void MainWindow::buildToolPanel()
{
    watermarkDock_ = new QDockWidget(tr("Watermark"), this);
    watermarkDock_->setObjectName(QStringLiteral("watermarkDock"));
    auto *panel = new QWidget(watermarkDock_);
    auto *layout = new QVBoxLayout(panel);
    auto *form = new QFormLayout;
    watermarkText_ = new QLineEdit(panel);
    currentPageWatermark_ = new QCheckBox(tr("Current page only"), panel);
    watermarkOpacity_ = new QDoubleSpinBox(panel);
    watermarkOpacity_->setRange(0.05, 1.0);
    watermarkOpacity_->setSingleStep(0.05);
    watermarkOpacity_->setValue(0.25);
    watermarkRotation_ = new QDoubleSpinBox(panel);
    watermarkRotation_->setRange(-180.0, 180.0);
    watermarkRotation_->setValue(-35.0);
    watermarkScale_ = new QDoubleSpinBox(panel);
    watermarkScale_->setRange(0.02, 2.0);
    watermarkScale_->setSingleStep(0.05);
    watermarkScale_->setValue(0.45);
    watermarkPages_ = new QLineEdit(panel);
    watermarkPages_->setPlaceholderText(tr("Example: 1-3,5; empty means all"));
    watermarkLayer_ = new QComboBox(panel);
    watermarkLayer_->addItem(tr("Foreground"), static_cast<int>(nexpdf::WatermarkLayer::Foreground));
    watermarkLayer_->addItem(tr("Background"), static_cast<int>(nexpdf::WatermarkLayer::Background));
    watermarkPosition_ = new QComboBox(panel);
    const QList<QPair<QString, QPointF>> positions = {
        {tr("Top left"), QPointF(0.15, 0.15)},
        {tr("Top center"), QPointF(0.5, 0.15)},
        {tr("Top right"), QPointF(0.85, 0.15)},
        {tr("Center left"), QPointF(0.15, 0.5)},
        {tr("Center"), QPointF(0.5, 0.5)},
        {tr("Center right"), QPointF(0.85, 0.5)},
        {tr("Bottom left"), QPointF(0.15, 0.85)},
        {tr("Bottom center"), QPointF(0.5, 0.85)},
        {tr("Bottom right"), QPointF(0.85, 0.85)}
    };
    for (const auto &[label, point] : positions) watermarkPosition_->addItem(label, point);
    watermarkPosition_->setCurrentIndex(4);
    watermarkTextLabel_ = new QLabel(tr("Watermark text"), panel);
    watermarkOpacityLabel_ = new QLabel(tr("Opacity"), panel);
    watermarkRotationLabel_ = new QLabel(tr("Rotation"), panel);
    watermarkScaleLabel_ = new QLabel(tr("Scale"), panel);
    watermarkPagesLabel_ = new QLabel(tr("Page range"), panel);
    watermarkLayerLabel_ = new QLabel(tr("Layer"), panel);
    watermarkPositionLabel_ = new QLabel(tr("Position"), panel);
    form->addRow(watermarkTextLabel_, watermarkText_);
    form->addRow(watermarkOpacityLabel_, watermarkOpacity_);
    form->addRow(watermarkRotationLabel_, watermarkRotation_);
    form->addRow(watermarkScaleLabel_, watermarkScale_);
    form->addRow(watermarkPagesLabel_, watermarkPages_);
    form->addRow(watermarkLayerLabel_, watermarkLayer_);
    form->addRow(watermarkPositionLabel_, watermarkPosition_);
    layout->addLayout(form);
    layout->addWidget(currentPageWatermark_);
    addTextWatermarkButton_ = new QPushButton(tr("Add text watermark"), panel);
    addImageWatermarkButton_ = new QPushButton(tr("Add image watermark…"), panel);
    scanWatermarkButton_ = new QPushButton(tr("Scan watermarks"), panel);
    addTextWatermarkButton_->setObjectName(QStringLiteral("addTextWatermarkButton"));
    addImageWatermarkButton_->setObjectName(QStringLiteral("addImageWatermarkButton"));
    scanWatermarkButton_->setObjectName(QStringLiteral("scanWatermarkButton"));
    addTextWatermarkButton_->setIcon(nexpdf::icons::actionIcon(nexpdf::icons::Kind::TextWatermark));
    addImageWatermarkButton_->setIcon(nexpdf::icons::actionIcon(nexpdf::icons::Kind::ImageWatermark));
    scanWatermarkButton_->setIcon(nexpdf::icons::actionIcon(nexpdf::icons::Kind::ScanWatermark));
    watermarkWarningLabel_ = new QLabel(tr("Review every candidate before removal. External baked-in watermarks may not be detected safely."), panel);
    watermarkWarningLabel_->setWordWrap(true);
    candidateList_ = new QListWidget(panel);
    removeWatermarkButton_ = new QPushButton(tr("Remove selected"), panel);
    removeWatermarkButton_->setObjectName(QStringLiteral("removeWatermarkButton"));
    removeWatermarkButton_->setIcon(nexpdf::icons::actionIcon(nexpdf::icons::Kind::RemoveWatermark));
    layout->addWidget(addTextWatermarkButton_);
    layout->addWidget(addImageWatermarkButton_);
    layout->addWidget(scanWatermarkButton_);
    layout->addWidget(watermarkWarningLabel_);
    layout->addWidget(candidateList_, 1);
    layout->addWidget(removeWatermarkButton_);
    watermarkDock_->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, watermarkDock_);

    connect(addTextWatermarkButton_, &QPushButton::clicked, this, &MainWindow::addTextWatermark);
    connect(addImageWatermarkButton_, &QPushButton::clicked, this, &MainWindow::addImageWatermark);
    connect(scanWatermarkButton_, &QPushButton::clicked, &session_, &nexpdf::DocumentSession::scanWatermarks);
    connect(removeWatermarkButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedWatermarks);
}

void MainWindow::retranslateUi()
{
    openAction_->setText(tr("Open…"));
    saveAsAction_->setText(tr("Save As…"));
    encryptAction_->setText(tr("Create encrypted copy…"));
    decryptAction_->setText(tr("Create decrypted copy…"));
    closeAction_->setText(tr("Close"));
    exitAction_->setText(tr("Exit"));
    undoAction_->setText(tr("Undo"));
    redoAction_->setText(tr("Redo"));
    insertPageAction_->setText(tr("Insert blank page"));
    importPagesAction_->setText(tr("Import pages…"));
    deletePageAction_->setText(tr("Delete page"));
    movePageUpAction_->setText(tr("Move page up"));
    movePageDownAction_->setText(tr("Move page down"));
    rotateLeftAction_->setText(tr("Rotate left"));
    rotateRightAction_->setText(tr("Rotate right"));
    zoomInAction_->setText(tr("Zoom in"));
    zoomOutAction_->setText(tr("Zoom out"));
    actualSizeAction_->setText(tr("Actual size"));
    previousPageAction_->setText(tr("Previous page"));
    nextPageAction_->setText(tr("Next page"));
    addTextAction_->setText(tr("Add text…"));
    addImageAction_->setText(tr("Add image…"));
    highlightAction_->setText(tr("Highlight selection"));
    underlineAction_->setText(tr("Underline selection"));
    strikeOutAction_->setText(tr("Strike out selection"));
    rectangleAction_->setText(tr("Rectangle selection"));
    ellipseAction_->setText(tr("Ellipse selection"));
    inkAction_->setText(tr("Ink"));
    moveObjectAction_->setText(tr("Move object to selection"));
    resizeObjectAction_->setText(tr("Resize object to selection"));
    deleteObjectAction_->setText(tr("Delete object in selection"));
    redactionPreviewAction_->setText(tr("Redaction preview"));
    applyRedactionsAction_->setText(tr("Apply redactions"));
    englishAction_->setText(tr("English"));
    chineseAction_->setText(tr("简体中文"));
    aboutAction_->setText(tr("About nexPDF"));
    searchEdit_->setPlaceholderText(tr("Search"));
    const auto setActionHint = [](QAction *action, const QString &description) {
        QString tooltip = description;
        if (!action->shortcut().isEmpty()) {
            tooltip += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
        }
        action->setToolTip(tooltip);
        action->setStatusTip(description);
    };
    const QList<QAction *> selfDescribingActions = {
        openAction_, saveAsAction_, encryptAction_, decryptAction_, undoAction_, redoAction_,
        insertPageAction_, importPagesAction_, deletePageAction_, movePageUpAction_, movePageDownAction_,
        rotateLeftAction_, rotateRightAction_, zoomInAction_, zoomOutAction_, actualSizeAction_,
        previousPageAction_, nextPageAction_, addTextAction_, addImageAction_, highlightAction_,
        underlineAction_, strikeOutAction_, rectangleAction_, ellipseAction_, inkAction_, deleteObjectAction_
    };
    for (QAction *action : selfDescribingActions) setActionHint(action, action->text());
    setActionHint(moveObjectAction_, tr("Select an object, then drag its destination"));
    setActionHint(resizeObjectAction_, tr("Select an object, then drag its new bounds"));
    setActionHint(redactionPreviewAction_, tr("Mark content for permanent redaction"));
    setActionHint(applyRedactionsAction_, tr("Permanently remove marked content"));
    searchEdit_->setToolTip(tr("Search document text and press Enter"));
    if (QToolBar *toolbar = findChild<QToolBar *>(QStringLiteral("mainToolbar"))) {
        toolbar->setWindowTitle(tr("Main toolbar"));
    }
    if (QToolBar *toolbar = findChild<QToolBar *>(QStringLiteral("editToolbar"))) {
        toolbar->setWindowTitle(tr("Edit toolbar"));
    }
    const QList<QPair<QString, QString>> compactLabels = {
        {QStringLiteral("openToolButton"), tr("Open")},
        {QStringLiteral("saveAsToolButton"), tr("Save")},
        {QStringLiteral("encryptToolButton"), tr("Encrypt")},
        {QStringLiteral("decryptToolButton"), tr("Decrypt")}
    };
    for (const auto &[objectName, label] : compactLabels) {
        if (QToolButton *button = findChild<QToolButton *>(objectName)) button->setText(label);
    }
    if (statusLabel_ != nullptr
        && (statusLabel_->text() == QStringLiteral("Ready")
            || statusLabel_->text() == QStringLiteral("就绪"))) {
        statusLabel_->setText(tr("Ready"));
    }
    if (watermarkDock_ != nullptr) {
        watermarkDock_->setWindowTitle(tr("Watermark"));
        watermarkTextLabel_->setText(tr("Watermark text"));
        watermarkOpacityLabel_->setText(tr("Opacity"));
        watermarkRotationLabel_->setText(tr("Rotation"));
        watermarkScaleLabel_->setText(tr("Scale"));
        watermarkPagesLabel_->setText(tr("Page range"));
        watermarkLayerLabel_->setText(tr("Layer"));
        watermarkPositionLabel_->setText(tr("Position"));
        watermarkPages_->setPlaceholderText(tr("Example: 1-3,5; empty means all"));
        watermarkLayer_->setItemText(0, tr("Foreground"));
        watermarkLayer_->setItemText(1, tr("Background"));
        const QStringList positionLabels = {
            tr("Top left"), tr("Top center"), tr("Top right"),
            tr("Center left"), tr("Center"), tr("Center right"),
            tr("Bottom left"), tr("Bottom center"), tr("Bottom right")
        };
        for (int index = 0; index < positionLabels.size(); ++index) {
            watermarkPosition_->setItemText(index, positionLabels[index]);
        }
        currentPageWatermark_->setText(tr("Current page only"));
        addTextWatermarkButton_->setText(tr("Add text watermark"));
        addImageWatermarkButton_->setText(tr("Add image watermark…"));
        scanWatermarkButton_->setText(tr("Scan watermarks"));
        removeWatermarkButton_->setText(tr("Remove selected"));
        addTextWatermarkButton_->setToolTip(tr("Add a removable text watermark"));
        addImageWatermarkButton_->setToolTip(tr("Add a removable image watermark"));
        scanWatermarkButton_->setToolTip(tr("Find removable watermark candidates"));
        removeWatermarkButton_->setToolTip(tr("Remove only the checked candidates"));
        watermarkWarningLabel_->setText(tr("Review every candidate before removal. External baked-in watermarks may not be detected safely."));
    }
    for (QMenu *menu : menuBar()->findChildren<QMenu *>()) {
        if (menu->objectName() == QStringLiteral("fileMenu")) menu->setTitle(tr("File"));
        else if (menu->objectName() == QStringLiteral("editMenu")) menu->setTitle(tr("Edit"));
        else if (menu->objectName() == QStringLiteral("viewMenu")) menu->setTitle(tr("View"));
        else if (menu->objectName() == QStringLiteral("settingsMenu")) menu->setTitle(tr("Settings"));
        else if (menu->objectName() == QStringLiteral("languageMenu")) menu->setTitle(tr("Language"));
        else if (menu->objectName() == QStringLiteral("helpMenu")) menu->setTitle(tr("Help"));
    }
}

void MainWindow::openPath(const QString &path, const QString &password)
{
    nexpdf::OpenOptions options;
    options.password = password;
    session_.open(path, options);
    statusLabel_->setText(tr("Open PDF"));
}

void MainWindow::chooseOpen()
{
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open PDF"), {}, tr("PDF files (*.pdf)"));
    if (!path.isEmpty()) openPath(path);
}

void MainWindow::saveAs(const nexpdf::EncryptionAlgorithm mode)
{
    if (currentPath_.isEmpty()) {
        showError({nexpdf::ErrorCode::InvalidArgument, tr("Open a PDF first."), {}, QStringLiteral("save")});
        return;
    }
    QString suggested = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/')
        + QFileInfo(currentPath_).completeBaseName() + QStringLiteral("-copy.pdf");
    const QString path = QFileDialog::getSaveFileName(this, tr("Save PDF"), suggested, tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;
    nexpdf::SaveOptions options;
    options.encryption.algorithm = mode;
    if (QFileInfo::exists(path)) {
        options.overwriteConfirmed = QMessageBox::question(this, tr("Replace file?"),
            tr("The destination exists. Replace it?")) == QMessageBox::Yes;
        if (!options.overwriteConfirmed) return;
    }
    session_.saveAs(path, options);
}

void MainWindow::createEncryptedCopy()
{
    if (currentPath_.isEmpty()) {
        showError({nexpdf::ErrorCode::InvalidArgument, tr("Open a PDF first."), {}, QStringLiteral("encrypt")});
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create encrypted copy"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QLineEdit userPassword;
    QLineEdit ownerPassword;
    QLineEdit confirmPassword;
    QLineEdit confirmOwnerPassword;
    userPassword.setEchoMode(QLineEdit::Password);
    ownerPassword.setEchoMode(QLineEdit::Password);
    confirmPassword.setEchoMode(QLineEdit::Password);
    confirmOwnerPassword.setEchoMode(QLineEdit::Password);
    QComboBox algorithm;
    algorithm.addItem(tr("AES-256"), static_cast<int>(nexpdf::EncryptionAlgorithm::Aes256));
    algorithm.addItem(tr("AES-128 compatibility"), static_cast<int>(nexpdf::EncryptionAlgorithm::Aes128));
    form->addRow(tr("User password"), &userPassword);
    form->addRow(tr("Owner password"), &ownerPassword);
    form->addRow(tr("Confirm user password"), &confirmPassword);
    form->addRow(tr("Confirm owner password"), &confirmOwnerPassword);
    form->addRow(tr("Create encrypted copy"), &algorithm);
    layout->addLayout(form);
    auto *passwordNotice = new QLabel(tr(
        "Use non-empty, long, unique passwords. The user password protects opening; "
        "the owner password controls unrestricted access."));
    passwordNotice->setWordWrap(true);
    layout->addWidget(passwordNotice);
    QCheckBox allowPrint(tr("Allow printing"));
    QCheckBox allowCopy(tr("Allow copying"));
    QCheckBox allowAnnotations(tr("Allow annotations"));
    QCheckBox allowForms(tr("Allow form filling"));
    QCheckBox allowAssembly(tr("Allow page assembly"));
    QCheckBox allowModification(tr("Allow modification"));
    QCheckBox allowAccessibility(tr("Allow accessibility extraction"));
    QCheckBox allowHighQualityPrint(tr("Allow high-quality printing"));
    for (QCheckBox *permission : {&allowPrint, &allowCopy, &allowAnnotations, &allowForms,
                                 &allowAssembly, &allowModification, &allowAccessibility,
                                 &allowHighQualityPrint}) {
        permission->setChecked(true);
        layout->addWidget(permission);
    }
    auto *notice = new QLabel(tr("PDF permissions rely on reader cooperation and are not a substitute for access control."));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (userPassword.text() != confirmPassword.text()) {
        QMessageBox::warning(this, tr("Error"), tr("Passwords do not match"));
        return;
    }
    if (ownerPassword.text() != confirmOwnerPassword.text()) {
        QMessageBox::warning(this, tr("Error"), tr("Owner passwords do not match"));
        return;
    }
    if (userPassword.text().isEmpty()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("A non-empty user password is required for confidential encryption."));
        return;
    }
    if (ownerPassword.text().isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("An owner password is required."));
        return;
    }
    if (userPassword.text() == ownerPassword.text()
        && QMessageBox::warning(this, tr("Same passwords"),
            tr("Some PDF readers try the user password first and may not grant owner access when both passwords are the same. AES confidentiality is unchanged, but permission behavior can be unreliable. Continue?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save PDF"), {}, tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;
    nexpdf::SaveOptions options;
    options.encryption.algorithm = static_cast<nexpdf::EncryptionAlgorithm>(algorithm.currentData().toInt());
    options.encryption.userPassword = userPassword.text();
    options.encryption.ownerPassword = ownerPassword.text();
    options.encryption.permissions.print = allowPrint.isChecked();
    options.encryption.permissions.copy = allowCopy.isChecked();
    options.encryption.permissions.annotate = allowAnnotations.isChecked();
    options.encryption.permissions.fillForms = allowForms.isChecked();
    options.encryption.permissions.assemble = allowAssembly.isChecked();
    options.encryption.permissions.modify = allowModification.isChecked();
    options.encryption.permissions.accessibility = allowAccessibility.isChecked();
    options.encryption.permissions.highQualityPrint = allowHighQualityPrint.isChecked();
    options.overwriteConfirmed = !QFileInfo::exists(path)
        || QMessageBox::question(this, tr("Replace file?"), tr("The destination exists. Replace it?")) == QMessageBox::Yes;
    if (options.overwriteConfirmed) {
        session_.saveAs(path, options);
        userPassword.clear();
        ownerPassword.clear();
        confirmPassword.clear();
        confirmOwnerPassword.clear();
    }
}

void MainWindow::createDecryptedCopy()
{
    saveAs(nexpdf::EncryptionAlgorithm::None);
}

void MainWindow::applyPageOperation(const nexpdf::EditKind kind, const int rotation)
{
    if (pageCount_ <= 0) return;
    nexpdf::EditOperation operation;
    operation.kind = kind;
    operation.pageIndex = canvas_->currentPage();
    operation.rotation = rotation;
    session_.applyEdit(operation);
}

void MainWindow::movePage(const int delta)
{
    if (pageCount_ <= 1) return;
    const int source = canvas_->currentPage();
    const int destination = std::clamp(source + delta, 0, pageCount_ - 1);
    if (source == destination) return;
    nexpdf::EditOperation operation;
    operation.kind = nexpdf::EditKind::MovePage;
    operation.pageIndex = source;
    operation.destinationIndex = destination;
    session_.applyEdit(operation);
}

void MainWindow::importPages()
{
    if (pageCount_ <= 0) return;
    const QString source = QFileDialog::getOpenFileName(this, tr("Import pages…"), {}, tr("PDF files (*.pdf)"));
    if (source.isEmpty()) return;
    bool accepted = false;
    const QString password = QInputDialog::getText(this, tr("Password required"),
        tr("Enter the PDF password:") + QStringLiteral(" (optional)"),
        QLineEdit::Password, {}, &accepted);
    if (!accepted) return;
    nexpdf::EditOperation operation;
    operation.kind = nexpdf::EditKind::ImportPages;
    operation.pageIndex = std::min(pageCount_, canvas_->currentPage() + 1);
    operation.sourcePath = source;
    operation.sourcePassword = password;
    session_.applyEdit(operation);
}

void MainWindow::addTextObject()
{
    if (pageCount_ <= 0) return;
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("Add text…"),
        tr("Add text…"), {}, &accepted);
    if (!accepted || text.trimmed().isEmpty()) return;
    nexpdf::EditOperation operation;
    operation.kind = nexpdf::EditKind::AddText;
    operation.pageIndex = canvas_->currentPage();
    operation.bounds = QRectF(54, 54, 360, 90);
    operation.text = text;
    operation.fontSize = 16;
    operation.color = Qt::black;
    session_.applyEdit(operation);
}

void MainWindow::addImageObject()
{
    if (pageCount_ <= 0) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Add image…"), {},
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (path.isEmpty()) return;
    nexpdf::EditOperation operation;
    operation.kind = nexpdf::EditKind::AddImage;
    operation.pageIndex = canvas_->currentPage();
    operation.bounds = QRectF(54, 54, 300, 220);
    operation.imagePath = path;
    session_.applyEdit(operation);
}

void MainWindow::addTextWatermark()
{
    nexpdf::WatermarkSpec spec;
    spec.kind = nexpdf::WatermarkKind::Text;
    spec.text = watermarkText_->text();
    if (!applyWatermarkOptions(spec)) return;
    session_.addWatermark(spec);
}

void MainWindow::addImageWatermark()
{
    if (pageCount_ <= 0) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Add image watermark…"), {},
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (path.isEmpty()) return;
    nexpdf::WatermarkSpec spec;
    spec.kind = nexpdf::WatermarkKind::Image;
    spec.imagePath = path;
    if (!applyWatermarkOptions(spec)) return;
    session_.addWatermark(spec);
}

bool MainWindow::applyWatermarkOptions(nexpdf::WatermarkSpec &spec)
{
    spec.opacity = watermarkOpacity_->value();
    spec.rotation = watermarkRotation_->value();
    spec.scale = watermarkScale_->value();
    spec.layer = static_cast<nexpdf::WatermarkLayer>(watermarkLayer_->currentData().toInt());
    spec.position = watermarkPosition_->currentData().toPointF();
    if (currentPageWatermark_->isChecked()) {
        spec.pages = {canvas_->currentPage()};
        return true;
    }
    const QString range = watermarkPages_->text().trimmed();
    if (range.isEmpty()) return true;

    QSet<int> pages;
    for (const QString &part : range.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString token = part.trimmed();
        const QStringList endpoints = token.split(QLatin1Char('-'));
        bool startOk = false;
        bool endOk = false;
        const int start = endpoints.value(0).trimmed().toInt(&startOk);
        const int end = endpoints.size() == 1
            ? start : endpoints.value(1).trimmed().toInt(&endOk);
        if (endpoints.size() == 1) endOk = startOk;
        if (endpoints.size() > 2 || !startOk || !endOk || start < 1 || end < start || end > pageCount_) {
            QMessageBox::warning(this, tr("Error"), tr("The watermark page range is invalid."));
            return false;
        }
        for (int page = start; page <= end; ++page) pages.insert(page - 1);
    }
    spec.pages = pages.values();
    std::sort(spec.pages.begin(), spec.pages.end());
    return !spec.pages.isEmpty();
}

void MainWindow::removeSelectedWatermarks()
{
    QStringList ids;
    for (int index = 0; index < candidateList_->count(); ++index) {
        const QListWidgetItem *item = candidateList_->item(index);
        if (item->checkState() == Qt::Checked) ids.append(item->data(Qt::UserRole).toString());
    }
    if (!ids.isEmpty()) session_.removeWatermarks(ids);
}

void MainWindow::showError(const nexpdf::OperationError &error)
{
    QMessageBox message(QMessageBox::Critical, tr("Error"), error.message, QMessageBox::Ok, this);
    message.setDetailedText(error.detail);
    message.exec();
    statusLabel_->setText(error.message);
}

bool MainWindow::confirmDiscard()
{
    if (!modified_) return true;
    const auto answer = QMessageBox::warning(this, tr("Save PDF"),
        tr("The document has unsaved changes."), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        saveAs();
        return false;
    }
    return true;
}

void MainWindow::setChinese(const bool enabled)
{
    if (chinese_ == enabled && ((enabled && chineseAction_->isChecked()) || (!enabled && englishAction_->isChecked()))) {
        return;
    }
    if (chinese_) qApp->removeTranslator(&chineseTranslator_);
    chinese_ = enabled;
    if (chinese_) qApp->installTranslator(&chineseTranslator_);
    chineseAction_->setChecked(chinese_);
    englishAction_->setChecked(!chinese_);
    QSettings().setValue(QStringLiteral("ui/language"), chinese_ ? QStringLiteral("zh") : QStringLiteral("en"));
    retranslateUi();
}
