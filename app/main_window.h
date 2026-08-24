#pragma once

#include "app_translator.h"
#include "nexpdf/document_session.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSpinBox;
class QPushButton;
class PdfCanvas;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void buildToolPanel();
    void retranslateUi();
    void openPath(const QString &path, const QString &password = {});
    void chooseOpen();
    void saveAs(nexpdf::EncryptionAlgorithm mode = nexpdf::EncryptionAlgorithm::Keep);
    void createEncryptedCopy();
    void createDecryptedCopy();
    void applyPageOperation(nexpdf::EditKind kind, int rotation = 0);
    void movePage(int delta);
    void importPages();
    void addTextObject();
    void addImageObject();
    void addTextWatermark();
    void addImageWatermark();
    bool applyWatermarkOptions(nexpdf::WatermarkSpec &spec);
    void removeSelectedWatermarks();
    void showError(const nexpdf::OperationError &error);
    bool confirmDiscard();
    void setChinese(bool enabled);
    void resetThumbnails();
    void requestVisibleThumbnails();
    void acceptThumbnailRender(const nexpdf::RenderResult &result);

    nexpdf::DocumentSession session_;
    AppTranslator chineseTranslator_;
    PdfCanvas *canvas_ = nullptr;
    QScrollArea *scrollArea_ = nullptr;
    QListWidget *thumbnailList_ = nullptr;
    QListWidget *candidateList_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QLineEdit *watermarkText_ = nullptr;
    QLineEdit *watermarkPages_ = nullptr;
    QCheckBox *currentPageWatermark_ = nullptr;
    QComboBox *watermarkLayer_ = nullptr;
    QComboBox *watermarkPosition_ = nullptr;
    QDoubleSpinBox *watermarkOpacity_ = nullptr;
    QDoubleSpinBox *watermarkRotation_ = nullptr;
    QDoubleSpinBox *watermarkScale_ = nullptr;
    QLabel *pageLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *watermarkTextLabel_ = nullptr;
    QLabel *watermarkOpacityLabel_ = nullptr;
    QLabel *watermarkRotationLabel_ = nullptr;
    QLabel *watermarkScaleLabel_ = nullptr;
    QLabel *watermarkPagesLabel_ = nullptr;
    QLabel *watermarkLayerLabel_ = nullptr;
    QLabel *watermarkPositionLabel_ = nullptr;
    QLabel *watermarkWarningLabel_ = nullptr;
    QDockWidget *watermarkDock_ = nullptr;
    QPushButton *addTextWatermarkButton_ = nullptr;
    QPushButton *addImageWatermarkButton_ = nullptr;
    QPushButton *scanWatermarkButton_ = nullptr;
    QPushButton *removeWatermarkButton_ = nullptr;

    QAction *openAction_ = nullptr;
    QAction *saveAsAction_ = nullptr;
    QAction *encryptAction_ = nullptr;
    QAction *decryptAction_ = nullptr;
    QAction *closeAction_ = nullptr;
    QAction *exitAction_ = nullptr;
    QAction *undoAction_ = nullptr;
    QAction *redoAction_ = nullptr;
    QAction *insertPageAction_ = nullptr;
    QAction *importPagesAction_ = nullptr;
    QAction *deletePageAction_ = nullptr;
    QAction *movePageUpAction_ = nullptr;
    QAction *movePageDownAction_ = nullptr;
    QAction *rotateLeftAction_ = nullptr;
    QAction *rotateRightAction_ = nullptr;
    QAction *zoomInAction_ = nullptr;
    QAction *zoomOutAction_ = nullptr;
    QAction *actualSizeAction_ = nullptr;
    QAction *previousPageAction_ = nullptr;
    QAction *nextPageAction_ = nullptr;
    QAction *addTextAction_ = nullptr;
    QAction *addImageAction_ = nullptr;
    QAction *highlightAction_ = nullptr;
    QAction *underlineAction_ = nullptr;
    QAction *strikeOutAction_ = nullptr;
    QAction *rectangleAction_ = nullptr;
    QAction *ellipseAction_ = nullptr;
    QAction *inkAction_ = nullptr;
    QAction *moveObjectAction_ = nullptr;
    QAction *resizeObjectAction_ = nullptr;
    QAction *deleteObjectAction_ = nullptr;
    QAction *redactionPreviewAction_ = nullptr;
    QActionGroup *selectionActionGroup_ = nullptr;
    QAction *applyRedactionsAction_ = nullptr;
    QAction *englishAction_ = nullptr;
    QAction *chineseAction_ = nullptr;
    QAction *aboutAction_ = nullptr;

    QString currentPath_;
    int pageCount_ = 0;
    quint64 revision_ = 0;
    quint64 nextThumbnailRequestId_ = (quint64{1} << 63);
    QHash<quint64, int> thumbnailRequests_;
    QSet<int> pendingThumbnailPages_;
    QRectF pendingObjectBounds_;
    int pendingObjectPage_ = -1;
    bool modified_ = false;
    bool signedDocument_ = false;
    bool chinese_ = false;
};
