#include "MainWindow.h"
#include "../core/DocumentParser.h"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QAbstractAnimation>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDropEvent>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QVector>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTextCursor>
#include <QMutexLocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QFontMetrics>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QThreadPool>
#include <QThread>

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <map>

#include "GraphWidget.h"
#include "LanguageManager.h"
#include "RedundancyReportDialog.h"
#include "SettingsDialog.h"

#include <QtGlobal>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QSettings>

static QString extractFirstBalancedJsonObject(const QString &rawIn);
static QString extractFirstBalancedJsonArray(const QString &rawIn);
static QVector<int> parseSemanticRetrieverIdList(const QString &raw);

/// Small arc spinner next to status text (matches file-list / folder-tree arc style).
class BusyChip final : public QWidget {
public:
    explicit BusyChip(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setPhase(int phase)
    {
        m_phase = phase & 3;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rf = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        QColor accent = palette().link().color();
        if (!isEnabled())
            accent.setAlpha(120);
        QPen pen(accent, 2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const int span = 90 * 16;
        const int start = -m_phase * 90 * 16;
        p.drawArc(rf, start - span, span);
    }

private:
    int m_phase = 0;
};

class AiTagDropTreeWidget final : public QTreeWidget {
    MainWindow *m_mainWindow = nullptr;

public:
    explicit AiTagDropTreeWidget(MainWindow *mw, QWidget *parent = nullptr) : QTreeWidget(parent), m_mainWindow(mw)
    {
        setColumnCount(1);
        setHeaderHidden(true);
        setRootIsDecorated(true);
        setUniformRowHeights(true);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setContextMenuPolicy(Qt::CustomContextMenu);
    }

protected:
    void dropEvent(QDropEvent *event) override
    {
        QTreeWidget::dropEvent(event);
        if (m_mainWindow)
            QTimer::singleShot(0, m_mainWindow, &MainWindow::syncAiTagHierarchyFromTree);
    }
};

namespace {

const QSet<QString> &plainTextFileSuffixes()
{
    static const QSet<QString> k = {QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("cpp"),
                                    QStringLiteral("h"),    QStringLiteral("c"),    QStringLiteral("hpp"),
                                    QStringLiteral("json"), QStringLiteral("xml"),  QStringLiteral("csv"),
                                    QStringLiteral("log"),  QStringLiteral("yaml"), QStringLiteral("yml"),
                                    QStringLiteral("py"),   QStringLiteral("js"),   QStringLiteral("ts")};
    return k;
}

/// PDF + Office Open XML / macro / template + OpenDocument + EPUB (ZIP-backed text extraction).
const QSet<QString> &zipOrPdfTextExtractSuffixes()
{
    static const QSet<QString> k = {QStringLiteral("pdf"),
                                    QStringLiteral("docx"),
                                    QStringLiteral("docm"),
                                    QStringLiteral("dotx"),
                                    QStringLiteral("dotm"),
                                    QStringLiteral("xlsx"),
                                    QStringLiteral("xlsm"),
                                    QStringLiteral("xltx"),
                                    QStringLiteral("xltm"),
                                    QStringLiteral("pptx"),
                                    QStringLiteral("pptm"),
                                    QStringLiteral("potx"),
                                    QStringLiteral("potm"),
                                    QStringLiteral("odt"),
                                    QStringLiteral("ods"),
                                    QStringLiteral("odp"),
                                    QStringLiteral("epub")};
    return k;
}

const QSet<QString> &officeZipPreviewSuffixes()
{
    static const QSet<QString> k = {QStringLiteral("docx"), QStringLiteral("docm"), QStringLiteral("dotx"),
                                    QStringLiteral("dotm"), QStringLiteral("xlsx"), QStringLiteral("xlsm"),
                                    QStringLiteral("xltx"), QStringLiteral("xltm"), QStringLiteral("pptx"),
                                    QStringLiteral("pptm"), QStringLiteral("potx"), QStringLiteral("potm"),
                                    QStringLiteral("odt"),  QStringLiteral("ods"),  QStringLiteral("odp"),
                                    QStringLiteral("epub")};
    return k;
}

} // namespace

namespace {

static bool sfPathHasQueuedUnder(const QString &dirRaw, const QStringList &queuedFiles)
{
    const QString d = QDir::cleanPath(dirRaw);
    if (d.isEmpty()) return false;
    const QString dPref = d + QLatin1Char('/');
    for (const QString &f0 : queuedFiles) {
        const QString f = QDir::cleanPath(f0);
        if (f.isEmpty()) continue;
        if (f == d || f.startsWith(dPref)) return true;
    }
    return false;
}

/// state: 0 none, 1 hollow, 2 arc (animPhase 0–3), 3 solid
static void sfPaintAnalysisBadge(QPainter *painter, const QRect &r, int state, int animPhase, const QColor &accent)
{
    if (state <= 0) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rf = QRectF(r).adjusted(1.0, 1.0, -1.0, -1.0);
    if (state == 3) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent);
        painter->drawEllipse(rf);
    } else if (state == 1) {
        QPen pen(accent, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rf);
    } else if (state == 2) {
        QPen pen(accent, 2.2);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const int span = 90 * 16;
        const int start = -(animPhase & 3) * 90 * 16;
        painter->drawArc(rf, start - span, span);
    }
    painter->restore();
}

static int sfFolderRowBadgeState(const QString &rowPath, const QTreeView *tv)
{
    if (!tv || rowPath.isEmpty()) return 0;
    const QString browsing = QDir::cleanPath(tv->property("sfBrowsingDir").toString());
    const QString analyzingDir = QDir::cleanPath(tv->property("sfAnalyzingDir").toString());
    const bool batch = tv->property("sfBatchActive").toInt() != 0;
    const bool watcherBusy = tv->property("sfWatcherBusy").toInt() != 0;
    const QStringList pend = tv->property("sfPendingPaths").toStringList();

    if (!analyzingDir.isEmpty() && rowPath == analyzingDir && watcherBusy)
        return 2;
    if (batch && sfPathHasQueuedUnder(rowPath, pend))
        return 1;
    if (batch && rowPath == browsing && !sfPathHasQueuedUnder(rowPath, pend))
        return 3;
    return 0;
}

} // namespace

class FileItemDelegate : public QStyledItemDelegate {
public:
    /// 0 none, 1 hollow (pending), 2 analyzing (arc; phase on list sfProgressPhase), 3 solid (done)
    static constexpr int kSpinRole = Qt::UserRole + 16;

    explicit FileItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        QWidget *w = const_cast<QWidget *>(opt.widget);
        QStyle *style = w ? w->style() : QApplication::style();

        const QString name = index.data(Qt::DisplayRole).toString();
        const QString path = index.data(Qt::UserRole + 1).toString();
        const int prog = index.data(kSpinRole).toInt();
        const int animPhase = w ? w->property("sfProgressPhase").toInt() : 0;

        const QColor accent = (opt.state & QStyle::State_Selected) ? opt.palette.highlightedText().color()
                                                                   : opt.palette.link().color();

        opt.text = QString();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        QRect dec = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, w);
        QRect tr = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        if (!tr.isValid())
            tr = opt.rect;
        int leftBound = opt.rect.left() + 4;
        if (dec.isValid())
            leftBound = qMax(leftBound, dec.right() + 4);

        const int diam = qMax(10, qMin(14, tr.height() - 2));
        QRect badge(leftBound, tr.center().y() - diam / 2, diam, diam);
        if (prog > 0)
            sfPaintAnalysisBadge(painter, badge, prog, animPhase, accent);

        const int textLeft = prog > 0 ? (badge.right() + 8) : leftBound;
        QRect textArea = tr;
        textArea.setLeft(qMax(tr.left(), textLeft));
        painter->save();

        QFont nameFont = opt.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        if (opt.state & QStyle::State_Selected)
            painter->setPen(opt.palette.highlightedText().color());
        else
            painter->setPen(opt.palette.text().color());

        const QFontMetrics fmName(nameFont);
        const QString nameElided = fmName.elidedText(name, Qt::ElideRight, textArea.width());
        painter->drawText(textArea, Qt::AlignLeft | Qt::AlignVCenter, nameElided);

        const int nameW = fmName.horizontalAdvance(nameElided);
        QRect pathRect = textArea;
        pathRect.setLeft(textArea.left() + nameW + 10);

        painter->setFont(opt.font);
        if (!(opt.state & QStyle::State_Selected))
            painter->setPen(Qt::gray);
        painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("[%1]").arg(path));

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString path = index.data(Qt::UserRole + 1).toString();
        const int prog = index.data(kSpinRole).toInt();
        QFont nameFont = opt.font;
        nameFont.setBold(true);
        const QFontMetrics fmName(nameFont);
        const QFontMetrics fmPath(opt.font);
        const int badgeW = prog > 0 ? 26 : 0;
        const int textW = fmName.horizontalAdvance(name) + 10 + fmPath.horizontalAdvance(QStringLiteral("[%1]").arg(path)) + 20 + badgeW;
        const int h = qMax(30, qMax(fmName.height(), fmPath.height()) + 8);
        return QSize(qMax(320, textW), h);
    }
};

class FolderTreeSpinDelegate : public QStyledItemDelegate {
public:
    explicit FolderTreeSpinDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        QWidget *w = const_cast<QWidget *>(opt.widget);
        QStyle *sty = w ? w->style() : QApplication::style();
        const QTreeView *tv = qobject_cast<const QTreeView *>(w);
        if (!tv && w)
            tv = qobject_cast<const QTreeView *>(w->parentWidget());

        QModelIndex src = index;
        if (const auto *px = qobject_cast<const QAbstractProxyModel *>(index.model()))
            src = px->mapToSource(index);
        const auto *fs = qobject_cast<const QFileSystemModel *>(src.model());
        const QString rowPath = fs ? QDir::cleanPath(fs->filePath(src)) : QString();

        const QString savedText = opt.text;
        opt.text = QString();
        sty->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        QRect dec = sty->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, w);
        QRect tr = sty->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        if (!tr.isValid())
            tr = opt.rect;
        int leftBound = opt.rect.left() + 2;
        if (dec.isValid())
            leftBound = qMax(leftBound, dec.right() + 4);

        const int st = sfFolderRowBadgeState(rowPath, tv);
        const int animPhase = tv ? tv->property("sfProgressPhase").toInt() : 0;
        const QColor accent = (opt.state & QStyle::State_Selected) ? opt.palette.highlightedText().color()
                                                                   : opt.palette.link().color();

        const int diam = qMax(10, qMin(14, tr.height() - 2));
        QRect badge(leftBound, tr.center().y() - diam / 2, diam, diam);
        if (st > 0)
            sfPaintAnalysisBadge(painter, badge, st, animPhase, accent);

        const int textLeft = st > 0 ? (badge.right() + 6) : leftBound;
        QRect textDraw = tr;
        textDraw.setLeft(qMax(tr.left(), textLeft));

        painter->save();
        if (opt.state & QStyle::State_Selected)
            painter->setPen(opt.palette.highlightedText().color());
        else
            painter->setPen(opt.palette.text().color());
        painter->setFont(opt.font);
        const QString elided = opt.fontMetrics.elidedText(savedText, Qt::ElideRight, textDraw.width());
        painter->drawText(textDraw, Qt::AlignLeft | Qt::AlignVCenter, elided);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget *ow = opt.widget;
        const QTreeView *tv = qobject_cast<const QTreeView *>(ow);
        if (!tv && ow)
            tv = qobject_cast<const QTreeView *>(ow->parentWidget());
        QModelIndex src = index;
        if (const auto *px = qobject_cast<const QAbstractProxyModel *>(index.model()))
            src = px->mapToSource(index);
        const auto *fs = qobject_cast<const QFileSystemModel *>(src.model());
        const QString rowPath = fs ? QDir::cleanPath(fs->filePath(src)) : QString();
        const int st = sfFolderRowBadgeState(rowPath, tv);
        const int extra = st > 0 ? 22 : 0;
        const QSize sh = QStyledItemDelegate::sizeHint(option, index);
        return QSize(sh.width() + extra, sh.height());
    }
};

namespace {

static const QString kTagImage = QStringLiteral("🖼️ 圖片");
static const QString kTagVideo = QStringLiteral("🎬 影片");
static const QString kTagDoc = QStringLiteral("📄 文件");
static const QString kTagAudio = QStringLiteral("🎵 音檔");
static const QString kTagDb = QStringLiteral("🗃️ 資料庫");

static QString normalizeDisplayTag(const QString &t) {
    const QString s = t.trimmed();
    // Merge legacy plain system tags into canonical emoji+zh tags
    if (s == QStringLiteral("圖片")) return kTagImage;
    if (s == QStringLiteral("影片")) return kTagVideo;
    if (s == QStringLiteral("文件")) return kTagDoc;
    if (s == QStringLiteral("音檔") || s == QStringLiteral("音訊")) return kTagAudio;
    if (s == QStringLiteral("資料庫")) return kTagDb;

    if (s == QStringLiteral("🖼️圖片") || s == kTagImage) return kTagImage;
    if (s == QStringLiteral("🎬影片") || s == kTagVideo) return kTagVideo;
    if (s == QStringLiteral("📄文件") || s == kTagDoc) return kTagDoc;
    if (s == QStringLiteral("🎧音訊") || s == QStringLiteral("🎵音檔") || s == kTagAudio) return kTagAudio;
    if (s == QStringLiteral("🗃️資料庫") || s == QStringLiteral("🗄️資料庫") || s == kTagDb) return kTagDb;
    return s;
}

static QString systemTagBaseZh(const QString &canon) {
    if (canon == kTagImage) return QStringLiteral("圖片");
    if (canon == kTagVideo) return QStringLiteral("影片");
    if (canon == kTagDoc) return QStringLiteral("文件");
    if (canon == kTagAudio) return QStringLiteral("音檔");
    if (canon == kTagDb) return QStringLiteral("資料庫");
    if (canon.contains(QStringLiteral("壓縮檔"))) return QStringLiteral("壓縮檔");
    if (canon.contains(QStringLiteral("程式碼"))) return QStringLiteral("程式碼");
    if (canon.contains(QStringLiteral("安裝檔"))) return QStringLiteral("安裝檔");
    if (canon.contains(QStringLiteral("備份檔"))) return QStringLiteral("備份檔");
    if (canon.contains(QStringLiteral("設定"))) return QStringLiteral("設定");
    if (canon.contains(QStringLiteral("設計"))) return QStringLiteral("設計");
    if (canon.contains(QStringLiteral("資料庫"))) return QStringLiteral("資料庫");
    if (canon.contains(QStringLiteral("學校作業"))) return QStringLiteral("學校作業");
    if (canon.contains(QStringLiteral("應用程式"))) return QStringLiteral("應用程式");
    if (canon.contains(QStringLiteral("履歷"))) return QStringLiteral("履歷");
    return QString();
}

static QString systemTagEmojiPrefix(const QString &canon) {
    // Extract a leading emoji-ish prefix even when there's no space.
    // We stop once we hit a letter/number or CJK ideograph.
    QString out;
    for (int i = 0; i < canon.size(); ++i) {
        const QChar c = canon.at(i);
        if (c.isSpace()) {
            if (!out.isEmpty()) break;
            continue;
        }
        const ushort u = c.unicode();
        const bool isCjk = (u >= 0x4E00 && u <= 0x9FFF);
        if (c.isLetterOrNumber() || isCjk) {
            break;
        }
        out.append(c);
        // Safety: don't let it grow unbounded
        if (out.size() >= 6) break;
    }
    return out.trimmed();
}

// Left-panel “AI 標籤” tab already implies AI — hide “[AI] …” in labels only (data keys unchanged).
static QString tagLibraryLabelStripAiBadge(const QString &displayName)
{
    QString d = displayName.trimmed();
    d = TagManager::stripAiPrefix(d).trimmed();
    static const QRegularExpression corruptOpen(QStringLiteral("^\\[\\s+"));
    if (corruptOpen.match(d).hasMatch()) {
        QString u = d;
        u.remove(corruptOpen);
        d = u.trimmed();
    }
    return d.trimmed();
}

// Same presentation rules as tag filter / AI list, for merge-target picker (display only).
static QString mergeTargetPickerLabel(const QString &rawTag)
{
    const QString canon = normalizeDisplayTag(rawTag);
    const QString baseZh = systemTagBaseZh(canon);
    const QString emoji = systemTagEmojiPrefix(canon);
    QString displayName = baseZh.isEmpty()
                              ? canon
                              : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
    displayName = displayName.trimmed();
    if (TagManager::hasAiPrefix(canon))
        displayName = tagLibraryLabelStripAiBadge(displayName);
    return displayName;
}

static QString emojiForMime(const QMimeType &mt) {
    const QString name = mt.name();
    if (name.startsWith("image/")) return QStringLiteral("🖼️");
    if (name.startsWith("video/")) return QStringLiteral("🎬");
    if (name.startsWith("audio/")) return QStringLiteral("🎧");
    if (name == QStringLiteral("application/pdf")) return QStringLiteral("📄");
    if (name.startsWith("text/")) return QStringLiteral("📝");
    if (name.contains(QStringLiteral("zip")) || name.contains(QStringLiteral("rar")) || name.contains(QStringLiteral("7z")) || name.contains(QStringLiteral("tar")))
        return QStringLiteral("📦");
    if (name.contains(QStringLiteral("json")) || name.contains(QStringLiteral("xml")) || name.contains(QStringLiteral("yaml")))
        return QStringLiteral("🧩");
    return QStringLiteral("📎");
}

static QString mimeDisplay(const QMimeType &mt) {
    const QString comment = mt.comment();
    if (!comment.isEmpty()) return comment;
    return mt.name();
}

static QString baseName(const QString &absPath) {
    return QFileInfo(absPath).fileName();
}

static QString parentDirDisplay(const QString &absPath) {
    return QFileInfo(absPath).absolutePath();
}

static QString resolveModelPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidates[] = {
        QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")),
        QDir(appDir).filePath(QStringLiteral("../assets/models/chat_model.gguf")),
    };
    for (const QString &p : candidates) {
        const QString clean = QDir::cleanPath(p);
        if (QFile::exists(clean)) return clean;
    }
    return QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")));
}

/// Full-file SHA-256 (lowercase hex). Empty if unreadable.
static QString sha256HexOfFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    char buf[65536];
    while (true) {
        const qint64 n = f.read(buf, sizeof buf);
        if (n < 0) return QString();
        if (n == 0) break;
        h.addData(QByteArrayView(buf, static_cast<qsizetype>(n)));
    }
    return QString::fromLatin1(h.result().toHex());
}

static const QString &kAiDrawerUncategorizedKey()
{
    static const QString k = QStringLiteral("__uncat__");
    return k;
}

static QStringList sfFixedAiClusterDrawerKeys()
{
    return {QStringLiteral("[營運管理]"),     QStringLiteral("[技術與開發]"), QStringLiteral("[財務與法務]"),
            QStringLiteral("[行銷與企劃]"), QStringLiteral("[人事與行政]"), QStringLiteral("[多媒體資源]"),
            QStringLiteral("[會議與報告]"), QStringLiteral("[其他雜項]")};
}

static QString sfAiFolderTagForDrawerCanon(const QString &canonicalBracketKey)
{
    QString s = canonicalBracketKey.trimmed();
    if (s.startsWith(QLatin1Char('[')) && s.endsWith(QLatin1Char(']')))
        s = s.mid(1, s.size() - 2).trimmed();
    return QStringLiteral("[AI] ") + s;
}

static QString sfNormalizeDrawerJsonKeyToCanon(const QString &raw)
{
    const QString t = raw.trimmed();
    for (const QString &ref : sfFixedAiClusterDrawerKeys()) {
        if (QString::compare(t, ref, Qt::CaseInsensitive) == 0) return ref;
        const QString inner = ref.mid(1, ref.size() - 2).trimmed();
        if (QString::compare(t, inner, Qt::CaseInsensitive) == 0) return ref;
        const QString withAi = QStringLiteral("[AI] ") + inner;
        if (QString::compare(t, withAi, Qt::CaseInsensitive) == 0) return ref;
    }
    return QString();
}

static bool sfIsSyntheticAiDrawerFolderTag(const QString &t)
{
    const QString nt = t.trimmed();
    for (const QString &dk : sfFixedAiClusterDrawerKeys()) {
        if (nt == sfAiFolderTagForDrawerCanon(dk)) return true;
    }
    return false;
}

static QStringList orderedSystemTagWhitelistCanons()
{
    return {kTagImage,
            kTagVideo,
            kTagAudio,
            kTagDoc,
            kTagDb,
            QStringLiteral("📦壓縮檔"),
            QStringLiteral("🧩設定"),
            QStringLiteral("🧩 程式碼"),
            QStringLiteral("💻安裝檔"),
            QStringLiteral("💻應用程式"),
            QStringLiteral("📦備份檔"),
            QStringLiteral("🗓️會議"),
            QStringLiteral("🧑‍💼履歷"),
            QStringLiteral("🎒學校作業"),
            QStringLiteral("💰財務"),
            QStringLiteral("🎨設計")};
}

static QString mapLooseSystemTagToWhitelistCanon(const QString &rawIn)
{
    const QString n0 = normalizeDisplayTag(rawIn.trimmed());
    if (TagManager::hasAiPrefix(n0)) return QString();

    for (const QString &w : orderedSystemTagWhitelistCanons()) {
        if (QString::compare(n0, w, Qt::CaseInsensitive) == 0) return w;
    }

    const QString low = n0.toLower();
    if (low.contains(QStringLiteral("system shortcut")) || low.contains(QStringLiteral("系統捷徑")))
        return QStringLiteral("🧩設定");

    if (n0.contains(QStringLiteral("安裝檔"))) return QStringLiteral("💻安裝檔");

    if (low == QStringLiteral("application") || low == QStringLiteral("applications") || low.contains(QStringLiteral("[應用程式]"))
        || n0.contains(QStringLiteral("應用程式"))) {
        return QStringLiteral("💻應用程式");
    }

    if (low.contains(QStringLiteral("source code")) || low == QStringLiteral("code") || low == QStringLiteral("script")
        || n0.contains(QStringLiteral("程式碼")))
        return QStringLiteral("🧩 程式碼");

    if (n0.contains(QStringLiteral("壓縮檔"))) return QStringLiteral("📦壓縮檔");
    if (n0.contains(QStringLiteral("備份檔"))) return QStringLiteral("📦備份檔");
    if (n0.contains(QStringLiteral("會議"))) return QStringLiteral("🗓️會議");
    if (n0.contains(QStringLiteral("履歷"))) return QStringLiteral("🧑‍💼履歷");
    if (n0.contains(QStringLiteral("學校作業"))) return QStringLiteral("🎒學校作業");
    if (n0.contains(QStringLiteral("財務"))) return QStringLiteral("💰財務");
    if (n0.contains(QStringLiteral("設計"))) return QStringLiteral("🎨設計");
    if (n0.contains(QStringLiteral("設定"))) return QStringLiteral("🧩設定");

    return QString();
}

static QString aiCoreKeyForFuzzy(const QString &tagOrRaw)
{
    QString u = tagOrRaw.trimmed();
    u = TagManager::stripAiPrefix(u).trimmed();
    static const QRegularExpression reWs(QStringLiteral("\\s+"));
    u.replace(reWs, QStringLiteral(" "));
    return u.toLower();
}

static QString fuzzyResolveAiTagKey(const QString &rawIn, const QSet<QString> &aiTagSet)
{
    const QString raw = rawIn.trimmed();
    if (raw.isEmpty()) return QString();

    QStringList variants;
    variants << raw;
    if (!TagManager::hasAiPrefix(raw)) {
        variants << (QStringLiteral("[AI] ") + raw);
        variants << (QStringLiteral("[AI]") + raw);
    }

    for (const QString &cand0 : std::as_const(variants)) {
        const QString k = normalizeDisplayTag(cand0.trimmed());
        if (k.isEmpty()) continue;
        if (aiTagSet.contains(k)) {
            for (const QString &x : aiTagSet) {
                if (x == k) return x;
            }
        }
        for (const QString &x : aiTagSet) {
            if (QString::compare(x, k, Qt::CaseInsensitive) == 0) return x;
        }
    }

    const QString want = aiCoreKeyForFuzzy(raw);
    if (want.isEmpty()) return QString();
    for (const QString &x : aiTagSet) {
        if (aiCoreKeyForFuzzy(x) == want) return x;
    }
    return QString();
}

} // namespace

void MainWindow::prependSingleFileToAnalysisQueueFront(const QString &absPath)
{
    const QString p = QDir::cleanPath(absPath);
    if (p.isEmpty() || !m_isBatchMode) return;

    QFileInfo finfo(p);
    if (!isAnalyzableFile(finfo)) return;
    if (m_aiSummaryByPath.contains(p)) return;

    if (!m_currentAnalyzingFile.isEmpty() && m_currentAnalyzingFile == p) return;

    QQueue<QString> stripped;
    while (!m_analysisQueue.isEmpty()) {
        const QString x = m_analysisQueue.dequeue();
        if (x != p) stripped.enqueue(x);
    }
    m_analysisQueue = std::move(stripped);

    QQueue<QString> newQueue;
    newQueue.enqueue(p);
    while (!m_analysisQueue.isEmpty()) newQueue.enqueue(m_analysisQueue.dequeue());
    m_analysisQueue = std::move(newQueue);

    const bool inFlight = (watcher && watcher->isRunning());
    m_totalBatchSize = m_batchCompletedCount + m_analysisQueue.size() + (inFlight ? 1 : 0);
    if (batchProgressBar) {
        batchProgressBar->setMaximum(qMax(1, m_totalBatchSize));
        syncBatchProgressBars();
    }
    updateBackgroundStatusLabel();
    m_priorityFolderBannerPath.clear();
    refreshCurrentAnalysisTargetUi();
    m_coldArchiveBypassPaths.insert(p);
}

void MainWindow::enqueuePriorityAnalyzeForFileIfNeeded(const QString &absPath)
{
    const QString p = QDir::cleanPath(absPath);
    if (p.isEmpty()) return;
    QFileInfo fi(p);
    if (!isAnalyzableFile(fi)) return;
    if (m_aiSummaryByPath.contains(p)) return;

    if (m_isBatchMode) {
        prependSingleFileToAnalysisQueueFront(p);
        return;
    }

    if (watcher && watcher->isRunning()) {
        m_pendingPrioritySingleFile = p;
        m_coldArchiveBypassPaths.insert(p);
        refreshCurrentAnalysisTargetUi();
        return;
    }

    if (!llamaEngine.isModelLoaded()) return;

    QTimer::singleShot(0, this, [this, p]() { analyzeFileForPath(p); });
}

void MainWindow::prioritizeAnalysisPaths(QStringList &paths, const QString &focusFolderAbs)
{
    const QString focus = QDir::cleanPath(focusFolderAbs);
    if (focus.isEmpty() || paths.isEmpty()) return;
    QStringList under;
    QStringList other;
    under.reserve(paths.size());
    other.reserve(paths.size());
    for (const QString &p : paths) {
        const QString d = QDir::cleanPath(QFileInfo(p).absolutePath());
        if (d == focus || d.startsWith(focus + QLatin1Char('/'))) under << p;
        else other << p;
    }
    paths = under + other;
}

void MainWindow::prependUnanalyzedFromFolderToAnalysisQueue(const QString &folderAbs)
{
    const QString folder = QDir::cleanPath(folderAbs);
    if (folder.isEmpty() || !m_isBatchMode) return;

    const bool recursive = chkRecursive && chkRecursive->isChecked();
    const QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QStringList found;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        const QString p = QDir::cleanPath(it.next());
        if (p.contains(QStringLiteral("/.smartfile")) || p.contains(QStringLiteral("\\.smartfile"))) continue;
        const QFileInfo finfo(p);
        if (!isAnalyzableFile(finfo)) continue;
        if (m_aiSummaryByPath.contains(p)) continue;
        found << p;
    }
    if (found.isEmpty()) {
        refreshCurrentAnalysisTargetUi();
        return;
    }
    prioritizeAnalysisPaths(found, folder);

    QSet<QString> existing;
    for (const QString &q : m_analysisQueue) existing.insert(q);
    if (!m_currentAnalyzingFile.isEmpty()) existing.insert(m_currentAnalyzingFile);
    for (auto pit = m_pendingResults.constBegin(); pit != m_pendingResults.constEnd(); ++pit) existing.insert(pit.key());

    QStringList prepend;
    for (const QString &p : found) {
        if (existing.contains(p)) continue;
        prepend << p;
        existing.insert(p);
    }
    if (prepend.isEmpty()) {
        refreshCurrentAnalysisTargetUi();
        return;
    }

    for (const QString &p : prepend)
        m_coldArchiveBypassPaths.insert(p);

    QQueue<QString> newQueue;
    for (const QString &p : prepend) newQueue.enqueue(p);
    while (!m_analysisQueue.isEmpty()) newQueue.enqueue(m_analysisQueue.dequeue());
    m_analysisQueue = std::move(newQueue);

    const bool inFlight = (watcher && watcher->isRunning());
    m_totalBatchSize = m_batchCompletedCount + m_analysisQueue.size() + (inFlight ? 1 : 0);
    if (batchProgressBar) {
        batchProgressBar->setMaximum(qMax(1, m_totalBatchSize));
        syncBatchProgressBars();
    }
    updateBackgroundStatusLabel();
    refreshCurrentAnalysisTargetUi();
}

namespace {

QString shortHashForUi(const QString &hex)
{
    if (hex.size() <= 16) return hex;
    return hex.left(8) + QStringLiteral("…") + hex.right(6);
}

void collectCheckedPathsFromRedundancyGroup(QTreeWidgetItem *grp, QStringList *out)
{
    if (!grp || !out) return;
    QTreeWidget *tw = grp->treeWidget();
    if (!tw) return;
    for (int ci = 0; ci < grp->childCount(); ++ci) {
        QTreeWidgetItem *row = grp->child(ci);
        if (!row) continue;
        auto *cb = qobject_cast<QCheckBox *>(tw->itemWidget(row, 0));
        if (!cb || !cb->isChecked()) continue;
        const QString p = cb->property("absPath").toString();
        if (!p.isEmpty()) *out << p;
    }
}

} // namespace

void MainWindow::mergeTaskCenterRedundancyBatch(int batchFilesAnalyzed,
                                                int batchNewTagAdds,
                                                const QMap<QString, QSet<QString>> &hashGroups,
                                                const QMap<QString, QSet<QString>> &nameGroups)
{
    m_tcAccumFilesAnalyzed += batchFilesAnalyzed;
    m_tcAccumTagAdds += batchNewTagAdds;
    for (auto it = hashGroups.constBegin(); it != hashGroups.constEnd(); ++it) {
        if (it.value().isEmpty()) continue;
        m_persistRedundancyHash[it.key()].unite(it.value());
    }
    for (auto it = nameGroups.constBegin(); it != nameGroups.constEnd(); ++it) {
        if (it.value().size() < 2) continue;
        m_persistRedundancyName[it.key()].unite(it.value());
    }
    refreshTaskCenterRedundancyTreeUi();
}

void MainWindow::pruneTaskCenterPersistentRedundancy(const QStringList &removedPaths)
{
    QSet<QString> dead;
    for (const QString &p : removedPaths) {
        const QString c = QDir::cleanPath(p);
        if (!c.isEmpty()) dead.insert(c);
    }
    if (dead.isEmpty()) return;

    for (auto it = m_persistRedundancyHash.begin(); it != m_persistRedundancyHash.end();) {
        for (const QString &p : dead) it.value().remove(p);
        if (it.value().size() < 2) it = m_persistRedundancyHash.erase(it);
        else ++it;
    }
    for (auto it = m_persistRedundancyName.begin(); it != m_persistRedundancyName.end();) {
        for (const QString &p : dead) it.value().remove(p);
        if (it.value().size() < 2) it = m_persistRedundancyName.erase(it);
        else ++it;
    }
    if (m_persistRedundancyHash.isEmpty() && m_persistRedundancyName.isEmpty()) {
        m_tcAccumFilesAnalyzed = 0;
        m_tcAccumTagAdds = 0;
    }
    refreshTaskCenterRedundancyTreeUi();
}

void MainWindow::refreshTaskCenterRedundancyTreeUi()
{
    if (!m_taskCenterRedundancyTree) return;
    QScrollBar *vsb = m_taskCenterRedundancyTree->verticalScrollBar();
    const int scrollValue = vsb ? vsb->value() : 0;

    m_taskCenterRedundancyTree->clear();

    auto filterMultiPath = [](const QMap<QString, QSet<QString>> &in) {
        QMap<QString, QSet<QString>> out;
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            if (it.value().size() >= 2) out.insert(it.key(), it.value());
        }
        return out;
    };

    const QMap<QString, QSet<QString>> hashToPaths = filterMultiPath(m_persistRedundancyHash);
    const QMap<QString, QSet<QString>> baseNameToPaths = filterMultiPath(m_persistRedundancyName);

    int hashDupCount = 0;
    for (auto it = hashToPaths.constBegin(); it != hashToPaths.constEnd(); ++it) {
        hashDupCount += static_cast<int>(it.value().size()) - 1;
    }
    int nameDupCount = 0;
    for (auto it = baseNameToPaths.constBegin(); it != baseNameToPaths.constEnd(); ++it) {
        nameDupCount += static_cast<int>(it.value().size()) - 1;
    }

    auto &lm = LanguageManager::instance();
    auto *head = new QTreeWidgetItem(m_taskCenterRedundancyTree,
                                     {lm.getText(QStringLiteral("redundancy_dialog_body_grouped"))
                                          .arg(m_tcAccumFilesAnalyzed)
                                          .arg(m_tcAccumTagAdds)
                                          .arg(hashDupCount)
                                          .arg(nameDupCount)});
    head->setFlags(head->flags() & ~Qt::ItemIsSelectable);

    if (!hashToPaths.isEmpty()) {
        auto *section = new QTreeWidgetItem(m_taskCenterRedundancyTree,
                                            {lm.getText(QStringLiteral("redundancy_section_hash"))});
        section->setExpanded(true);
        section->setFlags(section->flags() & ~Qt::ItemIsSelectable);
        for (auto it = hashToPaths.constBegin(); it != hashToPaths.constEnd(); ++it) {
            const QString hx = it.key();
            const QSet<QString> paths = it.value();
            if (paths.isEmpty()) continue;
            const QString title = lm.getText(QStringLiteral("redundancy_group_hash_title"))
                                      .arg(shortHashForUi(hx))
                                      .arg(paths.size());
            auto *grp = new QTreeWidgetItem(section, {title});
            grp->setExpanded(true);
            grp->setFlags(grp->flags() & ~Qt::ItemIsSelectable);
            QStringList ordered;
            for (const QString &p : paths) ordered << p;
            std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
                return a.localeAwareCompare(b) < 0;
            });
            for (const QString &path : ordered) {
                auto *row = new QTreeWidgetItem(grp, {QString()});
                auto *cb = new QCheckBox(path, m_taskCenterRedundancyTree);
                cb->setProperty("absPath", path);
                cb->setChecked(false);
                m_taskCenterRedundancyTree->setItemWidget(row, 0, cb);
            }
        }
    }

    if (!baseNameToPaths.isEmpty()) {
        auto *section = new QTreeWidgetItem(m_taskCenterRedundancyTree,
                                            {lm.getText(QStringLiteral("redundancy_section_name"))});
        section->setExpanded(true);
        section->setFlags(section->flags() & ~Qt::ItemIsSelectable);
        QStringList keys = baseNameToPaths.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });
        for (const QString &base : keys) {
            const QSet<QString> paths = baseNameToPaths.value(base);
            if (paths.size() < 2) continue;
            const QString title =
                lm.getText(QStringLiteral("redundancy_group_name_title")).arg(base).arg(paths.size());
            auto *grp = new QTreeWidgetItem(section, {title});
            grp->setExpanded(true);
            grp->setFlags(grp->flags() & ~Qt::ItemIsSelectable);
            QStringList ordered;
            for (const QString &p : paths) ordered << p;
            std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
                return a.localeAwareCompare(b) < 0;
            });
            for (const QString &path : ordered) {
                auto *row = new QTreeWidgetItem(grp, {QString()});
                auto *cb = new QCheckBox(path, m_taskCenterRedundancyTree);
                cb->setProperty("absPath", path);
                cb->setChecked(false);
                m_taskCenterRedundancyTree->setItemWidget(row, 0, cb);
            }
        }
    }

    if (vsb) {
        const int mx = vsb->maximum();
        vsb->setValue(qBound(0, scrollValue, mx));
    }
}

void MainWindow::onTaskCenterCleanClicked()
{
    if (!m_taskCenterRedundancyTree) return;
    auto &lm = LanguageManager::instance();
    QStringList toDelete;
    for (int si = 0; si < m_taskCenterRedundancyTree->topLevelItemCount(); ++si) {
        QTreeWidgetItem *top = m_taskCenterRedundancyTree->topLevelItem(si);
        if (!top) continue;
        for (int gi = 0; gi < top->childCount(); ++gi) {
            QTreeWidgetItem *grp = top->child(gi);
            if (!grp) continue;
            collectCheckedPathsFromRedundancyGroup(grp, &toDelete);
        }
    }
    if (toDelete.isEmpty()) {
        QMessageBox::warning(this, lm.getText(QStringLiteral("redundancy_dialog_title")),
                             lm.getText(QStringLiteral("redundancy_delete_select_first")));
        return;
    }
    QStringList removed;
    for (const QString &p : toDelete) {
        if (QFile::remove(p)) removed << p;
    }
    if (!removed.isEmpty()) {
        QString bulletLines;
        for (const QString &p : removed) bulletLines += QStringLiteral("- %1\n").arg(p);
        bulletLines = bulletLines.trimmed();
        QMessageBox::information(this, lm.getText(QStringLiteral("redundancy_dialog_title")),
                                 lm.getText(QStringLiteral("redundancy_delete_success")).arg(bulletLines));
        {
            QMutexLocker locker(&tagMutex);
            for (const QString &p : removed) tagManager.removeFileMetadata(p, false);
            tagManager.saveTags();
        }
        for (const QString &p : removed) m_aiSummaryByPath.remove(p);
        scanFiles();
        updateTagList();
        if (m_bgAutoAnalyzeEnabled) ensureRecursiveWatchCoversWorkspace();
        pruneTaskCenterPersistentRedundancy(removed);
    }
}

void MainWindow::recordBatchPathForContentHash(const QString &hashHex, const QString &filePath)
{
    if (hashHex.isEmpty() || filePath.isEmpty()) return;
    m_batchHashToPaths[hashHex].insert(filePath);
    QStringList known;
    {
        QMutexLocker locker(&tagMutex);
        known = tagManager.filePathsWithContentHash(hashHex);
    }
    for (const QString &p : known) m_batchHashToPaths[hashHex].insert(p);
}

void MainWindow::noteSameNameDifferentHashConflicts(const QString &filePath, const QString &hashHex)
{
    if (filePath.isEmpty() || hashHex.isEmpty()) return;
    const QString base = QFileInfo(filePath).fileName();
    QStringList candidates;
    {
        QMutexLocker locker(&tagMutex);
        candidates = tagManager.filePathsWithFileName(base);
    }
    for (const QString &other : candidates) {
        if (other == filePath) continue;
        QString oh;
        {
            QMutexLocker locker(&tagMutex);
            oh = tagManager.fileContentHash(other);
        }
        if (oh.isEmpty()) continue;
        if (oh.compare(hashHex, Qt::CaseInsensitive) == 0) continue;
        m_batchNameConflictPaths[base].insert(filePath);
        m_batchNameConflictPaths[base].insert(other);
    }
}

QString MainWindow::elideStatusLine(const QString &fullText, int pixelBudget) const
{
    if (fullText.isEmpty()) return fullText;
    const int budget = qMax(48, pixelBudget);
    if (!lblBackgroundStatus) return fullText;
    return lblBackgroundStatus->fontMetrics().elidedText(fullText, Qt::ElideRight, budget);
}

void MainWindow::syncBatchAnalyzeButtonLabel()
{
    if (!btnBatchAnalyze) return;
    auto &lm = LanguageManager::instance();
    if (!btnBatchAnalyze->isEnabled() && m_isBatchMode && m_batchTriggeredByBackgroundAuto) {
        btnBatchAnalyze->setText(lm.getText(QStringLiteral("btn_batch_bg_running")));
    } else {
        btnBatchAnalyze->setText(lm.getText(QStringLiteral("資料夾分析")));
    }
}

void MainWindow::refreshFileAndFolderAnalysisIndicators()
{
    const bool inferBusy = watcher && watcher->isRunning();
    const bool spinHot = inferBusy || m_analysisUiWorkActive;
    const QString cur = QDir::cleanPath(m_currentAnalyzingFile);

    QSet<QString> queued;
    for (const QString &p : m_analysisQueue)
        queued.insert(QDir::cleanPath(p));

    if (fileList) {
        fileList->setProperty("sfProgressPhase", m_analysisSpinPhase);
        if (QWidget *vp = fileList->viewport())
            vp->setProperty("sfProgressPhase", m_analysisSpinPhase);
        for (int i = 0; i < fileList->count(); ++i) {
            auto *it = fileList->item(i);
            if (!it) continue;
            const QString path = QDir::cleanPath(it->data(Qt::UserRole).toString());
            if (path.isEmpty()) {
                it->setData(FileItemDelegate::kSpinRole, QVariant());
                continue;
            }

            int st = 0;
            if (!cur.isEmpty() && path == cur && spinHot)
                st = 2;
            else if (m_isBatchMode && queued.contains(path))
                st = 1;
            else if (m_aiSummaryByPath.contains(path) || m_pendingResults.contains(path))
                st = 3;

            it->setData(FileItemDelegate::kSpinRole, st > 0 ? QVariant(st) : QVariant());
        }
        fileList->viewport()->update();
    }

    if (folderTree) {
        QStringList pend;
        pend.reserve(m_analysisQueue.size());
        for (const QString &p : m_analysisQueue)
            pend.append(QDir::cleanPath(p));

        folderTree->setProperty("sfBatchActive", m_isBatchMode ? 1 : 0);
        folderTree->setProperty("sfBrowsingDir", QDir::cleanPath(currentPath));
        folderTree->setProperty("sfPendingPaths", pend);
        const QString ad = cur.isEmpty() ? QString() : QFileInfo(cur).absolutePath();
        folderTree->setProperty("sfAnalyzingDir", QDir::cleanPath(ad));
        folderTree->setProperty("sfWatcherBusy", spinHot ? 1 : 0);
        folderTree->setProperty("sfProgressPhase", m_analysisSpinPhase);
        if (QWidget *tvp = folderTree->viewport())
            tvp->setProperty("sfProgressPhase", m_analysisSpinPhase);
        folderTree->viewport()->update();
    }

    syncPreviewBusySpinner();
}

void MainWindow::ensureAnalysisIndicatorTimer()
{
    const bool inferBusy = watcher && watcher->isRunning();
    const bool spinHot = inferBusy || m_analysisUiWorkActive;
    if (!m_analysisSpinTimer)
        return;
    if (spinHot) {
        if (!m_analysisSpinTimer->isActive())
            m_analysisSpinTimer->start();
    } else {
        m_analysisSpinTimer->stop();
    }
}

void MainWindow::reselectFileInList(const QString &absPath)
{
    if (!fileList || absPath.isEmpty()) return;
    const QString fp = QDir::cleanPath(absPath);
    for (int i = 0; i < fileList->count(); ++i) {
        auto *it = fileList->item(i);
        if (!it) continue;
        if (QDir::cleanPath(it->data(Qt::UserRole).toString()) != fp) continue;
        fileList->setCurrentItem(it);
        fileList->scrollToItem(it);
        onFileSelected(it);
        break;
    }
}

void MainWindow::syncPreviewBusySpinner()
{
    const bool inferBusy = watcher && watcher->isRunning();
    const bool show = m_analysisUiWorkActive || inferBusy;
    if (!m_statusBusyChip)
        return;
    m_statusBusyChip->setVisible(show);
    if (show)
        m_statusBusyChip->setPhase(m_analysisSpinPhase);
}

void MainWindow::clearAnalysisWorkFlagsAndSyncUi()
{
    m_analysisUiWorkActive = false;
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::startAnalysisSpinnerForPath(const QString &absPath)
{
    Q_UNUSED(absPath);
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::stopAnalysisSpinner()
{
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::tickAnalysisSpinner()
{
    m_analysisSpinPhase = (m_analysisSpinPhase + 1) & 3;
    if (fileList) {
        fileList->setProperty("sfProgressPhase", m_analysisSpinPhase);
        if (QWidget *vp = fileList->viewport())
            vp->setProperty("sfProgressPhase", m_analysisSpinPhase);
        fileList->viewport()->update();
    }
    if (folderTree) {
        folderTree->setProperty("sfProgressPhase", m_analysisSpinPhase);
        if (QWidget *tvp = folderTree->viewport())
            tvp->setProperty("sfProgressPhase", m_analysisSpinPhase);
        folderTree->viewport()->update();
    }
    if (m_statusBusyChip && m_statusBusyChip->isVisible())
        m_statusBusyChip->setPhase(m_analysisSpinPhase);
}

void MainWindow::refreshCurrentAnalysisTargetUi()
{
    if (!lblCurrentTarget) return;
    auto &lm = LanguageManager::instance();
    const int budget = qMax(120, lblCurrentTarget->width() - 8);
    const QFontMetrics fm(lblCurrentTarget->font());

    if (fileListMode == FileListMode::SemanticResults) {
        const QString banner =
            QStringLiteral("🌐 全域語意搜尋結果（扁平列表 · 已脫離目前資料夾範圍）");
        lblCurrentTarget->setText(fm.elidedText(banner, Qt::ElideRight, budget));
        return;
    }

    const bool inferBusy = watcher && watcher->isRunning();
    const QString cur = m_currentAnalyzingFile;

    QString raw;
    if (inferBusy && m_isBatchMode && !m_analysisQueue.isEmpty()) {
        const QString head = m_analysisQueue.head();
        if (!cur.isEmpty() && head != cur) {
            raw = lm.getText(QStringLiteral("ui_target_lock_file")).arg(QFileInfo(head).fileName());
        }
    }
    if (raw.isEmpty() && !cur.isEmpty() && (inferBusy || m_isBatchMode)) {
        raw = lm.getText(QStringLiteral("ui_target_analyzing")).arg(QFileInfo(cur).fileName());
    }
    if (raw.isEmpty() && m_isBatchMode && !m_analysisQueue.isEmpty()) {
        raw = lm.getText(QStringLiteral("ui_target_lock_file")).arg(QFileInfo(m_analysisQueue.head()).fileName());
    }
    if (raw.isEmpty() && !m_pendingPrioritySingleFile.isEmpty()) {
        raw = lm.getText(QStringLiteral("ui_target_lock_file")).arg(QFileInfo(m_pendingPrioritySingleFile).fileName());
    }
    if (raw.isEmpty() && !m_priorityFolderBannerPath.isEmpty()) {
        raw = lm.getText(QStringLiteral("ui_target_lock_folder")).arg(QFileInfo(m_priorityFolderBannerPath).fileName());
    }

    if (raw.isEmpty()) {
        lblCurrentTarget->clear();
        return;
    }
    lblCurrentTarget->setText(fm.elidedText(raw, Qt::ElideRight, budget));
}

void MainWindow::updateBackgroundStatusLabel()
{
    const int nRemaining = m_isBatchMode ? qMax(0, m_totalBatchSize - m_batchCompletedCount) : 0;

    const QString styleBusyLeft = QStringLiteral(
        "QLabel { color:#1d4ed8; font-weight:700; font-size:14px; padding-left:12px; }");
    static const QString kTaskCenterStatusSheet = QStringLiteral(
        "QLabel { background-color: #2563eb; color: white; border-radius: 4px; padding: 4px; "
        "font-weight: bold; }");

    const bool manualBatchActive = m_isBatchMode && !m_batchTriggeredByBackgroundAuto && nRemaining > 0;
    const bool bgBatchActive = m_isBatchMode && m_batchTriggeredByBackgroundAuto && nRemaining > 0;
    const bool bgIdle = !m_isBatchMode && m_bgAutoAnalyzeEnabled && !rootPath.trimmed().isEmpty();

    QString rawText;
    if (manualBatchActive || bgBatchActive) {
        const QString dirDisp = m_backgroundAnalyzeFolderLabel.isEmpty()
                                    ? QStringLiteral("—")
                                    : m_backgroundAnalyzeFolderLabel;
        rawText = LanguageManager::instance()
                      .getText(QStringLiteral("bg_analyze_queue_dir"))
                      .arg(dirDisp)
                      .arg(nRemaining);
    } else if (bgIdle) {
        rawText = LanguageManager::instance().getText(QStringLiteral("bg_idle_monitoring"));
    } else {
        rawText.clear();
    }

    if (lblBackgroundStatus) {
        if (manualBatchActive) {
            lblBackgroundStatus->setVisible(true);
            lblBackgroundStatus->setStyleSheet(styleBusyLeft);
            lblBackgroundStatus->setFixedWidth(220);
            lblBackgroundStatus->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            lblBackgroundStatus->setMinimumHeight(lblBackgroundStatus->fontMetrics().height() + 4);
            lblBackgroundStatus->setText(elideStatusLine(rawText, 208));
        } else {
            lblBackgroundStatus->setVisible(false);
            lblBackgroundStatus->clear();
        }
    }

    if (m_taskCenterStatusLabel) {
        const bool showTc = bgBatchActive || bgIdle;
        m_taskCenterStatusLabel->setVisible(showTc);
        if (!showTc || rawText.isEmpty()) {
            if (!showTc) m_taskCenterStatusLabel->clear();
        } else {
            m_taskCenterStatusLabel->setStyleSheet(kTaskCenterStatusSheet);
            m_taskCenterStatusLabel->setWordWrap(false);
            m_taskCenterStatusLabel->setMinimumHeight(32);
            int tw = 400;
            if (m_taskCenterSplitter) {
                QWidget *w = m_taskCenterSplitter->widget(0);
                if (w && w->width() > 48) tw = qMax(120, w->width() - 24);
            }
            m_taskCenterStatusLabel->setText(
                m_taskCenterStatusLabel->fontMetrics().elidedText(rawText, Qt::ElideRight, tw));
        }
    }

    if (m_btnRestartBackgroundAnalyze) {
        const bool showRestart =
            m_showRestartBackgroundPrompt && m_bgAutoAnalyzeEnabled && !rootPath.trimmed().isEmpty();
        m_btnRestartBackgroundAnalyze->setVisible(showRestart);
    }

    applyDualTrackBatchProgressVisibility();
}

void MainWindow::appendTaskCenterLog(const QString &text)
{
    if (!m_backgroundLogEdit) return;
    QTextCursor c = m_backgroundLogEdit->textCursor();
    c.movePosition(QTextCursor::End);
    m_backgroundLogEdit->setTextCursor(c);
    m_backgroundLogEdit->insertPlainText(text);
    if (!text.endsWith(QLatin1Char('\n')))
        m_backgroundLogEdit->insertPlainText(QStringLiteral("\n"));
    m_backgroundLogEdit->insertPlainText(QStringLiteral("\n"));
    c.movePosition(QTextCursor::End);
    m_backgroundLogEdit->setTextCursor(c);
}

void MainWindow::syncBatchProgressBars()
{
    if (!batchProgressBar || !m_taskCenterBatchProgress) return;
    m_taskCenterBatchProgress->setRange(batchProgressBar->minimum(), batchProgressBar->maximum());
    m_taskCenterBatchProgress->setValue(batchProgressBar->value());
    m_taskCenterBatchProgress->setFormat(batchProgressBar->format());
    applyDualTrackBatchProgressVisibility();
}

void MainWindow::applyDualTrackBatchProgressVisibility()
{
    const bool manualTrack =
        m_isBatchMode && !m_batchTriggeredByBackgroundAuto && m_totalBatchSize > 0;
    const bool bgTrack = m_isBatchMode && m_batchTriggeredByBackgroundAuto && m_totalBatchSize > 0;

    if (batchProgressBar) batchProgressBar->setVisible(manualTrack);
    if (lblBatchStatus) lblBatchStatus->setVisible(manualTrack);
    if (m_taskCenterBatchProgress) m_taskCenterBatchProgress->setVisible(bgTrack);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupToolbar();
    m_mainTabWidget = new QTabWidget(this);
    setCentralWidget(m_mainTabWidget);

    m_workspaceTab = new QWidget(this);
    auto *workspaceLayout = new QVBoxLayout(m_workspaceTab);
    workspaceLayout->setContentsMargins(8, 8, 8, 0);

    m_workspaceTopBar = new QWidget(m_workspaceTab);
    auto *heroLay = new QHBoxLayout(m_workspaceTopBar);
    heroLay->setContentsMargins(12, 0, 12, 10);
    m_heroOmnibox = new QLineEdit(m_workspaceTopBar);
    m_heroOmnibox->setFixedHeight(40);
    m_heroOmnibox->setClearButtonEnabled(true);
    {
        QFont hf = m_heroOmnibox->font();
        hf.setPointSize(qMax(hf.pointSize() + 1, 12));
        m_heroOmnibox->setFont(hf);
    }
    m_heroOmnibox->setMinimumWidth(320);
    m_heroOmnibox->setMaximumWidth(920);
    m_heroOmnibox->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  border: 1px solid rgba(255,255,255,90);"
        "  border-radius: 12px;"
        "  padding: 6px 14px;"
        "  background: rgba(255,255,255,12);"
        "}"));
    m_heroOmnibox->setPlaceholderText(QStringLiteral(
        "🔍 輸入關鍵字，或使用自然語言進行語意搜尋... (例如：幫我找出去年關於財務的報告)"));

    auto *heroField = new QWidget(m_workspaceTopBar);
    auto *heroFieldLay = new QHBoxLayout(heroField);
    heroFieldLay->setContentsMargins(0, 0, 0, 0);
    heroFieldLay->setSpacing(10);
    heroFieldLay->addWidget(m_heroOmnibox, 1);

    m_btnSemanticSearch = new QPushButton(QStringLiteral("🔍 搜尋"), m_workspaceTopBar);
    m_btnSemanticSearch->setFixedHeight(40);
    m_btnSemanticSearch->setMinimumWidth(100);
    m_btnSemanticSearch->setCursor(Qt::PointingHandCursor);
    m_btnSemanticSearch->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #2b6cb0;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  border: 1px solid #1a4d8c;"
        "  border-radius: 10px;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover:enabled { background: #3182ce; }"
        "QPushButton:pressed { background: #2c5282; padding-top: 7px; padding-bottom: 5px; }"
        "QPushButton:disabled { background: rgba(43,108,176,0.35); color: rgba(255,255,255,0.7); }"));
    heroFieldLay->addWidget(m_btnSemanticSearch, 0, Qt::AlignVCenter);

    m_heroSearchBusyChip = new BusyChip(m_workspaceTopBar);
    m_heroSearchBusyChip->setFixedSize(22, 22);
    m_heroSearchBusyChip->hide();
    heroFieldLay->addWidget(m_heroSearchBusyChip, 0, Qt::AlignVCenter);

    heroLay->addStretch(1);
    heroLay->addWidget(heroField, 0, Qt::AlignHCenter);
    heroLay->addStretch(1);
    workspaceLayout->addWidget(m_workspaceTopBar);

    setupFourColumnLayout();
    workspaceLayout->addWidget(mainSplitter, 1);
    m_mainTabWidget->addTab(m_workspaceTab, tr("核心工作區"));

    connect(m_heroOmnibox, &QLineEdit::textChanged, this, &MainWindow::onHeroOmniboxTextChanged);
    connect(m_heroOmnibox, &QLineEdit::returnPressed, this, &MainWindow::onHeroOmniboxReturnPressed);
    connect(m_btnSemanticSearch, &QPushButton::clicked, this, &MainWindow::onHeroOmniboxReturnPressed);

    m_graphTab = new QWidget(this);
    auto *graphLayout = new QVBoxLayout(m_graphTab);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    m_graphTacticalTitle = new QLabel(QStringLiteral("[戰術情報網絡分析]"), m_graphTab);
    m_graphTacticalTitle->setStyleSheet(QStringLiteral(
        "QLabel { font-weight: 800; font-size: 15px; padding: 8px 14px; color: #e2e8f0; "
        "background: rgba(22,22,30,0.98); border-bottom: 1px solid rgba(255,255,255,28); }"));
    graphLayout->addWidget(m_graphTacticalTitle);
    m_graphWidget = new GraphWidget(&tagManager, m_graphTab);
    graphLayout->addWidget(m_graphWidget, 1);
    m_mainTabWidget->addTab(m_graphTab, tr("關聯圖譜分析"));

    m_taskCenterTab = new QWidget(this);
    auto *tcLayout = new QVBoxLayout(m_taskCenterTab);
    tcLayout->setContentsMargins(12, 12, 12, 12);

    m_taskCenterSplitter = new QSplitter(Qt::Horizontal, m_taskCenterTab);
    m_taskCenterSplitter->setChildrenCollapsible(false);

    auto *tcLeftPane = new QWidget(m_taskCenterSplitter);
    auto *tcLeftLayout = new QVBoxLayout(tcLeftPane);
    tcLeftLayout->setContentsMargins(0, 0, 8, 0);

    m_taskCenterStatusLabel = new QLabel(tcLeftPane);
    m_taskCenterStatusLabel->setWordWrap(false);
    m_taskCenterStatusLabel->setMinimumHeight(28);
    m_taskCenterStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tcLeftLayout->addWidget(m_taskCenterStatusLabel);

    m_taskCenterBatchProgress = new QProgressBar(tcLeftPane);
    m_taskCenterBatchProgress->setFixedHeight(36);
    m_taskCenterBatchProgress->setTextVisible(true);
    m_taskCenterBatchProgress->setVisible(false);
    m_taskCenterBatchProgress->setFormat(QStringLiteral("%p%"));
    m_taskCenterBatchProgress->setAlignment(Qt::AlignCenter);
    m_taskCenterBatchProgress->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  border: 1px solid rgba(255,255,255,70);"
        "  border-radius: 8px;"
        "  background: rgba(255,255,255,10);"
        "  padding: 2px;"
        "  color: rgba(255,255,255,230);"
        "  font-weight: 600;"
        "}"
        "QProgressBar::chunk {"
        "  background: #2b6cb0;"
        "  border-radius: 6px;"
        "  margin: 0px;"
        "}"));
    tcLeftLayout->addWidget(m_taskCenterBatchProgress);

    m_backgroundLogEdit = new QTextEdit(tcLeftPane);
    m_backgroundLogEdit->setReadOnly(true);
    m_backgroundLogEdit->setAcceptRichText(false);
    m_backgroundLogEdit->setMinimumHeight(72);
    m_backgroundLogEdit->setMaximumHeight(220);
    tcLeftLayout->addWidget(m_backgroundLogEdit, 0);

    auto *tcRightPane = new QWidget(m_taskCenterSplitter);
    auto *tcRightLayout = new QVBoxLayout(tcRightPane);
    tcRightLayout->setContentsMargins(8, 0, 0, 0);

    m_taskCenterRedundancyTree = new QTreeWidget(tcRightPane);
    m_taskCenterRedundancyTree->setColumnCount(1);
    m_taskCenterRedundancyTree->setHeaderHidden(true);
    m_taskCenterRedundancyTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_taskCenterRedundancyTree->header()->setStretchLastSection(false);
    m_taskCenterRedundancyTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tcRightLayout->addWidget(m_taskCenterRedundancyTree, 1);

    m_taskCenterCleanBtn = new QPushButton(QStringLiteral("清理勾選檔案"), tcRightPane);
    connect(m_taskCenterCleanBtn, &QPushButton::clicked, this, &MainWindow::onTaskCenterCleanClicked);
    tcRightLayout->addWidget(m_taskCenterCleanBtn);

    m_taskCenterSplitter->addWidget(tcLeftPane);
    m_taskCenterSplitter->addWidget(tcRightPane);
    m_taskCenterSplitter->setStretchFactor(0, 22);
    m_taskCenterSplitter->setStretchFactor(1, 78);

    tcLayout->addWidget(m_taskCenterSplitter, 1);

    m_mainTabWidget->addTab(m_taskCenterTab, tr("任務控制中心"));

    connect(m_mainTabWidget, &QTabWidget::currentChanged, this, [this](int) {
        if (!m_mainTabWidget || !m_graphWidget || !m_graphTab) return;
        if (m_mainTabWidget->currentWidget() == m_graphTab) {
            m_graphWidget->buildGraph();
        }
    });

    setupContextMenus();

    m_dirWatcher = new QFileSystemWatcher(this);
    connect(m_dirWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryChanged);
    m_dirDebounceTimer = new QTimer(this);
    m_dirDebounceTimer->setSingleShot(true);
    m_dirDebounceTimer->setInterval(1000);
    connect(m_dirDebounceTimer, &QTimer::timeout, this, [this]() {
        // If AI/background scan is running, skip notification to avoid disruption.
        const bool busy = (watcher && watcher->isRunning())
                          || (initialScanWatcher && initialScanWatcher->isRunning())
                          || (modelLoadWatcher && modelLoadWatcher->isRunning())
                          || (m_consolidateWatcher && m_consolidateWatcher->isRunning())
                          || m_isBatchMode;
        if (busy) return;
        if (rootPath.trimmed().isEmpty()) return;

        scanFiles();
        updateTagList();
        if (m_bgAutoAnalyzeEnabled) {
            ensureRecursiveWatchCoversWorkspace();
        }
    });

    m_bgAutoAnalyzeDebounce = new QTimer(this);
    m_bgAutoAnalyzeDebounce->setSingleShot(true);
    m_bgAutoAnalyzeDebounce->setInterval(2000);
    connect(m_bgAutoAnalyzeDebounce, &QTimer::timeout, this, &MainWindow::onBackgroundAutoAnalyzeDebounce);

    loadBackgroundAutoAnalyzeSetting();

    // 保留至少一個核心給 UI 執行緒
    int idealThreads = QThread::idealThreadCount();
    int maxThreads = qMax(1, idealThreads - 1); // 如果單核就維持 1，多核則減 1
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);

    watcher = new QFutureWatcher<std::string>(this);
    connect(watcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onAnalysisFinished);

    m_consolidateWatcher = new QFutureWatcher<std::string>(this);
    connect(m_consolidateWatcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onTagFolderClustersFinished);

    m_semanticSearchWatcher = new QFutureWatcher<std::string>(this);
    connect(m_semanticSearchWatcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onSemanticSearchFinished);

    m_heroSemanticSpinTimer = new QTimer(this);
    m_heroSemanticSpinTimer->setInterval(120);
    connect(m_heroSemanticSpinTimer, &QTimer::timeout, this, [this]() {
        if (!m_heroSearchBusyChip || !m_heroSearchBusyChip->isVisible()) return;
        m_heroSemanticSpinPhase = (m_heroSemanticSpinPhase + 1) & 3;
        m_heroSearchBusyChip->setPhase(m_heroSemanticSpinPhase);
    });

    m_analysisSpinTimer = new QTimer(this);
    m_analysisSpinTimer->setInterval(130);
    connect(m_analysisSpinTimer, &QTimer::timeout, this, &MainWindow::tickAnalysisSpinner);

    modelLoadWatcher = new QFutureWatcher<bool>(this);
    connect(modelLoadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool ok = modelLoadWatcher->result();
        lblStatus->setText(ok ? LanguageManager::instance().getText(QStringLiteral("模型已自動載入 (Model auto-loaded)"))
                              : LanguageManager::instance().getText(QStringLiteral("模型自動載入失敗 (Auto-load failed)")));
    });

    initialScanWatcher = new QFutureWatcher<void>(this);
    connect(initialScanWatcher, &QFutureWatcher<void>::finished, this, &MainWindow::onBackgroundScanFinished);

    llamaEngine.setCancelFlag(&cancelFlag);

    mapsHomeFixAndSetRoot(QDir::homePath());
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    scanFiles();

    const QString modelPath = resolveModelPath();
    if (!QFile::exists(modelPath)) {
        lblStatus->setText(LanguageManager::instance()
                               .getText(QStringLiteral("❌ 找不到模型: %1（請確認 assets/models/chat_model.gguf）"))
                               .arg(modelPath));
    } else {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("正在自動載入模型… %1")).arg(modelPath));
        modelLoadWatcher->setFuture(QtConcurrent::run([this, modelPath]() {
            return llamaEngine.loadModel(modelPath.toStdString());
        }));
    }

    initialScanWatcher->setFuture(QtConcurrent::run([this]() {
        const QString baseDir = rootPath.isEmpty() ? QDir::homePath() : rootPath;
        int n = 0;
        QDirIterator it(baseDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            const QFileInfo fileInfo(filePath);
            if (!fileInfo.exists()) continue;
            if (fileInfo.isDir()) continue;
            if (fileInfo.isSymLink()) {
                const QString target = fileInfo.symLinkTarget();
                if (!target.isEmpty() && QFileInfo(target).isDir()) continue;
            }

            const QString fileName = fileInfo.fileName();
            const QStringList fastTags = getFastPathTags(fileName);
            if (fastTags.isEmpty()) {
                ++n;
                if ((n % 2000) == 0) {
                    QMetaObject::invokeMethod(
                        this,
                        [this]() { onBackgroundScanProgress(); },
                        Qt::QueuedConnection);
                }
                continue;
            }

            {
                QMutexLocker locker(&tagMutex);
                const auto existingByPath = tagManager.getTags(filePath);
                const auto existingByName = tagManager.getTags(fileName);
                QSet<QString> existingSet;
                for (const auto &t : existingByPath) existingSet.insert(t);
                for (const auto &t : existingByName) existingSet.insert(t);
                for (const QString &t : fastTags) {
                    if (existingSet.contains(t)) continue;
                    tagManager.addTag(filePath, t, false);
                    existingSet.insert(t);
                }
            }
            ++n;
            if ((n % 2000) == 0) {
                QMetaObject::invokeMethod(
                    this,
                    [this]() { onBackgroundScanProgress(); },
                    Qt::QueuedConnection);
            }
        }
        {
            QMutexLocker locker(&tagMutex);
            tagManager.saveTags();
        }
    }));

    resize(1200, 800);
    setWindowTitle(QStringLiteral("Smart File Organizer"));

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() { updateAllTexts(); });
    updateAllTexts();
}

void MainWindow::onDirectoryChanged(const QString &path) {
    const QString clean = QDir::cleanPath(path);
    if (clean.contains(QStringLiteral("/.smartfile")) || clean.contains(QStringLiteral("\\.smartfile"))
        || clean.contains(QStringLiteral("_冗餘檔案待處理區"))) {
        return;
    }
    m_lastDirChangePath = path;
    if (m_dirDebounceTimer) {
        m_dirDebounceTimer->start();
    } else {
        QTimer::singleShot(1000, this, [this]() {
            if (rootPath.trimmed().isEmpty()) return;
            scanFiles();
            updateTagList();
        });
    }

    if (m_bgAutoAnalyzeEnabled && m_bgAutoAnalyzeDebounce && !rootPath.trimmed().isEmpty()) {
        m_bgAutoAnalyzeDebounce->start();
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(rootPath, this);
    connect(&dlg, &SettingsDialog::settingsApplied, this, [this]() {
        loadBackgroundAutoAnalyzeSetting();
        updateAllTexts();
    });
    connect(&dlg, &SettingsDialog::clearAiCacheRequested, this, &MainWindow::onWorkspaceClearAiCache);
    connect(&dlg, &SettingsDialog::clearHashCacheRequested, this, &MainWindow::onWorkspaceClearHashCache);
    connect(&dlg, &SettingsDialog::factoryResetRequested, this, &MainWindow::onWorkspaceFactoryReset);
    const int code = dlg.exec();
    if (code != QDialog::Accepted) return;

    updateAllTexts();
    loadBackgroundAutoAnalyzeSetting();

    const QString newModelPath = dlg.modelPath();
    if (!newModelPath.isEmpty()) {
        // Prevent reloading while inference is active.
        if (watcher && watcher->isRunning()) {
            cancelFlag.store(true);
            watcher->future().waitForFinished();
        }
        if (modelLoadWatcher && modelLoadWatcher->isRunning()) {
            modelLoadWatcher->future().waitForFinished();
        }

        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("正在載入新模型… %1")).arg(newModelPath));
        modelLoadWatcher->setFuture(QtConcurrent::run([this, newModelPath]() {
            return llamaEngine.loadModel(newModelPath.toStdString());
        }));
    }
}

void MainWindow::onWorkspaceClearAiCache()
{
    if (watcher && watcher->isRunning()) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("workspace_clear_busy")));
        return;
    }
    {
        QMutexLocker locker(&tagMutex);
        tagManager.clearAiTagsAndSummaries(true);
    }
    m_aiSummaryByPath.clear();
    m_analysisByContentHash.clear();
    if (m_aiSummaryEdit) m_aiSummaryEdit->clear();
    updateTagList();
    scanFiles();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
        updatePreviewForFile(fp);
    }
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("workspace_clear_ai_done")));
}

void MainWindow::onWorkspaceClearHashCache()
{
    if (watcher && watcher->isRunning()) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("workspace_clear_busy")));
        return;
    }
    {
        QMutexLocker locker(&tagMutex);
        tagManager.clearHashCaches(true);
    }
    m_analysisByContentHash.clear();
    m_aiSummaryByPath.clear();
    if (m_aiSummaryEdit) m_aiSummaryEdit->clear();
    updateTagList();
    scanFiles();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
        updatePreviewForFile(fp);
    }
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("workspace_clear_hash_done")));
}

void MainWindow::onWorkspaceFactoryReset()
{
    if (m_isBatchMode) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("workspace_factory_stop_batch")));
        return;
    }
    if (watcher && watcher->isRunning()) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("workspace_clear_busy")));
        return;
    }
    if (rootPath.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Smartflie"), QStringLiteral("尚未開啟工作區。"));
        return;
    }

    tagManager.factoryResetWorkspaceData();
    if (!rootPath.trimmed().isEmpty()) {
        tagManager.loadTags(rootPath.toStdString());
    }

    m_aiSummaryByPath.clear();
    m_analysisByContentHash.clear();
    m_pendingResults.clear();
    m_analysisQueue.clear();
    m_batchHashToPaths.clear();
    m_batchNameConflictPaths.clear();
    m_currentAnalyzingFile.clear();

    if (m_aiSummaryEdit) m_aiSummaryEdit->clear();
    activeVirtualTag.clear();
    fileListMode = FileListMode::PhysicalFolder;
    if (cmbTagFilter) cmbTagFilter->setCurrentIndex(0);

    updateTagList();
    scanFiles();
    if (m_mainTabWidget && m_workspaceTab) m_mainTabWidget->setCurrentWidget(m_workspaceTab);
    if (m_graphWidget) m_graphWidget->buildGraph();
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("workspace_factory_done")));
}

void MainWindow::updateAllTexts() {
    auto &lm = LanguageManager::instance();

    if (m_mainTabWidget && m_workspaceTab) m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_workspaceTab), lm.getText(QStringLiteral("tab_workspace")));
    if (m_mainTabWidget && m_graphTab) m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_graphTab), lm.getText(QStringLiteral("tab_graph")));

    if (m_actOpenFolder) m_actOpenFolder->setText(lm.getText(QStringLiteral("toolbar_open")));
    if (m_actSettings) m_actSettings->setText(lm.getText(QStringLiteral("toolbar_settings")));

    llamaEngine.setOutputLanguage(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("en_US")
                                                                                   : QStringLiteral("zh_TW"));

    if (btnAnalyzeFile) btnAnalyzeFile->setText(lm.getText(QStringLiteral("btn_analyze")));
    if (btnCancelAnalysis) btnCancelAnalysis->setText(lm.getText(QStringLiteral("btn_cancel")));
    if (btnSaveTags) btnSaveTags->setText(lm.getText(QStringLiteral("btn_save")));
    if (btnAddTag) btnAddTag->setText(lm.getText(QStringLiteral("btn_add_tag")));
    if (btnRemoveTag) btnRemoveTag->setText(lm.getText(QStringLiteral("btn_remove_tag")));
    if (btnAddExistingTag) btnAddExistingTag->setText(lm.getText(QStringLiteral("btn_add_existing_tag")));
    if (btnAutoMergeTags) {
        const QString normalText = lm.language() == LanguageManager::Language::EN_US
                                       ? QStringLiteral("🤖 AI tag folders (Generate Tag Folders)")
                                       : QStringLiteral("🤖 AI 智能標籤分類 (Generate Tag Folders)");
        const QString busyText = lm.language() == LanguageManager::Language::EN_US
                                     ? QStringLiteral("🤖 AI organizing…")
                                     : QStringLiteral("🤖 AI 思考中…");
        btnAutoMergeTags->setText(m_isConsolidatingTags ? busyText : normalText);
        btnAutoMergeTags->setEnabled(!m_isConsolidatingTags);
    }
    if (btnPhysicalArchive) btnPhysicalArchive->setText(lm.getText(QStringLiteral("btn_physical_archive")));
    if (btnUndoPhysicalArchive) btnUndoPhysicalArchive->setText(lm.getText(QStringLiteral("btn_undo_archive")));
    if (m_lblPhysicalArchiveWarning) {
        m_lblPhysicalArchiveWarning->setText(
            lm.getText(QStringLiteral("physical_archive_ui_warning")));
    }
    if (lblTagLibraryTitle) lblTagLibraryTitle->setText(QStringLiteral("🏷️ %1").arg(lm.getText(QStringLiteral("標籤庫"))));
    if (chkRecursive) chkRecursive->setText(lm.getText(QStringLiteral("包含子資料夾")));
    if (lblFolderTreeTitle) lblFolderTreeTitle->setText(QStringLiteral("🗂️ %1").arg(lm.getText(QStringLiteral("資料夾樹"))));
    if (lblFileListTitle) lblFileListTitle->setText(QStringLiteral("📂 %1").arg(lm.getText(QStringLiteral("檔案清單"))));
    updateBackgroundStatusLabel();
    if (lblPreviewTitle) lblPreviewTitle->setText(QStringLiteral("👁️ %1").arg(lm.getText(QStringLiteral("預覽與控制"))));
    if (lblPreviewImage) {
        // Only update the default placeholder text
        if (lblPreviewImage->text().contains(QStringLiteral("選擇檔案以預覽"))
            || lblPreviewImage->text().contains(QStringLiteral("Select a file to preview"))) {
            lblPreviewImage->setText(lm.getText(QStringLiteral("選擇檔案以預覽")));
        }
    }
    if (m_previewTabWidget && m_previewTagTab) {
        m_previewTabWidget->setTabText(m_previewTabWidget->indexOf(m_previewTagTab), lm.getText(QStringLiteral("標籤管理")));
    }
    if (m_previewTabWidget && m_previewOpsTab) {
        m_previewTabWidget->setTabText(m_previewTabWidget->indexOf(m_previewOpsTab), lm.getText(QStringLiteral("檔案操作")));
    }
    if (m_mainTabWidget && m_taskCenterTab) {
        m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_taskCenterTab),
                                    lm.getText(QStringLiteral("tab_task_center")));
    }
    if (m_backgroundLogEdit) {
        m_backgroundLogEdit->setPlaceholderText(lm.getText(QStringLiteral("bg_log_placeholder")));
    }
    if (m_taskCenterCleanBtn) {
        m_taskCenterCleanBtn->setText(lm.getText(QStringLiteral("task_center_clean_selected")));
    }

    if (cmbSort) {
        const int idx = cmbSort->currentIndex();
        cmbSort->blockSignals(true);
        cmbSort->clear();
        cmbSort->addItem(lm.getText(QStringLiteral("依名稱")));
        cmbSort->addItem(lm.getText(QStringLiteral("依日期")));
        cmbSort->addItem(lm.getText(QStringLiteral("依大小")));
        cmbSort->setCurrentIndex(std::max(0, idx));
        cmbSort->blockSignals(false);
    }
    if (m_heroOmnibox) {
        if (lm.language() == LanguageManager::Language::EN_US) {
            m_heroOmnibox->setPlaceholderText(QStringLiteral(
                "🔍 Enter keywords or natural-language semantic search… (e.g. find last year’s finance reports)"));
        } else {
            m_heroOmnibox->setPlaceholderText(QStringLiteral(
                "🔍 輸入關鍵字，或使用自然語言進行語意搜尋... (例如：幫我找出去年關於財務的報告)"));
        }
    }
    if (m_btnSemanticSearch) {
        m_btnSemanticSearch->setText(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("🔍 Search")
                                                                                        : QStringLiteral("🔍 搜尋"));
    }

    // Home title (only when currently in Home mode)
    if (workspaceTitleLabel) {
        const QString homeTitleZh = QStringLiteral("📁 本機磁碟 (Home)");
        const QString homeTitleEn = QStringLiteral("📁 Local Disk (Home)");
        if (workspaceTitleLabel->text() == homeTitleZh || workspaceTitleLabel->text() == homeTitleEn) {
            workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(lm.getText(QStringLiteral("本機磁碟 (Home)"))));
        }
    }

    // Re-render tag list display names (system tags need presentation translation)
    if (m_tagTabWidget && m_systemTagListWidget && m_aiTagTreeWidget) {
        m_tagTabWidget->setTabText(m_tagTabWidget->indexOf(m_systemTagListWidget), lm.getText(QStringLiteral("預設分類 (System Tags)")));
        m_tagTabWidget->setTabText(m_tagTabWidget->indexOf(m_aiTagTreeWidget), lm.getText(QStringLiteral("AI 標籤 (AI Tags)")));
    }

    auto refreshTagLibraryList = [&](QListWidget *activeList) {
        if (!activeList) return;
        const QString allFilesText = lm.getText(QStringLiteral("All Files"));
        if (activeList->count() > 0) {
            auto *it0 = activeList->item(0);
            if (it0 && it0->data(Qt::UserRole).toString() == QStringLiteral("ALL")) {
                it0->setText(allFilesText);
            }
        }
        for (int i = 0; i < activeList->count(); ++i) {
            auto *it = activeList->item(i);
            if (!it) continue;
            const QString role = it->data(Qt::UserRole).toString();
            if (role == QStringLiteral("ALL")) continue;
            const int n = it->data(Qt::UserRole + 1).toInt();
            const QString canon = normalizeDisplayTag(role);
            const QString baseZh = it->data(Qt::UserRole + 2).toString();
            const QString emoji = systemTagEmojiPrefix(canon);
            QString displayName = baseZh.isEmpty()
                                      ? canon
                                      : QStringLiteral("%1 %2").arg(emoji, lm.getText(baseZh));
            displayName = displayName.trimmed();
            it->setText(QStringLiteral("%1 (%2)").arg(displayName).arg(n));
        }
    };
    auto refreshAiTagTreeLabels = [&](QTreeWidget *tree) {
        if (!tree) return;
        const QString allFilesText = lm.getText(QStringLiteral("All Files"));
        std::function<void(QTreeWidgetItem *)> walk;
        walk = [&](QTreeWidgetItem *node) {
            if (!node) return;
            const QString role = node->data(0, Qt::UserRole).toString();
            if (role == QStringLiteral("ALL")) {
                node->setText(0, allFilesText);
            } else if (!role.isEmpty()) {
                const int n = node->data(0, Qt::UserRole + 1).toInt();
                const QString canon = normalizeDisplayTag(role);
                const QString baseZh = node->data(0, Qt::UserRole + 2).toString();
                const QString emoji = systemTagEmojiPrefix(canon);
                QString displayName = baseZh.isEmpty()
                                          ? canon
                                          : QStringLiteral("%1 %2").arg(emoji, lm.getText(baseZh));
                displayName = tagLibraryLabelStripAiBadge(displayName.trimmed());
                const bool isFolder = node->data(0, Qt::UserRole + 3).toInt() == 1;
                if (isFolder)
                    node->setText(0, QStringLiteral("📁 %1 (%2)").arg(displayName).arg(n));
                else
                    node->setText(0, QStringLiteral("🏷️ %1 (%2)").arg(displayName).arg(n));
            }
            for (int i = 0; i < node->childCount(); ++i) walk(node->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    };
    refreshTagLibraryList(m_systemTagListWidget);
    refreshAiTagTreeLabels(m_aiTagTreeWidget);

    if (cmbTagFilter && cmbTagFilter->count() > 0) {
        cmbTagFilter->setItemText(0, lm.getText(QStringLiteral("All Files")));
    }

    if (lblTags) {
        const QString zh = QStringLiteral("標籤: --");
        const QString en = QStringLiteral("Tags: --");
        if (lblTags->text() == zh || lblTags->text() == en) {
            lblTags->setText(QStringLiteral("%1: --").arg(lm.getText(QStringLiteral("標籤"))));
        }
    }
    if (lblStatus) {
        const QString zh = QStringLiteral("狀態: 就緒");
        const QString en = QStringLiteral("Status: Ready");
        if (lblStatus->text() == zh || lblStatus->text() == en) {
            lblStatus->setText(QStringLiteral("%1: %2").arg(lm.getText(QStringLiteral("狀態")), lm.getText(QStringLiteral("就緒"))));
        }
    }


    if (btnLoadMore) {
        btnLoadMore->setText(QStringLiteral("%1 (%2)")
                                 .arg(lm.getText(QStringLiteral("載入更多")))
                                 .arg(BATCH_SIZE));
    }
    if (btnLoadAll) {
        btnLoadAll->setText(lm.getText(QStringLiteral("載入全部")));
    }

    if (btnStopBatchAnalyze) btnStopBatchAnalyze->setText(lm.getText(QStringLiteral("停止")));
    if (m_btnRestartBackgroundAnalyze)
        m_btnRestartBackgroundAnalyze->setText(lm.getText(QStringLiteral("btn_restart_bg_analyze")));

    syncBatchAnalyzeButtonLabel();
    refreshCurrentAnalysisTargetUi();

    if (m_lblSummaryTitle) m_lblSummaryTitle->setText(lm.getText(QStringLiteral("AI 智慧摘要")));
    if (m_aiSummaryEdit) {
        m_aiSummaryEdit->setPlaceholderText(lm.getText(QStringLiteral("尚未分析")));
        if (m_aiSummaryEdit->toPlainText().trimmed().isEmpty()) {
            // Keep it empty; placeholder will show.
        }
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::onBackgroundScanProgress() {
    updateTagListCountsOnly();
}

void MainWindow::onBackgroundScanFinished() {
    updateTagList();
    lblStatus->setText(lblStatus->text() + QStringLiteral(" | %1").arg(LanguageManager::instance().getText(QStringLiteral("背景全域掃描完成"))));
}

void MainWindow::setupToolbar() {
    toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setMovable(false);
    m_actOpenFolder = toolbar->addAction(QStringLiteral("開啟資料夾"));
    connect(m_actOpenFolder, &QAction::triggered, this, &MainWindow::openFolder);

    toolbar->addSeparator();
    m_actSettings = toolbar->addAction(QStringLiteral("⚙️ 設定 (Settings)"));
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::openSettings);
    toolbar->addSeparator();
}

void MainWindow::setupFourColumnLayout() {
    mainSplitter = new QSplitter(Qt::Horizontal, this);

    // --- Column 1: Tags ---
    tagsPanel = new QWidget(this);
    auto *tagsLayout = new QVBoxLayout(tagsPanel);
    auto *tagsHeader = new QHBoxLayout();
    lblTagLibraryTitle = new QLabel(QStringLiteral("🏷️ 標籤庫"), this);
    tagsHeader->addWidget(lblTagLibraryTitle);
    tagsHeader->addStretch(1);
    chkRecursive = new QCheckBox(QStringLiteral("包含子資料夾"), this);
    chkRecursive->setChecked(false);
    connect(chkRecursive, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        if (fileListMode == FileListMode::PhysicalFolder) {
            scanFiles();
            sortFileList();
        }
    });
    tagsHeader->addWidget(chkRecursive);
    tagsLayout->addLayout(tagsHeader);

    m_tagTabWidget = new QTabWidget(this);
    m_systemTagListWidget = new QListWidget(this);
    m_systemTagListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_aiTagTreeWidget = new AiTagDropTreeWidget(this, this);
    connect(m_systemTagListWidget, &QListWidget::itemClicked, this, &MainWindow::onTagSelected);
    connect(m_aiTagTreeWidget, &QTreeWidget::itemClicked, this, &MainWindow::onAiTagTreeItemClicked);
    m_tagTabWidget->addTab(m_systemTagListWidget, LanguageManager::instance().getText(QStringLiteral("預設分類 (System Tags)")));
    m_tagTabWidget->addTab(m_aiTagTreeWidget, LanguageManager::instance().getText(QStringLiteral("AI 標籤 (AI Tags)")));
    tagsLayout->addWidget(m_tagTabWidget);

    auto *tagButtons = new QHBoxLayout();
    btnLeftAddTag = new QPushButton(QStringLiteral("➕"), this);
    btnLeftAddTag->setToolTip(QStringLiteral("新增標籤"));
    connect(btnLeftAddTag, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("Add Tag"), QStringLiteral("New tag:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || t.isEmpty()) return;
        const QString fp = currentFilePath();
        if (!fp.isEmpty()) {
            QMutexLocker locker(&tagMutex);
            tagManager.addTag(fp, t, true);
            tagManager.saveTags();
        }
        updateTagList();
        reloadCurrentFileListPanel();
    });
    tagButtons->addWidget(btnLeftAddTag);

    btnLeftRemoveTag = new QPushButton(QStringLiteral("➖"), this);
    btnLeftRemoveTag->setToolTip(QStringLiteral("刪除標籤（全域）"));
    connect(btnLeftRemoveTag, &QPushButton::clicked, this, &MainWindow::removeGlobalTag);
    tagButtons->addWidget(btnLeftRemoveTag);
    tagsLayout->addLayout(tagButtons);

    mainSplitter->addWidget(tagsPanel);

    // --- Column 2: Folders + nav under title ---
    foldersPanel = new QWidget(this);
    auto *foldersLayout = new QVBoxLayout(foldersPanel);
    lblFolderTreeTitle = new QLabel(QStringLiteral("🗂️ 資料夾樹"), this);
    foldersLayout->addWidget(lblFolderTreeTitle);

    auto *navRow = new QHBoxLayout();
    btnBack = new QPushButton(QStringLiteral("⬅️"), this);
    btnBack->setToolTip(QStringLiteral("上一頁"));
    connect(btnBack, &QPushButton::clicked, this, &MainWindow::goBack);
    navRow->addWidget(btnBack);

    btnForward = new QPushButton(QStringLiteral("➡️"), this);
    btnForward->setToolTip(QStringLiteral("下一頁"));
    connect(btnForward, &QPushButton::clicked, this, &MainWindow::goForward);
    navRow->addWidget(btnForward);

    btnHome = new QPushButton(QStringLiteral("🏠"), this);
    btnHome->setToolTip(QStringLiteral("回首頁（家目錄）"));
    connect(btnHome, &QPushButton::clicked, this, &MainWindow::goHome);
    navRow->addWidget(btnHome);

    navRow->addStretch(1);
    foldersLayout->addLayout(navRow);

    workspaceTitleLabel = new QLabel(QStringLiteral("📁 本機磁碟 (Home)"), this);
    workspaceTitleLabel->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 14px; padding-bottom: 5px; color: palette(windowText);"));
    foldersLayout->addWidget(workspaceTitleLabel);

    folderTree = new QTreeView(this);
    folderTree->setMinimumWidth(250);
    folderTree->setHeaderHidden(true);
    folderTree->setAnimated(true);
    folderTree->setIndentation(18);
    folderTree->setExpandsOnDoubleClick(true);
    folderTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    folderTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    folderModel = new QFileSystemModel(this);
    folderModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    folderModel->setRootPath(QDir::homePath());
    proxyModel = new WorkspaceFilterProxyModel(this);
    proxyModel->setSourceModel(folderModel);
    proxyModel->setWorkspace(QDir::homePath());
    folderTree->setModel(proxyModel);
    for (int col = 1; col < folderModel->columnCount(); ++col) folderTree->hideColumn(col);
    folderTree->header()->setStretchLastSection(false);
    folderTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    folderTree->setItemDelegateForColumn(0, new FolderTreeSpinDelegate(folderTree));

    foldersLayout->addWidget(folderTree);
    connect(folderTree, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        const QModelIndex srcIdx = proxyModel ? proxyModel->mapToSource(idx) : idx;
        const QString selectedDir = folderModel->filePath(srcIdx);
        if (selectedDir.isEmpty()) return;
        QFileInfo fi(selectedDir);
        if (!fi.exists() || !fi.isDir()) return;
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        m_priorityFolderBannerPath = QDir::cleanPath(fi.absoluteFilePath());
        refreshCurrentAnalysisTargetUi();
        navigateToFolder(fi.absoluteFilePath(), true);
        prependUnanalyzedFromFolderToAnalysisQueue(QDir::cleanPath(fi.absoluteFilePath()));
    });

    mainSplitter->addWidget(foldersPanel);

    // --- Column 3: Files (row1 sort, row2 filter+search) ---
    filesPanel = new QWidget(this);
    auto *filesLayout = new QVBoxLayout(filesPanel);
    auto *fileTitleRow = new QHBoxLayout();
    lblFileListTitle = new QLabel(QStringLiteral("📂 檔案清單"), this);
    fileTitleRow->addWidget(lblFileListTitle, 0);
    fileTitleRow->addSpacerItem(
        new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
    lblBackgroundStatus = new QLabel(this);
    lblBackgroundStatus->setVisible(false);
    lblBackgroundStatus->setWordWrap(false);
    lblBackgroundStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblBackgroundStatus->setFixedWidth(220);
    lblBackgroundStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    lblBackgroundStatus->setStyleSheet(QStringLiteral(
        "QLabel { color:#1d4ed8; font-weight:700; font-size:14px; padding-left:12px; }"));
    fileTitleRow->addWidget(lblBackgroundStatus, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_btnRestartBackgroundAnalyze = new QPushButton(this);
    m_btnRestartBackgroundAnalyze->setVisible(false);
    m_btnRestartBackgroundAnalyze->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(m_btnRestartBackgroundAnalyze, &QPushButton::clicked, this, [this]() {
        m_showRestartBackgroundPrompt = false;
        if (m_btnRestartBackgroundAnalyze)
            m_btnRestartBackgroundAnalyze->setVisible(false);
        if (m_bgAutoAnalyzeDebounce && m_bgAutoAnalyzeEnabled && !rootPath.trimmed().isEmpty())
            m_bgAutoAnalyzeDebounce->start();
        updateBackgroundStatusLabel();
    });
    fileTitleRow->addWidget(m_btnRestartBackgroundAnalyze, 0, Qt::AlignRight | Qt::AlignVCenter);

    filesLayout->addLayout(fileTitleRow);

    lblCurrentTarget = new QLabel(this);
    lblCurrentTarget->setWordWrap(false);
    lblCurrentTarget->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lblCurrentTarget->setFixedHeight(lblCurrentTarget->fontMetrics().height() + 6);
    lblCurrentTarget->setMinimumWidth(280);
    lblCurrentTarget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lblCurrentTarget->setStyleSheet(QStringLiteral(
        "QLabel { color: palette(windowText); font-size: 13px; padding: 2px 0 6px 0; }"));
    filesLayout->addWidget(lblCurrentTarget);

    m_semanticGlobalBanner = new QLabel(this);
    m_semanticGlobalBanner->setWordWrap(true);
    m_semanticGlobalBanner->setVisible(false);
    m_semanticGlobalBanner->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_semanticGlobalBanner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_semanticGlobalBanner->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #fff3cd; color: #856404; border: 1px solid #ffc107; "
        "border-radius: 6px; padding: 8px 10px; font-weight: 600; font-size: 13px; }"));
    filesLayout->addWidget(m_semanticGlobalBanner);

    auto *controlsCol = new QVBoxLayout();

    auto *rowSort = new QHBoxLayout();
    cmbSort = new QComboBox(this);
    cmbSort->addItem(QStringLiteral("依名稱"));
    cmbSort->addItem(QStringLiteral("依日期"));
    cmbSort->addItem(QStringLiteral("依大小"));
    connect(cmbSort, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSortChanged);
    rowSort->addWidget(cmbSort);
    rowSort->addStretch(1);
    controlsCol->addLayout(rowSort);

    auto *rowFilter = new QHBoxLayout();
    cmbTagFilter = new QComboBox(this);
    cmbTagFilter->addItem(LanguageManager::instance().getText(QStringLiteral("All Files")), QStringLiteral("ALL"));
    cmbTagFilter->setToolTip(QStringLiteral("🏷️ 標籤篩選"));
    rowFilter->addWidget(cmbTagFilter, 1);
    rowFilter->addStretch(1);
    controlsCol->addLayout(rowFilter);

    filesLayout->addLayout(controlsCol);

    fileList = new QListWidget(this);
    fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    fileList->setItemDelegate(new FileItemDelegate(fileList));
    connect(fileList, &QListWidget::itemClicked, this, &MainWindow::onFileSelected);
    connect(fileList, &QListWidget::customContextMenuRequested, this, &MainWindow::showFileContextMenu);
    connect(fileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString absPath = item->data(Qt::UserRole).toString();
        if (absPath.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
    });

    m_fileListPageStack = new QStackedWidget(this);
    m_fileListPageStack->addWidget(fileList);

    QWidget *semanticBusyPage = new QWidget(this);
    auto *busyLay = new QVBoxLayout(semanticBusyPage);
    busyLay->setContentsMargins(16, 16, 16, 16);
    auto *busyLabel = new QLabel(QStringLiteral("⏳ 正在全域知識庫中進行語意推論，請稍候..."), semanticBusyPage);
    busyLabel->setAlignment(Qt::AlignCenter);
    busyLabel->setWordWrap(true);
    busyLay->addStretch(1);
    busyLay->addWidget(busyLabel);
    busyLay->addStretch(1);
    m_fileListPageStack->addWidget(semanticBusyPage);

    filesLayout->addWidget(m_fileListPageStack, 1);

    auto *loadRow = new QHBoxLayout();
    btnLoadMore = new QPushButton(QStringLiteral("載入更多 (%1)").arg(BATCH_SIZE), this);
    btnLoadAll = new QPushButton(QStringLiteral("載入全部"), this);
    loadRow->addWidget(btnLoadMore);
    loadRow->addWidget(btnLoadAll);
    loadRow->addStretch(1);
    filesLayout->addLayout(loadRow);

    btnLoadMore->hide();
    btnLoadAll->hide();
    connect(btnLoadMore, &QPushButton::clicked, this, [this]() { renderFileListBatch(BATCH_SIZE); });
    connect(btnLoadAll, &QPushButton::clicked, this, [this]() {
        const int remaining = static_cast<int>(m_pendingFilesToDisplay.size()) - m_currentLoadedCount;
        renderFileListBatch(remaining);
    });

    connect(cmbTagFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        syncTagListFromTagFilter();
        filterFiles();
    });

    mainSplitter->addWidget(filesPanel);

    // --- Column 4: Preview ---
    previewPanel = new QWidget(this);
    auto *previewLayout = new QVBoxLayout(previewPanel);
    lblPreviewTitle = new QLabel(QStringLiteral("👁️ 預覽與控制"), this);
    previewLayout->addWidget(lblPreviewTitle);

    lblPreviewImage = new QLabel(QStringLiteral("選擇檔案以預覽"), this);
    lblPreviewImage->setAlignment(Qt::AlignCenter);
    lblPreviewImage->setStyleSheet(QStringLiteral("border: 1px dashed gray; min-height: 200px;"));
    previewLayout->addWidget(lblPreviewImage);

    txtPreviewText = new QTextEdit(this);
    txtPreviewText->setReadOnly(true);
    txtPreviewText->setVisible(false);
    previewLayout->addWidget(txtPreviewText);

    lblTags = new QLabel(QStringLiteral("標籤: --"), this);
    lblTags->setWordWrap(true);
    lblTags->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    previewLayout->addWidget(lblTags);

    m_statusRow = new QWidget(this);
    auto *statusHBox = new QHBoxLayout(m_statusRow);
    statusHBox->setContentsMargins(0, 0, 0, 0);
    statusHBox->setSpacing(8);
    m_statusBusyChip = new BusyChip(m_statusRow);
    m_statusBusyChip->setFixedSize(20, 20);
    m_statusBusyChip->hide();
    statusHBox->addWidget(m_statusBusyChip, 0, Qt::AlignTop);
    lblStatus = new QLabel(QStringLiteral("狀態: 就緒"), this);
    lblStatus->setWordWrap(true);
    statusHBox->addWidget(lblStatus, 1);
    previewLayout->addWidget(m_statusRow);

    m_lblSummaryTitle = new QLabel(QStringLiteral("AI 智慧摘要"), this);
    m_lblSummaryTitle->setStyleSheet(QStringLiteral("font-weight: 700; margin-top: 10px;"));
    previewLayout->addWidget(m_lblSummaryTitle);

    m_aiSummaryEdit = new QTextEdit(this);
    m_aiSummaryEdit->setReadOnly(true);
    m_aiSummaryEdit->setAcceptRichText(false);
    m_aiSummaryEdit->setFixedHeight(88);
    m_aiSummaryEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_aiSummaryEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_aiSummaryEdit->setPlaceholderText(QStringLiteral("尚未分析"));
    previewLayout->addWidget(m_aiSummaryEdit);

    // ===== Batch analyze controls =====
    auto *batchRow = new QHBoxLayout();
    btnBatchAnalyze = new QPushButton(QStringLiteral("資料夾分析"), this);
    connect(btnBatchAnalyze, &QPushButton::clicked, this, [this]() { startBatchAnalysis(); });
    batchRow->addWidget(btnBatchAnalyze);

    btnStopBatchAnalyze = new QPushButton(QStringLiteral("停止"), this);
    btnStopBatchAnalyze->setEnabled(false);
    connect(btnStopBatchAnalyze, &QPushButton::clicked, this, [this]() {
        const bool wasBg = m_batchTriggeredByBackgroundAuto;
        // Abort current inference and stop the queue.
        cancelFlag.store(true);
        stopAnalysisSpinner();
        m_analysisQueue.clear();
        // Default behavior: apply completed results before stopping.
        flushPendingBatchResults();
        m_totalBatchSize = 0;
        m_isBatchMode = false;
        m_batchTriggeredByBackgroundAuto = false;
        m_currentAnalyzingFile.clear();
        m_backgroundAnalyzeFolderLabel.clear();
        m_pendingPrioritySingleFile.clear();
        m_priorityFolderBannerPath.clear();
        m_showRestartBackgroundPrompt = wasBg && m_bgAutoAnalyzeEnabled;
        if (m_btnRestartBackgroundAnalyze)
            m_btnRestartBackgroundAnalyze->setVisible(m_showRestartBackgroundPrompt);
        updateBackgroundStatusLabel();
        syncBatchProgressBars();
        if (btnStopBatchAnalyze) btnStopBatchAnalyze->setEnabled(false);
        if (btnBatchAnalyze) btnBatchAnalyze->setEnabled(true);
        if (btnAnalyzeFile) btnAnalyzeFile->setEnabled(!currentFilePath().isEmpty());
        syncBatchAnalyzeButtonLabel();
        refreshCurrentAnalysisTargetUi();
        // Refresh once so UI reflects applied results.
        updateTagList();
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("已停止資料夾分析")));
        scanFiles();
        showFolderAnalysisReport();
        refreshFileAndFolderAnalysisIndicators();
        ensureAnalysisIndicatorTimer();
    });
    batchRow->addWidget(btnStopBatchAnalyze);
    batchRow->addStretch(1);
    previewLayout->addLayout(batchRow);

    batchProgressBar = new QProgressBar(this);
    batchProgressBar->setVisible(false);
    batchProgressBar->setTextVisible(true);
    batchProgressBar->setFixedHeight(24);
    batchProgressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    batchProgressBar->setFormat(QStringLiteral("%p%"));
    batchProgressBar->setAlignment(Qt::AlignCenter);
    // Ensure visible on dark backgrounds (macOS can be subtle at 0%).
    batchProgressBar->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  border: 1px solid rgba(255,255,255,70);"
        "  border-radius: 8px;"
        "  background: rgba(255,255,255,10);"
        "  padding: 2px;"               /* inner gap between border and fill */
        "  color: rgba(255,255,255,230);"
        "  font-weight: 600;"
        "}"
        "QProgressBar::chunk {"
        "  background: #2b6cb0;"
        "  border-radius: 6px;"
        "  margin: 0px;"                /* keep fill flush within padded area */
        "}"));
    previewLayout->addWidget(batchProgressBar);

    lblBatchStatus = new QLabel(QString(), this);
    lblBatchStatus->setVisible(false);
    lblBatchStatus->setWordWrap(true);
    previewLayout->addWidget(lblBatchStatus);

    // ===== Tabbed controls (Tag Management / File Operations) =====
    m_previewTabWidget = new QTabWidget(this);
    m_previewTagTab = new QWidget(this);
    m_previewOpsTab = new QWidget(this);
    m_previewTabWidget->addTab(m_previewTagTab, QStringLiteral("標籤管理"));
    m_previewTabWidget->addTab(m_previewOpsTab, QStringLiteral("檔案操作"));

    auto *tagGroupLayout = new QVBoxLayout(m_previewTagTab);

    auto *tagRow1 = new QHBoxLayout();
    btnSaveTags = new QPushButton(QStringLiteral("💾 儲存"), this);
    connect(btnSaveTags, &QPushButton::clicked, this, &MainWindow::saveTags);
    btnSaveTags->setEnabled(false);
    tagRow1->addWidget(btnSaveTags);
    tagRow1->addStretch(1);
    tagGroupLayout->addLayout(tagRow1);

    auto *tagRow2 = new QHBoxLayout();
    btnAddTag = new QPushButton(QStringLiteral("➕ 加入標籤"), this);
    connect(btnAddTag, &QPushButton::clicked, this, &MainWindow::addTag);
    tagRow2->addWidget(btnAddTag);

    btnRemoveTag = new QPushButton(QStringLiteral("➖ 移除標籤"), this);
    connect(btnRemoveTag, &QPushButton::clicked, this, &MainWindow::removeTag);
    tagRow2->addWidget(btnRemoveTag);
    tagRow2->addStretch(1);
    tagGroupLayout->addLayout(tagRow2);

    btnAddExistingTag = new QPushButton(QStringLiteral("🏷️ 加入現有標籤"), this);
    tagGroupLayout->addWidget(btnAddExistingTag);
    rebuildAddExistingTagMenu();

    btnAutoMergeTags = new QPushButton(QStringLiteral("🤖 AI 智能標籤分類 (Generate Tag Folders)"), this);
    btnAutoMergeTags->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnAutoMergeTags->setMinimumHeight(36);
    btnAutoMergeTags->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  font-weight: 700;"
        "  border-radius: 10px;"
        "  padding: 8px 12px;"
        "}"
        "QPushButton:enabled {"
        "  background: rgba(43,108,176,0.25);"
        "  border: 1px solid rgba(43,108,176,0.55);"
        "}"
        "QPushButton:hover:enabled {"
        "  background: rgba(43,108,176,0.35);"
        "}"
        "QPushButton:disabled {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  color: rgba(255,255,255,0.55);"
        "}"));
    connect(btnAutoMergeTags, &QPushButton::clicked, this, &MainWindow::generateTagFoldersWithAI);
    tagGroupLayout->addWidget(btnAutoMergeTags);
    tagGroupLayout->addStretch(1);

    auto *fileGroupLayout = new QVBoxLayout(m_previewOpsTab);

    auto *analysisRow = new QHBoxLayout();
    btnAnalyzeFile = new QPushButton(QStringLiteral("✨ 分析"), this);
    connect(btnAnalyzeFile, &QPushButton::clicked, this, &MainWindow::analyzeFile);
    analysisRow->addWidget(btnAnalyzeFile);

    btnCancelAnalysis = new QPushButton(QStringLiteral("⛔ 取消"), this);
    connect(btnCancelAnalysis, &QPushButton::clicked, this, &MainWindow::cancelAnalysis);
    btnCancelAnalysis->setEnabled(false);
    analysisRow->addWidget(btnCancelAnalysis);
    analysisRow->addStretch(1);
    fileGroupLayout->addLayout(analysisRow);

    auto *archiveRow = new QHBoxLayout();
    btnPhysicalArchive = new QPushButton(QStringLiteral("實體歸檔 (依標籤)"), this);
    connect(btnPhysicalArchive, &QPushButton::clicked, this, &MainWindow::physicalArchiveFiles);
    archiveRow->addWidget(btnPhysicalArchive);

    btnUndoPhysicalArchive = new QPushButton(QStringLiteral("回上一步 (復原歸檔)"), this);
    connect(btnUndoPhysicalArchive, &QPushButton::clicked, this, &MainWindow::undoLastPhysicalArchive);
    btnUndoPhysicalArchive->setEnabled(false);
    archiveRow->addWidget(btnUndoPhysicalArchive);
    archiveRow->addStretch(1);
    fileGroupLayout->addLayout(archiveRow);

    m_lblPhysicalArchiveWarning = new QLabel(this);
    m_lblPhysicalArchiveWarning->setWordWrap(true);
    m_lblPhysicalArchiveWarning->setTextFormat(Qt::PlainText);
    m_lblPhysicalArchiveWarning->setStyleSheet(QStringLiteral(
        "QLabel { color: #dc2626; font-weight: 700; font-size: 12px; padding: 6px 4px; }"));
    fileGroupLayout->addWidget(m_lblPhysicalArchiveWarning);

    fileGroupLayout->addStretch(1);

    previewLayout->addWidget(m_previewTabWidget);

    previewLayout->addStretch(1);
    mainSplitter->addWidget(previewPanel);

    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 4);
    mainSplitter->setStretchFactor(3, 3);

    syncNavigationButtons();
}

void MainWindow::setupContextMenus() {
    auto *systemList = m_systemTagListWidget;
    auto *aiTree = m_aiTagTreeWidget;
    if (!systemList || !aiTree) return;

    auto attachMenu = [this](QListWidget *list) {
        connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint &pos) {
            QListWidgetItem *it = list->itemAt(pos);
        if (!it) return;
        if (it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) return;

        const QString rawTag = it->data(Qt::UserRole).toString();
        if (rawTag.isEmpty()) return;

        QMenu menu(this);
        auto &lm = LanguageManager::instance();
        QAction *actRename = menu.addAction(lm.getText(QStringLiteral("重新命名")));
        QAction *actDelete = menu.addAction(lm.getText(QStringLiteral("刪除（全域）")));
        QAction *actMerge = menu.addAction(lm.getText(QStringLiteral("合併標籤至...")));
            QAction *chosen = menu.exec(list->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actRename) {
            bool ok = false;
            const QString newTag = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"), QLineEdit::Normal, rawTag, &ok).trimmed();
            if (!ok || newTag.isEmpty() || newTag == rawTag) return;
            QMutexLocker locker(&tagMutex);
            tagManager.renameTag(rawTag, newTag);
            tagManager.saveTags();
        } else if (chosen == actMerge) {
            // Pick a target tag (exclude current). Show labels without “[AI] ”; map back to real keys for mergeTag.
            std::vector<QString> all;
            {
                QMutexLocker locker(&tagMutex);
                all = tagManager.getAllTags();
            }
            QStringList realCandidates;
            realCandidates.reserve(static_cast<int>(all.size()));
            for (const auto &t : all) {
                if (t == rawTag) continue;
                realCandidates << t;
            }
            realCandidates.removeDuplicates();
            realCandidates.sort(Qt::CaseInsensitive);
            if (realCandidates.isEmpty()) return;

            QStringList displayChoices;
            displayChoices.reserve(realCandidates.size());
            std::vector<QString> realByRow;
            realByRow.reserve(static_cast<size_t>(realCandidates.size()));
            QSet<QString> usedLabels;
            for (const QString &real : realCandidates) {
                const QString baseLabel = mergeTargetPickerLabel(real);
                QString show = baseLabel;
                if (usedLabels.contains(show)) {
                    show = QStringLiteral("%1  <%2>").arg(baseLabel, real);
                }
                usedLabels.insert(show);
                displayChoices << show;
                realByRow.push_back(real);
            }

            bool ok = false;
            const QString picked = QInputDialog::getItem(
                this,
                lm.getText(QStringLiteral("合併標籤至...")),
                lm.getText(QStringLiteral("選擇目標標籤:")),
                displayChoices,
                0,
                false,
                &ok);
            if (!ok || picked.trimmed().isEmpty()) return;

            const int ix = displayChoices.indexOf(picked);
            if (ix < 0 || ix >= static_cast<int>(realByRow.size())) return;
            const QString target = realByRow[static_cast<size_t>(ix)];
            if (target.trimmed().isEmpty() || target == rawTag) return;

            {
                QMutexLocker locker(&tagMutex);
                tagManager.mergeTag(rawTag, target);
                tagManager.saveTags();
            }
        } else if (chosen == actDelete) {
            QMutexLocker locker(&tagMutex);
            tagManager.deleteTag(rawTag);
            tagManager.saveTags();
        }
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        updateTagList();
        scanFiles();
        });
    };

    attachMenu(systemList);

    connect(aiTree, &QTreeWidget::customContextMenuRequested, this, [this, aiTree](const QPoint &pos) {
        QTreeWidgetItem *it = aiTree->itemAt(pos);
        if (!it) return;
        if (it->data(0, Qt::UserRole).toString() == QStringLiteral("ALL")) return;
        const QString rawTag = it->data(0, Qt::UserRole).toString();
        if (rawTag.isEmpty()) return;

        auto isUnderTag = [this](QString walk, const QString &ancestor) -> bool {
            QSet<QString> seen;
            while (!walk.isEmpty()) {
                if (walk == ancestor) return true;
                if (seen.contains(walk)) break;
                seen.insert(walk);
                QString p;
                {
                    QMutexLocker locker(&tagMutex);
                    p = tagManager.tagParent(walk);
                }
                walk = p;
            }
            return false;
        };

        QMenu menu(this);
        auto &lm = LanguageManager::instance();
        QAction *actRename = menu.addAction(lm.getText(QStringLiteral("重新命名")));
        QAction *actGroup = menu.addAction(lm.getText(QStringLiteral("tag_group_under")));
        QAction *actMerge = menu.addAction(lm.getText(QStringLiteral("合併標籤至...")));
        QAction *actDelete = menu.addAction(lm.getText(QStringLiteral("刪除（全域）")));
        QAction *chosen = menu.exec(aiTree->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actRename) {
            bool ok = false;
            const QString newTag = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"),
                                                         QLineEdit::Normal, rawTag, &ok)
                                     .trimmed();
            if (!ok || newTag.isEmpty() || newTag == rawTag) return;
            QMutexLocker locker(&tagMutex);
            tagManager.renameTag(rawTag, newTag);
            tagManager.saveTags();
        } else if (chosen == actGroup) {
            std::vector<QString> all;
            {
                QMutexLocker locker(&tagMutex);
                all = tagManager.getAllTags();
            }
            QStringList parents;
            for (const QString &t : all) {
                if (!TagManager::hasAiPrefix(t) || t == rawTag) continue;
                if (isUnderTag(t, rawTag)) continue;
                parents << t;
            }
            parents.removeDuplicates();
            if (parents.isEmpty()) {
                QMessageBox::information(this, lm.getText(QStringLiteral("tag_group_under")),
                                         lm.getText(QStringLiteral("tag_group_under_none")));
                return;
            }
            std::sort(parents.begin(), parents.end(), [](const QString &a, const QString &b) {
                return a.localeAwareCompare(b) < 0;
            });
            QStringList labels;
            for (const QString &t : parents) labels << mergeTargetPickerLabel(t);
            bool ok = false;
            const QString picked = QInputDialog::getItem(this, lm.getText(QStringLiteral("tag_group_under")),
                                                         lm.getText(QStringLiteral("tag_pick_parent")), labels, 0,
                                                         false, &ok);
            if (!ok) return;
            const int ix = labels.indexOf(picked);
            if (ix < 0 || ix >= parents.size()) return;
            const QString parentTag = parents.at(ix);
            {
                QMutexLocker locker(&tagMutex);
                if (!tagManager.setAiTagParent(rawTag, parentTag, true)) {
                    QMessageBox::warning(this, lm.getText(QStringLiteral("tag_group_under")),
                                         lm.getText(QStringLiteral("tag_group_under_invalid")));
                }
            }
        } else if (chosen == actMerge) {
            std::vector<QString> all;
            {
                QMutexLocker locker(&tagMutex);
                all = tagManager.getAllTags();
            }
            QStringList realCandidates;
            for (const QString &t : all) {
                if (t == rawTag) continue;
                realCandidates << t;
            }
            realCandidates.removeDuplicates();
            realCandidates.sort(Qt::CaseInsensitive);
            if (realCandidates.isEmpty()) return;
            QStringList displayChoices;
            std::vector<QString> realByRow;
            QSet<QString> usedLabels;
            for (const QString &real : realCandidates) {
                const QString baseLabel = mergeTargetPickerLabel(real);
                QString show = baseLabel;
                if (usedLabels.contains(show)) show = QStringLiteral("%1  <%2>").arg(baseLabel, real);
                usedLabels.insert(show);
                displayChoices << show;
                realByRow.push_back(real);
            }
            bool ok = false;
            const QString picked = QInputDialog::getItem(this, lm.getText(QStringLiteral("合併標籤至...")),
                                                         lm.getText(QStringLiteral("選擇目標標籤:")), displayChoices, 0,
                                                         false, &ok);
            if (!ok || picked.trimmed().isEmpty()) return;
            const int ix = displayChoices.indexOf(picked);
            if (ix < 0 || ix >= static_cast<int>(realByRow.size())) return;
            const QString target = realByRow[static_cast<size_t>(ix)];
            if (target.trimmed().isEmpty() || target == rawTag) return;
            {
                QMutexLocker locker(&tagMutex);
                tagManager.mergeTag(rawTag, target);
                tagManager.saveTags();
            }
        } else if (chosen == actDelete) {
            std::vector<QString> ch;
            {
                QMutexLocker locker(&tagMutex);
                ch = tagManager.directChildTags(rawTag);
            }
            if (!ch.empty()) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Question);
                box.setWindowTitle(lm.getText(QStringLiteral("刪除（全域）")));
                box.setText(lm.getText(QStringLiteral("tag_delete_parent_has_children")).arg(rawTag));
                QPushButton *bDissolve =
                    box.addButton(lm.getText(QStringLiteral("tag_delete_dissolve")), QMessageBox::AcceptRole);
                QPushButton *bCascade =
                    box.addButton(lm.getText(QStringLiteral("tag_delete_cascade")), QMessageBox::DestructiveRole);
                box.addButton(QMessageBox::Cancel);
                box.setDefaultButton(QMessageBox::Cancel);
                box.exec();
                if (box.clickedButton() == bDissolve) {
                    QMutexLocker locker(&tagMutex);
                    tagManager.deleteTagDissolveChildren(rawTag, true);
                } else if (box.clickedButton() == bCascade) {
                    QMutexLocker locker(&tagMutex);
                    tagManager.deleteTagCascadeAi(rawTag, true);
                } else {
                    return;
                }
            } else {
                QMutexLocker locker(&tagMutex);
                tagManager.deleteTag(rawTag);
                tagManager.saveTags();
            }
        }
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        updateTagList();
        scanFiles();
    });
}

void MainWindow::showFileContextMenu(const QPoint &pos) {
    // 1. 確認事件迴圈是否成功捕捉到訊號
    qDebug() << "[Debug] 觸發右鍵選單，接收到 Viewport 座標：" << pos;

    if (!fileList) {
        qDebug() << "[Error] fileList 元件不存在或未初始化。";
        return;
    }

    // 2. 執行 Hit-Testing
    QListWidgetItem *item = fileList->itemAt(pos);
    if (!item) {
        // 如果點到空白處，給予明確提示，而不是安靜地 return
        qDebug() << "[Debug] 點擊到空白區域，沒有選中任何具體檔案項目。";
        return;
    }

    // 3. 【關鍵修正】強制將右鍵點擊的項目設為「當前選取」狀態
    fileList->setCurrentItem(item);

    const QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) {
        qDebug() << "[Error] 選中項目未綁定有效的文件路徑資料。";
        return;
    }

    qDebug() << "[Debug] 成功鎖定檔案，準備彈出選單：" << filePath;

    // 4. 建立並配置右鍵選單
    QMenu menu(this);
    auto &lm = LanguageManager::instance();
    QAction *actRename = menu.addAction(lm.getText(QStringLiteral("重新命名")));
    QAction *actDelete = menu.addAction(lm.getText(QStringLiteral("刪除")));
    menu.addSeparator();
    QAction *actReveal = menu.addAction(lm.getText(QStringLiteral("在資料夾中顯示")));

    // 5. 將 Viewport 座標轉換為全域螢幕座標並阻塞執行
    QAction *chosen = menu.exec(fileList->viewport()->mapToGlobal(pos));
    if (!chosen) {
        qDebug() << "[Debug] 使用者取消了選單。";
        return;
    }

    // 6. 處理對應的 Action 邏輯
    if (chosen == actRename) {
        const QFileInfo oldInfo(filePath);
        const QString oldName = oldInfo.fileName();
        bool ok = false;

        const QString newName = QInputDialog::getText(
                                    this,
                                    lm.getText(QStringLiteral("重新命名")),
                                    lm.getText(QStringLiteral("新的檔名：")),
                                    QLineEdit::Normal,
                                    oldName,
                                    &ok)
                                    .trimmed();

        if (!ok || newName.isEmpty() || newName == oldName) return;

        const QString newPath = oldInfo.dir().filePath(newName);

        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("目標檔名已存在。"));
            return;
        }

        if (!QFile::rename(filePath, newPath)) {
            QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("檔案可能被占用或沒有權限。"));
            return;
        }

        const QFileInfo newInfo(newPath);
        item->setText(newInfo.fileName());
        item->setData(Qt::UserRole, newPath);

        if (fileListMode == FileListMode::VirtualTag || fileListMode == FileListMode::SemanticResults) {
            QString relativePath = QDir(rootPath).relativeFilePath(newInfo.absolutePath());
            if (relativePath == QStringLiteral(".")) relativePath = QStringLiteral("根目錄");
            item->setData(Qt::UserRole + 1, relativePath);
        }
        onFileSelected(item);
        qDebug() << "[Debug] 重新命名成功：" << newName;
        return;
    }

    if (chosen == actDelete) {
        const int ret = QMessageBox::question(
            this,
            QStringLiteral("刪除確認"),
            QStringLiteral("確定要刪除「%1」嗎？").arg(QFileInfo(filePath).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        QFile file(filePath);
        bool removed = file.moveToTrash();
        if (!removed) removed = file.remove();

        if (!removed) {
            QMessageBox::warning(this, QStringLiteral("刪除失敗"), QStringLiteral("檔案可能被鎖定或沒有權限。"));
            return;
        }

        delete fileList->takeItem(fileList->row(item));
        qDebug() << "[Debug] 檔案刪除成功：" << filePath;
        return;
    }

    if (chosen == actReveal) {
        const QFileInfo fi(filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        qDebug() << "[Debug] 開啟檔案位置：" << fi.absolutePath();
    }
}

// File operation buttons (Rename/Delete/Reveal) were removed in favor of the right-click context menu.

namespace {
QString sanitizeTagFolderName(const QString &tag) {
    QString s = tag.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("_未命名標籤");
    }
    const QString invalid = QStringLiteral("<>:\"/\\|?*\r\n");
    for (QChar c : invalid) {
        s.replace(c, QLatin1Char('_'));
    }
    if (s == QLatin1String(".") || s == QLatin1String("..")) {
        s = QLatin1Char('_') + s;
    }
    return s;
}
} // namespace

void MainWindow::physicalArchiveFiles() {
    auto &lm = LanguageManager::instance();
    if (rootPath.isEmpty()) {
        QMessageBox::information(this, lm.getText(QStringLiteral("physical_archive_confirm_title")),
                                 lm.getText(QStringLiteral("physical_archive_need_workspace")));
        return;
    }

    const QString rootClean = QDir::cleanPath(rootPath);
    const QString homeClean = QDir::cleanPath(QDir::homePath());
    const QString desktopClean = QDir::cleanPath(QDir(homeClean).filePath(QStringLiteral("Desktop")));

    const bool isRootDir = QDir(rootClean).isRoot()
#ifdef Q_OS_WIN
                           || QRegularExpression(QStringLiteral("^[A-Za-z]:/$")).match(rootClean + QLatin1Char('/')).hasMatch()
#endif
        ;
    const bool isHighRisk = isRootDir || rootClean == homeClean || rootClean == desktopClean;
    if (isHighRisk) {
        QMessageBox::critical(
            this,
            lm.getText(QStringLiteral("physical_archive_confirm_title")),
            lm.getText(QStringLiteral("physical_archive_high_risk")));
        return;
    }

    const int answer = QMessageBox::warning(
        this,
        lm.getText(QStringLiteral("physical_archive_confirm_title")),
        lm.getText(QStringLiteral("physical_archive_confirm_body")).arg(rootPath),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    m_lastMoveHistory.clear();
    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(false);
    }

    std::vector<std::pair<QString, QString>> pairs;
    {
        QMutexLocker locker(&tagMutex);
        pairs = tagManager.taggedFilesWithPrimaryTag();
    }

    for (const auto &entry : pairs) {
        const QString srcPath = QDir::cleanPath(entry.first);
        const QString &rawTag = entry.second;

        const QFileInfo fiSrc(srcPath);
        if (!fiSrc.exists() || !fiSrc.isFile()) {
            qDebug() << "physicalArchiveFiles: skip (missing or not a file):" << srcPath;
            continue;
        }

        const QString rel = QDir(rootClean).relativeFilePath(fiSrc.absoluteFilePath());
        if (rel.startsWith(QStringLiteral(".."))) {
            qDebug() << "physicalArchiveFiles: skip (outside root):" << srcPath;
            continue;
        }
        if (rel == QStringLiteral(".smartfile") || rel.startsWith(QStringLiteral(".smartfile/"))) {
            qDebug() << "physicalArchiveFiles: skip (.smartfile):" << srcPath;
            continue;
        }

        const QString folderName = sanitizeTagFolderName(rawTag);
        const QString destDir = QDir(rootClean).absoluteFilePath(folderName);
        const QString destPath = QDir(destDir).absoluteFilePath(fiSrc.fileName());

        if (QDir::cleanPath(fiSrc.absolutePath()) == QDir::cleanPath(destDir)) {
            continue;
        }

        if (QFile::exists(destPath)) {
            qDebug() << "physicalArchiveFiles: skip (target exists):" << destPath;
            continue;
        }

        if (!QDir().mkpath(destDir)) {
            qDebug() << "physicalArchiveFiles: mkpath failed:" << destDir;
            continue;
        }

        QFile f(srcPath);
        if (!f.rename(destPath)) {
            qDebug() << "physicalArchiveFiles: rename failed" << srcPath << "->" << destPath << f.errorString();
            continue;
        }

        m_lastMoveHistory.push_back(qMakePair(destPath, srcPath));

        {
            QMutexLocker locker(&tagMutex);
            tagManager.relocateFilePath(srcPath, destPath, false);
        }
    }

    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }

    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(!m_lastMoveHistory.isEmpty());
    }

    scanFiles();
    updateTagList();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
    }
}

void MainWindow::undoLastPhysicalArchive() {
    if (m_lastMoveHistory.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("回上一步 (復原歸檔)"), QStringLiteral("沒有可復原的歸檔紀錄。"));
        return;
    }

    const int answer = QMessageBox::question(
        this,
        QStringLiteral("回上一步 (復原歸檔)"),
        QStringLiteral("將會把上一輪實體歸檔移動的檔案全部搬回原路徑。是否繼續？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    bool movedAny = false;
    for (const auto &p : m_lastMoveHistory) {
        const QString newPath = QDir::cleanPath(p.first);
        const QString oldPath = QDir::cleanPath(p.second);

        const QFileInfo fiNew(newPath);
        if (!fiNew.exists() || !fiNew.isFile()) {
            qDebug() << "undoLastPhysicalArchive: skip (missing):" << newPath;
            continue;
        }

        if (QFile::exists(oldPath)) {
            qDebug() << "undoLastPhysicalArchive: skip (old path exists):" << oldPath;
            continue;
        }

        const QString oldDir = QFileInfo(oldPath).absolutePath();
        if (!QDir().mkpath(oldDir)) {
            qDebug() << "undoLastPhysicalArchive: mkpath failed:" << oldDir;
            continue;
        }

        QFile f(newPath);
        if (!f.rename(oldPath)) {
            qDebug() << "undoLastPhysicalArchive: rename failed" << newPath << "->" << oldPath << f.errorString();
            continue;
        }

        movedAny = true;
        {
            QMutexLocker locker(&tagMutex);
            tagManager.relocateFilePath(newPath, oldPath, false);
        }
    }

    if (movedAny) {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }

    m_lastMoveHistory.clear();
    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(false);
    }

    scanFiles();
    updateTagList();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
    }
}

void MainWindow::mapsHomeFixAndSetRoot(const QString &dir) {
    QFileInfo fi(dir);
    const QString abs = fi.exists() ? fi.absoluteFilePath() : QDir::homePath();
    rootPath = abs;
    currentPath = abs;

    if (m_dirWatcher) {
        const QStringList oldDirs = m_dirWatcher->directories();
        if (!oldDirs.isEmpty()) m_dirWatcher->removePaths(oldDirs);
    }
    m_recursiveWatchPaths.clear();

    folderModel->setRootPath(rootPath);
    if (proxyModel) {
        proxyModel->setWorkspace(rootPath);
        const QString parentDir = QFileInfo(rootPath).path();
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(parentDir)));
    }
    setFolderTreeCurrentPath(rootPath);

    tagManager.loadTags(rootPath.toStdString());
    disableSemanticOverlays();
    if (m_fileListPageStack)
        m_fileListPageStack->setCurrentIndex(0);
    if (fileListMode == FileListMode::SemanticResults)
        fileListMode = FileListMode::PhysicalFolder;
    loadAiUiDrawerAssignments();
    m_analysisByContentHash.clear();
    {
        QMutexLocker locker(&tagMutex);
        tagManager.exportHashAnalysisCache(&m_analysisByContentHash);
    }
    applyFilesystemWatchPolicy();
    ensureRecursiveWatchCoversWorkspace();
    updateBackgroundStatusLabel();
    if (m_bgAutoAnalyzeEnabled && m_bgAutoAnalyzeDebounce && !rootPath.trimmed().isEmpty())
        m_bgAutoAnalyzeDebounce->start();
}

void MainWindow::setFolderTreeCurrentPath(const QString &absDir) {
    const QModelIndex srcIdx = folderModel->index(absDir);
    const QModelIndex idx = proxyModel ? proxyModel->mapFromSource(srcIdx) : srcIdx;
    if (idx.isValid()) {
        folderTree->setCurrentIndex(idx);
        folderTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        folderTree->expand(idx);
    }
}

void MainWindow::pushHistory(const QString &path) {
    if (path.isEmpty()) return;
    if (navIndex >= 0 && navIndex < navHistory.size() && navHistory[navIndex] == path) {
        syncNavigationButtons();
        return;
    }
    while (navHistory.size() > navIndex + 1) navHistory.removeLast();
    navHistory.push_back(path);
    navIndex = navHistory.size() - 1;
    syncNavigationButtons();
}

void MainWindow::syncNavigationButtons() {
    btnBack->setEnabled(navIndex > 0);
    btnForward->setEnabled(navIndex >= 0 && navIndex + 1 < navHistory.size());
}

void MainWindow::navigateToFolder(const QString &path, bool pushToHistory) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) return;
    disableSemanticOverlays();
    if (m_fileListPageStack)
        m_fileListPageStack->setCurrentIndex(0);
    if (fileListMode == FileListMode::SemanticResults)
        fileListMode = FileListMode::PhysicalFolder;
    currentPath = fi.absoluteFilePath();
    if (pushToHistory) pushHistory(currentPath);
    setFolderTreeCurrentPath(currentPath);
    scanFiles();
    sortFileList();
}

void MainWindow::goBack() {
    if (navIndex <= 0) return;
    --navIndex;
    const QString path = navHistory[navIndex];
    currentPath = path;
    disableSemanticOverlays();
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    setFolderTreeCurrentPath(currentPath);
    syncNavigationButtons();
    scanFiles();
    sortFileList();
}

void MainWindow::goForward() {
    if (navIndex + 1 >= navHistory.size()) return;
    ++navIndex;
    const QString path = navHistory[navIndex];
    currentPath = path;
    disableSemanticOverlays();
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    setFolderTreeCurrentPath(currentPath);
    syncNavigationButtons();
    scanFiles();
    sortFileList();
}

void MainWindow::goHome() {
    const QString home = QDir::homePath();
    rootPath = home;
    currentPath = home;

    if (m_dirWatcher) {
        const QStringList oldDirs = m_dirWatcher->directories();
        if (!oldDirs.isEmpty()) m_dirWatcher->removePaths(oldDirs);
    }
    m_recursiveWatchPaths.clear();

    folderModel->setRootPath(rootPath);
    if (workspaceTitleLabel) workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(LanguageManager::instance().getText(QStringLiteral("本機磁碟 (Home)"))));
    if (proxyModel) {
        const QString homeParent = QFileInfo(home).path();
        proxyModel->setWorkspace(home);
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(homeParent)));
    }
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    setFolderTreeCurrentPath(currentPath);

    tagManager.loadTags(rootPath.toStdString());
    loadAiUiDrawerAssignments();
    m_analysisByContentHash.clear();
    {
        QMutexLocker locker(&tagMutex);
        tagManager.exportHashAnalysisCache(&m_analysisByContentHash);
    }
    applyFilesystemWatchPolicy();

    scanFiles();
    sortFileList();
}

void MainWindow::onSortChanged(int) {
    sortFileList();
}

void MainWindow::sortFileList() {
    if (!fileList) return;
    const int mode = cmbSort ? cmbSort->currentIndex() : 0;
    if (mode == 0) {
        QList<QListWidgetItem *> items;
        for (int i = 0; i < fileList->count(); ++i) items.push_back(fileList->takeItem(0));
        std::sort(items.begin(), items.end(), [](QListWidgetItem *a, QListWidgetItem *b) {
            const QString pa = a->data(Qt::UserRole).toString();
            const QString pb = b->data(Qt::UserRole).toString();
            return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
        });
        for (auto *it : items) fileList->addItem(it);
        return;
    }

    QList<QListWidgetItem *> items;
    for (int i = 0; i < fileList->count(); ++i) items.push_back(fileList->takeItem(0));

    std::sort(items.begin(), items.end(), [mode](QListWidgetItem *a, QListWidgetItem *b) {
        const QString pa = a->data(Qt::UserRole).toString();
        const QString pb = b->data(Qt::UserRole).toString();
        const QFileInfo fa(pa);
        const QFileInfo fb(pb);
        if (mode == 1) {
            const auto ta = fa.lastModified();
            const auto tb = fb.lastModified();
            if (ta == tb) return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
            return ta > tb;
        }
        const qint64 sa = fa.size();
        const qint64 sb = fb.size();
        if (sa == sb) return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
        return sa > sb;
    });

    for (auto *it : items) fileList->addItem(it);
}

void MainWindow::openFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("選擇資料夾"),
        rootPath.isEmpty() ? QDir::homePath() : rootPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    mapsHomeFixAndSetRoot(dir);
    const QString folderName = QFileInfo(dir).fileName().isEmpty() ? dir : QFileInfo(dir).fileName();
    if (workspaceTitleLabel) workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(folderName));
    if (proxyModel) {
        proxyModel->setWorkspace(dir);
        const QString parentDir = QFileInfo(dir).path();
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(parentDir)));
    }
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    scanFiles();
    sortFileList();
}

QString MainWindow::currentFilePath() const {
    const auto selected = fileList->selectedItems();
    if (selected.isEmpty()) return {};
    return selected.first()->data(Qt::UserRole).toString();
}

namespace {

const QSet<QString> &junkFileSuffixes()
{
    static const QSet<QString> k = [] {
        QSet<QString> s;
        const QStringList parts = QStringLiteral(
                                      "ds_store,ini,cfg,plist,dll,so,dylib,sys,cab,tmp,bak,log,o,obj,class,pyc")
                                      .split(QLatin1Char(','));
        for (const QString &p : parts) s.insert(p.trimmed().toLower());
        return s;
    }();
    return k;
}

bool isJunkFilename(const QString &fileNameLower)
{
    static const QStringList kExact = {QStringLiteral("desktop.ini"), QStringLiteral("thumbs.db"),
                                       QStringLiteral("license"), QStringLiteral("copying")};
    return kExact.contains(fileNameLower);
}

} // namespace

bool MainWindow::isAnalyzableFile(const QFileInfo &fi) const {
    if (!fi.exists()) return false;
    if (fi.isDir()) return false;

    const QString fnLower = fi.fileName().toLower();
    if (isJunkFilename(fnLower)) return false;

    const QString sfx = fi.suffix().toLower();
    if (!sfx.isEmpty() && junkFileSuffixes().contains(sfx)) return false;

    if (fi.isSymLink()) {
        const QString target = fi.symLinkTarget();
        if (!target.isEmpty() && QFileInfo(target).isDir()) return false;
    }
    return true;
}

void MainWindow::scanFiles() {
    if (fileListMode == FileListMode::VirtualTag) {
        populateVirtualTagFiles(activeVirtualTag);
    } else if (fileListMode == FileListMode::SemanticResults) {
        populateSemanticResultFiles();
    } else {
        scanPhysicalFolder();
    }

    if (m_mainTabWidget && m_graphWidget && m_graphTab && m_mainTabWidget->currentWidget() == m_graphTab) {
        m_graphWidget->buildGraph();
    }
}

void MainWindow::scanPhysicalFolder() {
    fileList->clear();
    m_pendingFilesToDisplay.clear();
    m_currentLoadedCount = 0;

    const bool recursive = chkRecursive && chkRecursive->isChecked();
    int count = 0;

    const QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QDirIterator it(currentPath, QDir::Files | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) continue;
        if (!isAnalyzableFile(fileInfo)) continue;
        const QString fileName = fileInfo.fileName();
        m_pendingFilesToDisplay.push_back(filePath);

        const QStringList fastTags = getFastPathTags(fileName);
        if (!fastTags.isEmpty()) {
            QMutexLocker locker(&tagMutex);
            const auto existingByPath = tagManager.getTags(filePath);
            const auto existingByName = tagManager.getTags(fileName);
            QSet<QString> existingSet;
            for (const auto &t : existingByPath) existingSet.insert(t);
            for (const auto &t : existingByName) existingSet.insert(t);
            for (const QString &t : fastTags) {
                if (existingSet.contains(t)) continue;
                tagManager.addTag(filePath, t, false);
                existingSet.insert(t);
            }
        }
        ++count;
    }

    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }
    updateTagList();

    {
        auto &lm = LanguageManager::instance();
        const QString scope = recursive ? lm.getText(QStringLiteral("遞迴")) : lm.getText(QStringLiteral("僅此層"));
        lblStatus->setText(QStringLiteral("%1: %2 | %3: %4 [%5]")
                               .arg(lm.getText(QStringLiteral("資料夾")))
                               .arg(currentPath)
                               .arg(lm.getText(QStringLiteral("檔案數")))
                               .arg(count)
                               .arg(scope));
    }

    fileList->clear();
    renderFileListBatch(BATCH_SIZE);
}

void MainWindow::populateVirtualTagFiles(const QString &tag) {
    fileList->clear();
    m_pendingFilesToDisplay.clear();
    m_currentLoadedCount = 0;
    if (tag.isEmpty()) {
        if (btnLoadMore) btnLoadMore->hide();
        if (btnLoadAll) btnLoadAll->hide();
        return;
    }

    {
        QMutexLocker locker(&tagMutex);
        const std::vector<QString> raw = tagManager.getFilesByTag(tag);
        m_pendingFilesToDisplay.clear();
        m_pendingFilesToDisplay.reserve(raw.size());
        for (const QString &p : raw) {
            const QFileInfo finfo(p);
            if (!isAnalyzableFile(finfo)) continue;
            m_pendingFilesToDisplay.push_back(p);
        }
    }

    auto translateVirtualTagForDisplay = [&](const QString &raw) {
        auto &lm = LanguageManager::instance();
        QString t = raw.trimmed();
        if (t.isEmpty()) return t;

        if (TagManager::hasAiPrefix(t)) {
            t = TagManager::stripAiPrefix(t);
            if (t.isEmpty()) return raw.trimmed();
        }

        // Emoji prefixes can be multiple QChars (surrogates + variation selectors).
        QString prefix;
        int i = 0;
        while (i < t.size()) {
            const QChar c = t.at(i);
            if (c.isSpace()) break;
            if (c.isLetterOrNumber()) break;
            prefix.append(c);
            ++i;
        }
        const QString rest = t.mid(i).trimmed();
        if (!prefix.isEmpty() && !rest.isEmpty()) {
            const QString translatedRest = lm.getText(rest);
            if (translatedRest != rest) return QStringLiteral("%1 %2").arg(prefix, translatedRest).trimmed();
        }
        return lm.getText(t);
    };

    {
        auto &lm = LanguageManager::instance();
        lblStatus->setText(QStringLiteral("%1: %2 | %3: %4")
                               .arg(lm.getText(QStringLiteral("虛擬標籤檢視")))
                               .arg(translateVirtualTagForDisplay(tag))
                               .arg(lm.getText(QStringLiteral("檔案數")))
                               .arg(static_cast<int>(m_pendingFilesToDisplay.size())));
    }

    renderFileListBatch(BATCH_SIZE);
}

void MainWindow::renderFileListBatch(int count) {
    if (!fileList) return;
    if (count <= 0) {
        const bool hasMore = m_currentLoadedCount < static_cast<int>(m_pendingFilesToDisplay.size());
        if (btnLoadMore) btnLoadMore->setVisible(hasMore);
        if (btnLoadAll) btnLoadAll->setVisible(hasMore);
        return;
    }

    const int total = static_cast<int>(m_pendingFilesToDisplay.size());
    const int remaining = total - m_currentLoadedCount;
    const int take = std::min(count, remaining);
    if (take <= 0) {
        if (btnLoadMore) btnLoadMore->hide();
        if (btnLoadAll) btnLoadAll->hide();
        return;
    }

    const int start = m_currentLoadedCount;
    const int end = start + take;
    for (int i = start; i < end; ++i) {
        const QString filePath = m_pendingFilesToDisplay[static_cast<size_t>(i)];
        const QFileInfo fi(filePath);
        if (!fi.exists()) continue;
        if (!isAnalyzableFile(fi)) continue;

        if (fileListMode == FileListMode::SemanticResults) {
            QString relFull;
            if (!rootPath.trimmed().isEmpty()) {
                relFull = QDir(rootPath).relativeFilePath(filePath);
                if (relFull == QLatin1String(".") || relFull.isEmpty())
                    relFull = fi.fileName();
            } else {
                relFull = fi.fileName();
            }

            auto *item = new QListWidgetItem();
            item->setText(fi.fileName());
            item->setData(Qt::UserRole, filePath);
            item->setData(Qt::UserRole + 1, relFull);
            QFileIconProvider ip;
            item->setIcon(ip.icon(fi));
            fileList->addItem(item);
        } else if (fileListMode == FileListMode::VirtualTag) {
            QString parentPath = fi.absolutePath();
            QDir workspaceDir(rootPath);
            QString relativePath = workspaceDir.relativeFilePath(parentPath);
            if (relativePath == QStringLiteral(".")) {
                relativePath = QStringLiteral("根目錄");
            }

            auto *item = new QListWidgetItem();
            item->setText(fi.fileName());                     // 檔名（DisplayRole）
            item->setData(Qt::UserRole, filePath);            // 絕對路徑（雙擊用）
            item->setData(Qt::UserRole + 1, relativePath);    // 相對工作區路徑（Delegate 用）
            fileList->addItem(item);
        } else {
            const QString fileName = fi.fileName();
            auto *item = new QListWidgetItem(fileName, fileList);
            item->setData(Qt::UserRole, filePath);
            if (!rootPath.trimmed().isEmpty()) {
                QString rel = QDir(rootPath).relativeFilePath(filePath);
                if (rel == QLatin1String("."))
                    rel = fileName;
                item->setData(Qt::UserRole + 1, rel);
            }
        }
    }

    m_currentLoadedCount = end;

    const bool hasMore = m_currentLoadedCount < total;
    if (btnLoadMore) btnLoadMore->setVisible(hasMore);
    if (btnLoadAll) btnLoadAll->setVisible(hasMore);

    filterFiles();
    sortFileList();
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::updateTagListCountsOnly() {
    auto updateList = [this](QListWidget *list) {
        if (!list) return;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *it = list->item(i);
            if (!it) continue;
            const QString role = it->data(Qt::UserRole).toString();
            if (role == QStringLiteral("ALL")) continue;
            const QString canon = normalizeDisplayTag(role);
            int n = 0;
            {
                QMutexLocker locker(&tagMutex);
                n = static_cast<int>(tagManager.getFilesByTag(canon).size());
            }
            const QString baseZh = systemTagBaseZh(canon);
            const QString emoji = systemTagEmojiPrefix(canon);
            const QString displayName = baseZh.isEmpty()
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            const QString rowLabel = displayName.trimmed();
            it->setText(QStringLiteral("%1 (%2)").arg(rowLabel).arg(n));
            it->setData(Qt::UserRole, canon);
            it->setData(Qt::UserRole + 1, n);
            it->setData(Qt::UserRole + 2, baseZh);
        }
    };

    auto updateAiTree = [this](QTreeWidget *tree) {
        if (!tree) return;
        std::function<void(QTreeWidgetItem *)> walk;
        walk = [&](QTreeWidgetItem *it) {
            if (!it) return;
            const QString role = it->data(0, Qt::UserRole).toString();
            if (role.isEmpty() || role == QStringLiteral("ALL")) {
                for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
                return;
            }
            const bool isFolder = it->data(0, Qt::UserRole + 3).toInt() == 1;
            if (isFolder && role.startsWith(QStringLiteral("SF_DRAWER:"))) {
                int sum = 0;
                for (int i = 0; i < it->childCount(); ++i) {
                    QTreeWidgetItem *ch = it->child(i);
                    if (ch) sum += ch->data(0, Qt::UserRole + 1).toInt();
                }
                const QString dk = role.mid(QStringLiteral("SF_DRAWER:").size());
                const QString label =
                    (dk == kAiDrawerUncategorizedKey()) ? QStringLiteral("[未分類雜項]") : dk;
                it->setData(0, Qt::UserRole + 1, sum);
                it->setText(0, QStringLiteral("📁 %1 (%2)").arg(label).arg(sum));
                for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
                return;
            }
            const QString canon = normalizeDisplayTag(role);
            int n = 0;
            {
                QMutexLocker locker(&tagMutex);
                n = static_cast<int>(tagManager.getFilesByTag(canon).size());
            }
            const QString baseZh = systemTagBaseZh(canon);
            const QString emoji = systemTagEmojiPrefix(canon);
            const QString displayName = baseZh.isEmpty()
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            const QString nice = tagLibraryLabelStripAiBadge(displayName).trimmed();
            it->setData(0, Qt::UserRole + 1, n);
            it->setData(0, Qt::UserRole + 2, baseZh);
            if (isFolder)
                it->setText(0, QStringLiteral("📁 %1 (%2)").arg(nice).arg(n));
            else
                it->setText(0, QStringLiteral("🏷️ %1 (%2)").arg(nice).arg(n));
            for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    };

    updateList(m_systemTagListWidget);
    updateAiTree(m_aiTagTreeWidget);
    syncTagFilterFromTagList();
}

void MainWindow::updateTagList() {
    tagManager.repairMalformedTagKeys();

    if (m_systemTagListWidget) m_systemTagListWidget->clear();
    if (m_aiTagTreeWidget) m_aiTagTreeWidget->clear();

    if (m_systemTagListWidget) {
        auto *allItem = new QListWidgetItem(LanguageManager::instance().getText(QStringLiteral("All Files")), m_systemTagListWidget);
        allItem->setData(Qt::UserRole, QStringLiteral("ALL"));
    }

    if (m_aiTagTreeWidget) {
        auto *allAi = new QTreeWidgetItem(QStringList{LanguageManager::instance().getText(QStringLiteral("All Files"))});
        allAi->setData(0, Qt::UserRole, QStringLiteral("ALL"));
        m_aiTagTreeWidget->addTopLevelItem(allAi);
    }

    std::vector<QString> rawTags;
    {
        QMutexLocker locker(&tagMutex);
        rawTags = tagManager.getAllTags();
    }

    std::map<QString, QSet<QString>> normToFiles;
    for (const QString &t : rawTags) {
        const QString canon = normalizeDisplayTag(t);
        std::vector<QString> files;
        {
            QMutexLocker locker(&tagMutex);
            files = tagManager.getFilesByTag(t);
        }
        for (const QString &fp : files) normToFiles[canon].insert(fp);
    }

    QMap<QString, QSet<QString>> systemWhitelistToFiles;
    for (const QString &t : rawTags) {
        const QString canon = normalizeDisplayTag(t);
        if (TagManager::hasAiPrefix(canon)) continue;
        const QString sysCanon = mapLooseSystemTagToWhitelistCanon(canon);
        if (sysCanon.isEmpty()) continue;
        std::vector<QString> files;
        {
            QMutexLocker locker(&tagMutex);
            files = tagManager.getFilesByTag(t);
        }
        for (const QString &fp : files) systemWhitelistToFiles[sysCanon].insert(fp);
    }

    for (const QString &sysCanon : orderedSystemTagWhitelistCanons()) {
        const int n = static_cast<int>(systemWhitelistToFiles.value(sysCanon).size());
        const QString baseZh = systemTagBaseZh(sysCanon);
        const QString emoji = systemTagEmojiPrefix(sysCanon);
        const QString displayName = baseZh.isEmpty()
                                        ? sysCanon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
        QListWidget *target = m_systemTagListWidget;
        if (!target) continue;
        const QString rowLabel = displayName.trimmed();
        auto *it = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(rowLabel).arg(n), target);
        it->setData(Qt::UserRole, sysCanon);
        it->setData(Qt::UserRole + 1, n);
        it->setData(Qt::UserRole + 2, baseZh);
    }

    if (m_aiTagTreeWidget) {
        const int kKindRole = Qt::UserRole + 3;
        const int kKindFolder = 1;
        const int kKindLeaf = 0;

        auto countFor = [&](const QString &c) -> int {
            if (normToFiles.count(c)) return static_cast<int>(normToFiles.at(c).size());
            QMutexLocker locker(&tagMutex);
            return static_cast<int>(tagManager.getFilesByTag(c).size());
        };

        QSet<QString> aiLeaves;
        for (const QString &t : rawTags) {
            const QString tt = t.trimmed();
            if (!TagManager::hasAiPrefix(tt)) continue;
            if (sfIsSyntheticAiDrawerFolderTag(tt)) continue;
            aiLeaves.insert(tt);
        }

        QHash<QString, QVector<QString>> drawerToLeaves;
        for (const QString &dk : sfFixedAiClusterDrawerKeys())
            drawerToLeaves.insert(dk, {});
        drawerToLeaves.insert(kAiDrawerUncategorizedKey(), {});

        for (const QString &leaf : std::as_const(aiLeaves)) {
            QString dk = m_aiTagToDrawerKey.value(leaf);
            if (dk.isEmpty() || !drawerToLeaves.contains(dk))
                dk = kAiDrawerUncategorizedKey();
            drawerToLeaves[dk].append(leaf);
        }

        auto sortLeaves = [&](QVector<QString> &vec) {
            std::sort(vec.begin(), vec.end(), [&](const QString &a, const QString &b) {
                const int na = countFor(a);
                const int nb = countFor(b);
                if (na != nb) return na > nb;
                return a.localeAwareCompare(b) < 0;
            });
        };

        for (auto it = drawerToLeaves.begin(); it != drawerToLeaves.end(); ++it)
            sortLeaves(it.value());

        auto makeDrawerRoot = [&](const QString &drawerKey, const QString &labelText) {
            QTreeWidgetItem *root = new QTreeWidgetItem();
            QFont f = root->font(0);
            f.setBold(true);
            root->setFont(0, f);
            const int totalKids = drawerToLeaves.value(drawerKey).size();
            int sumFiles = 0;
            for (const QString &lf : drawerToLeaves.value(drawerKey))
                sumFiles += countFor(lf);
            root->setText(0, QStringLiteral("📁 %1 (%2)").arg(labelText).arg(sumFiles));
            root->setData(0, Qt::UserRole, QStringLiteral("SF_DRAWER:%1").arg(drawerKey));
            root->setData(0, kKindRole, kKindFolder);
            root->setData(0, Qt::UserRole + 1, sumFiles);
            root->setData(0, Qt::UserRole + 2, QString());

            for (const QString &leaf : drawerToLeaves.value(drawerKey)) {
                const int n = countFor(leaf);
                const QString baseZh = systemTagBaseZh(leaf);
                const QString emoji = systemTagEmojiPrefix(leaf);
                const QString displayName = baseZh.isEmpty()
                                                ? leaf
                                                : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
                const QString nice = tagLibraryLabelStripAiBadge(displayName).trimmed();
                QTreeWidgetItem *ch = new QTreeWidgetItem(root);
                ch->setText(0, QStringLiteral("🏷️ %1 (%2)").arg(nice).arg(n));
                ch->setData(0, Qt::UserRole, leaf);
                ch->setData(0, kKindRole, kKindLeaf);
                ch->setData(0, Qt::UserRole + 1, n);
                ch->setData(0, Qt::UserRole + 2, baseZh);
            }
            m_aiTagTreeWidget->addTopLevelItem(root);
        };

        for (const QString &dk : sfFixedAiClusterDrawerKeys())
            makeDrawerRoot(dk, dk);

        makeDrawerRoot(kAiDrawerUncategorizedKey(), QStringLiteral("[未分類雜項]"));
    }

    syncTagFilterFromTagList();
    rebuildAddExistingTagMenu();
}

void MainWindow::syncTagFilterFromTagList() {
    const QString prevData = cmbTagFilter->currentData().toString();
    cmbTagFilter->blockSignals(true);
    cmbTagFilter->clear();
    cmbTagFilter->addItem(LanguageManager::instance().getText(QStringLiteral("All Files")), QStringLiteral("ALL"));
    auto addFromList = [this](QListWidget *list) {
        if (!list) return;
        for (int i = 0; i < list->count(); ++i) {
            const auto *it = list->item(i);
            if (!it) continue;
            if (it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) continue;
            const QString rawTag = it->data(Qt::UserRole).toString();
            if (rawTag.isEmpty()) continue;
            const QString baseZh = it->data(Qt::UserRole + 2).toString();
            const QString canon = normalizeDisplayTag(rawTag);
            const QString emoji = systemTagEmojiPrefix(canon);
            QString displayName = baseZh.isEmpty()
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            displayName = displayName.trimmed();
            cmbTagFilter->addItem(displayName, rawTag);
        }
    };
    auto addFromAiTree = [this](QTreeWidget *tree) {
        if (!tree) return;
        std::function<void(QTreeWidgetItem *)> walk;
        walk = [&](QTreeWidgetItem *node) {
            if (!node) return;
            const QString rawTag = node->data(0, Qt::UserRole).toString();
            if (rawTag == QStringLiteral("ALL") || rawTag.isEmpty()) {
                for (int i = 0; i < node->childCount(); ++i) walk(node->child(i));
                return;
            }
            if (node->data(0, Qt::UserRole + 3).toInt() == 1) {
                for (int i = 0; i < node->childCount(); ++i) walk(node->child(i));
                return;
            }
            const QString baseZh = node->data(0, Qt::UserRole + 2).toString();
            const QString canon = normalizeDisplayTag(rawTag);
            const QString emoji = systemTagEmojiPrefix(canon);
            QString displayName = baseZh.isEmpty()
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            displayName = tagLibraryLabelStripAiBadge(displayName.trimmed());
            cmbTagFilter->addItem(displayName, rawTag);
            for (int i = 0; i < node->childCount(); ++i) walk(node->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    };
    addFromList(m_systemTagListWidget);
    addFromAiTree(m_aiTagTreeWidget);
    const int idx = cmbTagFilter->findData(prevData);
    cmbTagFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    cmbTagFilter->blockSignals(false);
}

void MainWindow::syncTagListFromTagFilter() {
    const QString selected = cmbTagFilter->currentData().toString();
    if (selected == QStringLiteral("ALL") || selected.isEmpty()) {
        if (m_systemTagListWidget) {
            for (int i = 0; i < m_systemTagListWidget->count(); ++i) {
                auto *it = m_systemTagListWidget->item(i);
                if (it && it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) {
                    m_systemTagListWidget->setCurrentItem(it);
                    break;
                }
            }
        }
        return;
    }
    auto selectIn = [&](QListWidget *list) {
        if (!list) return false;
        for (int i = 0; i < list->count(); ++i) {
            auto *it = list->item(i);
            if (it && it->data(Qt::UserRole).toString() == selected) {
                list->setCurrentItem(it);
                return true;
            }
        }
        return false;
    };
    auto selectInTree = [&](QTreeWidget *tree) -> bool {
        if (!tree) return false;
        std::function<bool(QTreeWidgetItem *)> walk;
        walk = [&](QTreeWidgetItem *node) -> bool {
            if (!node) return false;
            if (node->data(0, Qt::UserRole).toString() == selected) {
                tree->setCurrentItem(node);
                tree->scrollToItem(node);
                return true;
            }
            for (int i = 0; i < node->childCount(); ++i) {
                if (walk(node->child(i))) return true;
            }
            return false;
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (walk(tree->topLevelItem(i))) return true;
        }
        return false;
    };
    if (selectIn(m_systemTagListWidget)) return;
    (void)selectInTree(m_aiTagTreeWidget);
}

void MainWindow::filterFiles() {
    const QString query =
        m_heroOmnibox ? m_heroOmnibox->text().trimmed().toLower() : QString();
    const QString tagFilter = cmbTagFilter->currentData().toString();

    const bool useSemanticSubset =
        m_semanticFilterActive && !m_semanticVisiblePaths.isEmpty()
        && fileListMode != FileListMode::SemanticResults;

    std::vector<QString> filesWithTag;
    if (!tagFilter.isEmpty() && tagFilter != QStringLiteral("ALL")) {
        QMutexLocker locker(&tagMutex);
        filesWithTag = tagManager.getFilesByTag(tagFilter);
    }

    for (int i = 0; i < fileList->count(); ++i) {
        auto *it = fileList->item(i);
        if (!it) continue;

        const QString absPath = it->data(Qt::UserRole).toString();
        const QString absNorm = QDir::cleanPath(absPath);
        const QString nameLower = baseName(absPath).toLower();

        bool match = true;
        if (useSemanticSubset) {
            match = m_semanticVisiblePaths.contains(absNorm);
        }

        if (match && !query.isEmpty()) {
            bool qm = nameLower.contains(query) || parentDirDisplay(absPath).toLower().contains(query);
            if (!qm) {
                std::vector<QString> tags;
                {
                    QMutexLocker locker(&tagMutex);
                    tags = tagManager.getTags(absPath);
                }
                for (const auto &t : tags) {
                    if (t.toLower().contains(query)) {
                        qm = true;
                        break;
                    }
                }
            }
            match = qm;
        }

        if (match && !tagFilter.isEmpty() && tagFilter != QStringLiteral("ALL")) {
            bool tagOk = false;
            for (const auto &fp : filesWithTag) {
                if (fp == absPath || baseName(fp) == baseName(absPath)) {
                    tagOk = true;
                    break;
                }
            }
            match = tagOk;
        }

        it->setHidden(!match);
    }
}

void MainWindow::onHeroOmniboxReturnPressed()
{
    QObject *snd = sender();
    if (snd != m_btnSemanticSearch && m_btnSemanticSearch)
        m_btnSemanticSearch->animateClick();
    runHeroSemanticSearchQuery();
}

void MainWindow::onHeroOmniboxTextChanged(const QString &text)
{
    if (m_semanticFilterActive) {
        const QString a = text.trimmed();
        const QString b = m_semanticLockedQuery.trimmed();
        if (!m_semanticLockedQuery.isEmpty() && a != b)
            clearSemanticSearchFilter();
    }
    filterFiles();
}

void MainWindow::disableSemanticOverlays()
{
    m_semanticFilterActive = false;
    m_semanticVisiblePaths.clear();
    m_semanticLockedQuery.clear();
    m_semanticValidWorkspacePaths.clear();
    m_semanticSearchIdToPath.clear();
    refreshSemanticGlobalBanner();
}

void MainWindow::clearSemanticSearchFilter()
{
    const bool wasSemanticResults = (fileListMode == FileListMode::SemanticResults);
    disableSemanticOverlays();
    setHeroSemanticBusy(false);
    if (wasSemanticResults) {
        fileListMode = FileListMode::PhysicalFolder;
        scanFiles();
    } else {
        filterFiles();
    }
}

QString MainWindow::aiUiDrawerStorePath() const
{
    if (rootPath.trimmed().isEmpty()) return {};
    return QDir(rootPath).filePath(QStringLiteral(".smartfile/ai_ui_drawers.json"));
}

void MainWindow::loadAiUiDrawerAssignments()
{
    m_aiTagToDrawerKey.clear();
    const QString p = aiUiDrawerStorePath();
    if (p.isEmpty() || !QFile::exists(p)) return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    if (!d.isObject()) return;
    const QJsonObject o = d.object().value(QStringLiteral("tagToDrawer")).toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString k = it.key();
        const QString v = it.value().toString();
        if (!k.isEmpty() && !v.isEmpty()) m_aiTagToDrawerKey.insert(k, v);
    }
}

void MainWindow::saveAiUiDrawerAssignments() const
{
    const QString p = aiUiDrawerStorePath();
    if (p.isEmpty()) return;
    QDir().mkpath(QFileInfo(p).absolutePath());
    QJsonObject inner;
    for (auto it = m_aiTagToDrawerKey.constBegin(); it != m_aiTagToDrawerKey.constEnd(); ++it)
        inner.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("tagToDrawer"), inner);
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::refreshSemanticGlobalBanner()
{
    if (!m_semanticGlobalBanner) return;
    if (!m_semanticFilterActive || m_semanticVisiblePaths.isEmpty()) {
        m_semanticGlobalBanner->hide();
        return;
    }
    const int n = m_semanticVisiblePaths.size();
    m_semanticGlobalBanner->setText(
        QStringLiteral("🔍 跨資料夾全域搜尋結果（共找到 %1 筆相關檔案）").arg(n));
    m_semanticGlobalBanner->setVisible(true);
}

void MainWindow::populateSemanticResultFiles()
{
    if (!fileList) return;
    fileList->clear();
    m_pendingFilesToDisplay.clear();
    m_currentLoadedCount = 0;
    if (m_semanticVisiblePaths.isEmpty()) {
        if (btnLoadMore) btnLoadMore->hide();
        if (btnLoadAll) btnLoadAll->hide();
        refreshSemanticGlobalBanner();
        refreshCurrentAnalysisTargetUi();
        return;
    }
    QVector<QString> v;
    v.reserve(m_semanticVisiblePaths.size());
    for (const QString &p : std::as_const(m_semanticVisiblePaths))
        v.push_back(p);
    std::sort(v.begin(), v.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    for (const QString &fp : std::as_const(v)) {
        const QFileInfo fi(fp);
        if (!fi.exists()) continue;
        if (!isAnalyzableFile(fi)) continue;
        m_pendingFilesToDisplay.push_back(fp);
    }
    if (btnLoadMore) btnLoadMore->hide();
    if (btnLoadAll) btnLoadAll->hide();
    const int n = static_cast<int>(m_pendingFilesToDisplay.size());
    if (n > 0)
        renderFileListBatch(n);
    refreshSemanticGlobalBanner();
    refreshCurrentAnalysisTargetUi();
}

void MainWindow::reloadCurrentFileListPanel()
{
    if (fileListMode == FileListMode::PhysicalFolder)
        scanFiles();
    else if (fileListMode == FileListMode::SemanticResults)
        populateSemanticResultFiles();
    else
        populateVirtualTagFiles(activeVirtualTag);
}

QString MainWindow::buildWorkspaceSemanticIdLines(int maxFiles)
{
    m_semanticSearchIdToPath.clear();
    m_semanticValidWorkspacePaths.clear();
    QStringList lines;
    if (rootPath.trimmed().isEmpty()) return {};
    const QString root = QDir::cleanPath(rootPath);
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int id = 0;
    while (it.hasNext()) {
        if (id >= maxFiles) break;
        const QString absPath = QDir::cleanPath(it.next());
        const QFileInfo fi(absPath);
        if (!isAnalyzableFile(fi)) continue;
        ++id;
        m_semanticSearchIdToPath.insert(id, absPath);
        m_semanticValidWorkspacePaths.insert(absPath);
        const QString fn = baseName(absPath);
        QString relFromWs = QDir(root).relativeFilePath(absPath);
        if (relFromWs == QLatin1String(".") || relFromWs.isEmpty())
            relFromWs = fn;

        if (m_aiSummaryByPath.contains(absPath)) {
            QString sum = m_aiSummaryByPath.value(absPath).trimmed();
            if (sum.size() > 420)
                sum = sum.left(420) + QStringLiteral("…");
            QStringList tagParts;
            {
                QMutexLocker locker(&tagMutex);
                for (const QString &t : tagManager.getTags(absPath)) {
                    const QString tt = t.trimmed();
                    if (!tt.isEmpty())
                        tagParts.append(tt);
                }
            }
            QString tagsJoined = tagParts.join(QLatin1String(", "));
            if (tagsJoined.size() > 220)
                tagsJoined = tagsJoined.left(220) + QStringLiteral("…");
            if (tagsJoined.isEmpty())
                tagsJoined = QStringLiteral("(無標籤)");
            if (sum.isEmpty())
                sum = QStringLiteral("(無摘要文字)");
            lines.append(QStringLiteral("[ID: %1] 路徑: %2 | 檔名: %3 | 摘要: %4 | 標籤: %5")
                             .arg(id)
                             .arg(relFromWs, fn, sum, tagsJoined));
        } else {
            lines.append(QStringLiteral("[ID: %1] 路徑: %2 | 檔名: %3 | (尚未分析) 僅能依檔名與路徑推論")
                             .arg(id)
                             .arg(relFromWs, fn));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::setHeroSemanticBusy(bool busy)
{
    if (m_heroSearchBusyChip) {
        m_heroSearchBusyChip->setVisible(busy);
        if (busy) {
            m_heroSemanticSpinPhase = 0;
            m_heroSearchBusyChip->setPhase(0);
        }
    }
    if (m_btnSemanticSearch)
        m_btnSemanticSearch->setEnabled(!busy);
    if (m_heroOmnibox) {
        m_heroOmnibox->setEnabled(!busy);
        m_heroOmnibox->setReadOnly(busy);
    }
    if (m_fileListPageStack)
        m_fileListPageStack->setCurrentIndex(busy ? 1 : 0);
    if (m_heroSemanticSpinTimer) {
        if (busy)
            m_heroSemanticSpinTimer->start();
        else
            m_heroSemanticSpinTimer->stop();
    }
}

QString MainWindow::buildSemanticRetrieverPrompt(const QString &userQuery, const QString &idContextLines) const
{
    return QStringLiteral(
               "System:\n"
               "You are a precise file retrieval assistant. Your reply MUST be ONLY a JSON array of integers — no object wrapper, "
               "no markdown fences, no explanations, no file paths or names.\n"
               "Correct format example: [1, 5, 12]\n"
               "Pick 5 to 10 file IDs from the list below that best match the user query (fewer if fewer are relevant; at least 1 if any match). "
               "Each integer MUST be copied exactly from an [ID: N] line in the file list. Never invent IDs.\n\n"
               "User query:\n%1\n\n"
               "Files (numeric IDs only; use these IDs in your answer):\n%2\n")
        .arg(userQuery, idContextLines);
}

void MainWindow::runHeroSemanticSearchQuery()
{
    if (!m_heroOmnibox) return;
    const QString t = m_heroOmnibox->text();
    bool hasWs = false;
    for (QChar c : t) {
        if (c.isSpace()) {
            hasWs = true;
            break;
        }
    }
    const bool wantSemantic = t.trimmed().length() > 10 || hasWs;
    if (!wantSemantic) {
        clearSemanticSearchFilter();
        return;
    }

    if (!llamaEngine.isModelLoaded()) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("模型自動載入失敗 (Auto-load failed)")));
        return;
    }
    if (watcher && watcher->isRunning()) {
        QMessageBox::information(this, QStringLiteral("Smartflie"),
                                 LanguageManager::instance().getText(QStringLiteral("分析進行中，請稍後再試語意搜尋。")));
        return;
    }
    if (m_consolidateWatcher && m_consolidateWatcher->isRunning()) {
        QMessageBox::information(this, QStringLiteral("Smartflie"),
                                 QStringLiteral("AI 標籤分類進行中，請稍後再試語意搜尋。"));
        return;
    }
    if (m_semanticSearchWatcher && m_semanticSearchWatcher->isRunning())
        return;

    m_semanticSearchIdToPath.clear();
    m_semanticValidWorkspacePaths.clear();

    const int maxFiles = 180;
    const QString idLines = buildWorkspaceSemanticIdLines(maxFiles);
    if (idLines.isEmpty()) {
        if (lblStatus)
            lblStatus->setText(QStringLiteral("⚠️ 目前清單無檔案可供語意搜尋"));
        return;
    }

    const QString userQuery = t.trimmed();
    const QString fullPrompt = buildSemanticRetrieverPrompt(userQuery, idLines);

    setHeroSemanticBusy(true);
    if (lblStatus)
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("語意搜尋進行中…")));

    if (!m_semanticSearchWatcher)
        return;

    m_semanticSearchWatcher->setFuture(QtConcurrent::run([this, fullPrompt]() {
        return llamaEngine.generateResponse(fullPrompt.toStdString());
    }));
}

void MainWindow::onSemanticSearchFinished()
{
    auto endBusy = [this]() { setHeroSemanticBusy(false); };

    if (!m_semanticSearchWatcher) {
        endBusy();
        return;
    }

    QString raw;
    try {
        raw = QString::fromStdString(m_semanticSearchWatcher->result());
    } catch (const std::exception &e) {
        endBusy();
        QMessageBox::warning(this, QStringLiteral("Smartflie"),
                             QStringLiteral("語意搜尋結果讀取失敗：%1").arg(QString::fromUtf8(e.what())));
        return;
    } catch (...) {
        endBusy();
        QMessageBox::warning(this, QStringLiteral("Smartflie"),
                             QStringLiteral("語意搜尋結果讀取時發生未知錯誤。"));
        return;
    }

    if (raw.size() > 400000)
        raw.truncate(400000);

    if (raw.trimmed().startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)) {
        endBusy();
        QMessageBox::critical(this, QStringLiteral("Smartflie"), raw);
        return;
    }

    QVector<int> ids;
    try {
        ids = parseSemanticRetrieverIdList(raw);
    } catch (...) {
        ids.clear();
    }

    QSet<QString> picked;
    for (int id : ids) {
        const QString p = m_semanticSearchIdToPath.value(id);
        if (!p.isEmpty()) picked.insert(QDir::cleanPath(p));
    }

    if (picked.isEmpty()) {
        endBusy();
        if (lblStatus)
            lblStatus->setText(QStringLiteral("⚠️ 語意搜尋未解析出有效檔案 ID，請換個描述再試"));
        return;
    }

    m_semanticVisiblePaths = picked;
    m_semanticFilterActive = true;
    m_semanticLockedQuery = m_heroOmnibox ? m_heroOmnibox->text().trimmed() : QString();
    fileListMode = FileListMode::SemanticResults;
    populateSemanticResultFiles();
    endBusy();

    if (lblStatus) {
        lblStatus->setText(QStringLiteral("✅ 語意搜尋完成，已為您找到最相關的資料。"));
    }
}

void MainWindow::syncAiTagHierarchyFromTree()
{
    if (!m_aiTagTreeWidget) return;

    QHash<QString, QString> rebuilt;

    std::function<void(QTreeWidgetItem *)> walk;
    walk = [&](QTreeWidgetItem *it) {
        if (!it) return;
        const QString role = it->data(0, Qt::UserRole).toString();
        if (role == QStringLiteral("ALL")) {
            for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
            return;
        }
        if (role.startsWith(QStringLiteral("SF_DRAWER:"))) {
            const QString dkey = role.mid(QStringLiteral("SF_DRAWER:").size());
            for (int i = 0; i < it->childCount(); ++i) {
                QTreeWidgetItem *ch = it->child(i);
                const QString ttag = ch->data(0, Qt::UserRole).toString();
                if (!ttag.isEmpty() && TagManager::hasAiPrefix(ttag)) rebuilt.insert(ttag, dkey);
                walk(ch);
            }
            return;
        }
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };

    for (int ti = 0; ti < m_aiTagTreeWidget->topLevelItemCount(); ++ti)
        walk(m_aiTagTreeWidget->topLevelItem(ti));

    m_aiTagToDrawerKey = std::move(rebuilt);
    saveAiUiDrawerAssignments();
    updateTagList();
}

void MainWindow::onFileSelected(QListWidgetItem *item) {
    if (!item) return;
    const QString absPath = item->data(Qt::UserRole).toString();
    if (absPath.isEmpty()) return;

    QFileInfo fi(absPath);
    btnAnalyzeFile->setEnabled(isAnalyzableFile(fi));

    enqueuePriorityAnalyzeForFileIfNeeded(absPath);

    updatePreviewForFile(absPath);
    updateTagDisplayForFile(absPath);
    btnSaveTags->setEnabled(false);
}

void MainWindow::onTagSelected(QListWidgetItem *item) {
    if (!item) return;
    applyTagSelectionData(item->data(Qt::UserRole).toString());
}

void MainWindow::onAiTagTreeItemClicked(QTreeWidgetItem *item, int) {
    if (!item) return;
    const int kKindRole = Qt::UserRole + 3;
    if (item->data(0, kKindRole).toInt() == 1) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    applyTagSelectionData(item->data(0, Qt::UserRole).toString());
}

void MainWindow::applyTagSelectionData(const QString &data) {
    disableSemanticOverlays();
    if (data == QStringLiteral("ALL")) {
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        cmbTagFilter->setCurrentIndex(0);
        scanFiles();
        sortFileList();
        return;
    }

    const QString tag = normalizeDisplayTag(data);
    fileListMode = FileListMode::VirtualTag;
    activeVirtualTag = tag;
    const int idx = cmbTagFilter->findData(tag);
    if (idx >= 0) cmbTagFilter->setCurrentIndex(idx);
    populateVirtualTagFiles(tag);
}

void MainWindow::updatePreviewForFile(const QString &absPath) {
    QFileInfo fi(absPath);
    QMimeDatabase db;
    const QMimeType mt = db.mimeTypeForFile(fi);
    const QString typeLine = QStringLiteral("[ %1 %2 ]").arg(emojiForMime(mt), mimeDisplay(mt));

    lblPreviewImage->setVisible(false);
    txtPreviewText->setVisible(false);
    if (m_aiSummaryEdit) {
        const QString s = m_aiSummaryByPath.value(absPath).trimmed();
        if (s.isEmpty()) {
            m_aiSummaryEdit->clear(); // show placeholder "尚未分析"
        } else {
            m_aiSummaryEdit->setPlainText(s);
        }
    }

    if (!fi.exists()) {
        txtPreviewText->setVisible(true);
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(檔案不存在)"));
        return;
    }

    if (mt.name().startsWith(QStringLiteral("image/"))) {
        QPixmap pix(absPath);
        if (!pix.isNull()) {
            lblPreviewImage->setVisible(true);
            lblPreviewImage->setPixmap(pix.scaled(lblPreviewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            txtPreviewText->setVisible(true);
            txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(無法載入圖片)"));
        }
        return;
    }

    const QString suffix = fi.suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        txtPreviewText->setVisible(true);
        QString content = DocumentParser::extractPdfText(absPath);
        if (content.size() > 2500) content = content.left(2500) + QStringLiteral("...");
        if (content.trimmed().isEmpty()) {
            content = QStringLiteral("[%1]")
                          .arg(LanguageManager::instance().getText(QStringLiteral("無法提取文字內容（可能為掃描檔或加密）")));
        }
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n") + content);
        return;
    }
    if (officeZipPreviewSuffixes().contains(suffix)) {
        txtPreviewText->setVisible(true);
        QString content = DocumentParser::extractTextQString(absPath);
        if (content.size() > 2500) content = content.left(2500) + QStringLiteral("...");
        if (content.trimmed().isEmpty()) {
            content = QStringLiteral("[%1]")
                          .arg(LanguageManager::instance().getText(QStringLiteral("無法提取文字內容（可能為掃描檔或加密）")));
        }
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n") + content);
        return;
    }

    if (mt.name().startsWith(QStringLiteral("text/")) || suffix == QStringLiteral("md") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("log") || suffix == QStringLiteral("json") || suffix == QStringLiteral("xml") || suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") || suffix == QStringLiteral("cpp") || suffix == QStringLiteral("h") || suffix == QStringLiteral("py") || suffix == QStringLiteral("js") || suffix == QStringLiteral("ts")) {
        txtPreviewText->setVisible(true);
        std::ifstream f(absPath.toStdString(), std::ios::binary);
        if (!f.is_open()) {
            txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(無法讀取)"));
            return;
        }
        std::string buf;
        buf.resize(4096);
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        buf.resize(static_cast<size_t>(f.gcount()));
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n") + QString::fromUtf8(buf.data(), static_cast<int>(buf.size())));
        return;
    }

    txtPreviewText->setVisible(true);
    txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(%1)")
                                     .arg(LanguageManager::instance().getText(QStringLiteral("二進位檔：不顯示內容"))));
}

void MainWindow::updateTagDisplayForFile(const QString &absPath) {
    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getTags(absPath);
    }

    auto isAiTag = [](const QString &t) { return TagManager::hasAiPrefix(t); };
    auto normBase = [&](const QString &t) {
        return TagManager::stripAiPrefix(t).trimmed().toLower();
    };

    QSet<QString> manualBases;
    QStringList manualTags;
    QStringList aiTags;
    for (const auto &t : tags) {
        const QString base = normBase(t);
        if (base.isEmpty()) continue;
        if (isAiTag(t)) {
            aiTags << base;
        } else {
            manualBases.insert(base);
            manualTags << base;
        }
    }

    // Dedup: manual wins over AI.
    QStringList aiShown;
    for (const QString &b : aiTags) {
        if (manualBases.contains(b)) continue;
        aiShown << b;
    }

    manualTags.removeDuplicates();
    aiShown.removeDuplicates();
    manualTags.sort(Qt::CaseInsensitive);
    aiShown.sort(Qt::CaseInsensitive);

    auto &lm = LanguageManager::instance();
    auto displayTag = [&](const QString &rawTag) {
        // Presentation-only translation: keep emoji/prefix (can be multi-QChar), translate base part if known.
        QString t = rawTag.trimmed();
        if (t.isEmpty()) return t;

        QString prefix;
        int i = 0;
        while (i < t.size()) {
            const QChar c = t.at(i);
            if (c.isSpace()) break;
            if (c.isLetterOrNumber()) break;
            prefix.append(c);
            ++i;
        }
        const QString rest = t.mid(i).trimmed();
        if (!prefix.isEmpty() && !rest.isEmpty()) {
            const QString translatedRest = lm.getText(rest);
            if (translatedRest != rest) return QStringLiteral("%1 %2").arg(prefix, translatedRest).trimmed();
        }

        return lm.getText(t);
    };

    QString html;
    html += QStringLiteral("<div><b>%1</b>: ").arg(displayTag(QStringLiteral("個人標籤")).toHtmlEscaped());
    if (manualTags.isEmpty()) {
        html += QStringLiteral("<span style='color:#888'>(%1)</span>").arg(displayTag(QStringLiteral("無")).toHtmlEscaped());
    } else {
        for (const QString &t : manualTags) {
            html += QStringLiteral("<span style='background:#444; color:#fff; padding:2px 6px; border-radius:8px; margin-right:6px;'>%1</span>")
                        .arg(displayTag(t).toHtmlEscaped());
        }
    }
    html += QStringLiteral("</div>");

    html += QStringLiteral("<div style='margin-top:6px;'><b>%1</b>: ")
                .arg(displayTag(QStringLiteral("AI 智能建議")).toHtmlEscaped());
    if (aiShown.isEmpty()) {
        html += QStringLiteral("<span style='color:#888'>(%1)</span>").arg(displayTag(QStringLiteral("無")).toHtmlEscaped());
    } else {
        for (const QString &t : aiShown) {
            html += QStringLiteral("<span style='background:#2b6cb0; color:#fff; padding:2px 6px; border-radius:8px; margin-right:6px;'>🤖 %1</span>")
                        .arg(displayTag(t).toHtmlEscaped());
        }
    }
    html += QStringLiteral("</div>");

    lblTags->setText(html);
}

QString MainWindow::historicalTagsString() const {
    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getAllTags();
    }
    QStringList parts;
    for (const auto &t : tags) parts << t;
    return parts.join(QStringLiteral(", "));
}

std::vector<QString> MainWindow::sanitizeAiTags(const QString &raw) const {
    QString cleaned = raw;
    cleaned.replace(QRegularExpression(QStringLiteral("System:|Assistant:|User:|輸出:|標籤:|標签:"), QRegularExpression::CaseInsensitiveOption), QString());
    cleaned.replace(QStringLiteral("\n"), QStringLiteral(" ")).replace(QStringLiteral("\r"), QStringLiteral(" "));

    QStringList parts = cleaned.split(QRegularExpression(QStringLiteral("[,，、]")), Qt::SkipEmptyParts);
    QSet<QString> seen;
    std::vector<QString> out;
    const bool en = (LanguageManager::instance().language() == LanguageManager::Language::EN_US);
    const int maxLen = en ? 24 : 15;

    for (const QString &p0 : parts) {
        QString p = TagManager::stripAiPrefix(p0.trimmed());
        // Do NOT strip '[' / ']' here — that mangles "[AI] …" into "AI] …" / "[ …" garbage. Only trim punctuation/symbols.
        p.replace(QRegularExpression(QStringLiteral("[\\s\\.。;；:：\\(\\)<>\"'`~!@#$%^&*+=\\|\\\\/?]+")), QString());
        p = p.trimmed();
        if (p.isEmpty()) continue;
        if (p.size() > maxLen) continue;
        if (seen.contains(p)) continue;
        seen.insert(p);
        out.push_back(p);
        if (out.size() >= 5) break;
    }
    return out;
}

void MainWindow::setUiBusy(bool busy) {
    // In batch mode we keep UI interactive (non-blocking UX).
    if (m_isBatchMode) {
        if (btnAnalyzeFile) btnAnalyzeFile->setEnabled(false); // avoid concurrent single-file analyze
        if (btnCancelAnalysis) btnCancelAnalysis->setEnabled(busy);
        if (btnBatchAnalyze) btnBatchAnalyze->setEnabled(false);
        syncBatchAnalyzeButtonLabel();
        return;
    }

    btnAnalyzeFile->setEnabled(!busy && !currentFilePath().isEmpty());
    btnCancelAnalysis->setEnabled(busy);
    btnSaveTags->setEnabled(!busy && btnSaveTags->isEnabled());
    if (!busy)
        syncNavigationButtons();
    syncBatchAnalyzeButtonLabel();
    refreshCurrentAnalysisTargetUi();
}

void MainWindow::analyzeFile() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }
    analyzeFileForPath(fp);
}

void MainWindow::analyzeFileForPath(const QString &absPath, bool forceColdArchiveBypass) {
    const QString fp = QDir::cleanPath(absPath);
    if (fp.isEmpty()) return;

    QFileInfo fi(fp);
    if (!isAnalyzableFile(fi)) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("此項目不可分析")));
        return;
    }

    QString bypassSummary;
    QStringList bypassTags;
    if (trySystemBypassPreset(fi, &bypassSummary, &bypassTags)) {
        m_currentAnalyzingFile = fp;
        applyPresetBypassAnalysis(fp, bypassSummary, bypassTags);
        return;
    }

    bool fromBypassSet = false;
    if (m_coldArchiveBypassPaths.contains(fp)) {
        m_coldArchiveBypassPaths.remove(fp);
        fromBypassSet = true;
    }
    const bool forceLlmCold = forceColdArchiveBypass || fromBypassSet || !m_isBatchMode
                              || (m_isBatchMode && !m_batchTriggeredByBackgroundAuto);

    QString coldSummary;
    QStringList coldTags;
    if (tryColdArchiveBypass(fi, forceLlmCold, &coldSummary, &coldTags)) {
        m_currentAnalyzingFile = fp;
        applyColdArchiveAnalysis(fp, coldSummary, coldTags);
        return;
    }

    const QString contentHash = sha256HexOfFile(fp);
    primeAnalysisCacheFromDisk(contentHash);
    if (!contentHash.isEmpty()) {
        noteSameNameDifferentHashConflicts(fp, contentHash);
    }
    if (!contentHash.isEmpty() && m_analysisByContentHash.contains(contentHash)) {
        const QJsonObject cached = m_analysisByContentHash.value(contentHash);
        m_currentAnalyzingFile = fp;
        applyCachedAnalysisForHashHit(fp, cached, contentHash);
        if (m_isBatchMode) {
            ++m_batchCompletedCount;
            setUiBusy(false);
            if (batchProgressBar && m_totalBatchSize > 0) {
                batchProgressBar->setValue(qMin(m_totalBatchSize, m_batchCompletedCount));
            }
            syncBatchProgressBars();
            lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成（重複內容：已套用快取）")));
            updateBackgroundStatusLabel();
            QTimer::singleShot(0, this, &MainWindow::processNextInQueue);
        } else {
            lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
            m_currentAnalyzingFile.clear();
            refreshCurrentAnalysisTargetUi();
        }
        return;
    }

    if (!llamaEngine.isModelLoaded()) {
        // If we're auto-loading in background, wait (blocking) to avoid "Model not loaded" race.
        if (modelLoadWatcher && modelLoadWatcher->isRunning()) {
            lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("等待模型載入完成…")));
            modelLoadWatcher->future().waitForFinished();
        }
        if (!llamaEngine.isModelLoaded()) {
            QMessageBox::warning(this, QStringLiteral("Model"), QStringLiteral("模型尚未載入"));
            return;
        }
    }

    cancelFlag.store(false);
    setUiBusy(true);
    lblStatus->setText(tr("Preparing…"));

    m_currentAnalyzingFile = fp;
    m_analysisUiWorkActive = true;
    updateBackgroundStatusLabel();
    refreshCurrentAnalysisTargetUi();
    startAnalysisSpinnerForPath(fp);
    const QString filename = fi.fileName();
    // IMPORTANT: Do NOT feed full historical tags into the prompt.
    // It causes "prompt contamination" where prior institution names get repeated.
    const QString existingTags; // keep empty on purpose
    const QString rejectedTagsCsv = [this]() {
        QMutexLocker locker(&tagMutex);
        return tagManager.getRejectedTags().join(QStringLiteral(", "));
    }();

    const QString suffix = fi.suffix().toLower();
    const QSet<QString> zipXmlExtractable = zipOrPdfTextExtractSuffixes();
    const QSet<QString> legacyBinaryBlocked = {QStringLiteral("doc"), QStringLiteral("xls"), QStringLiteral("ppt")};
    const bool isTextExt =
        (plainTextFileSuffixes().contains(suffix) || zipXmlExtractable.contains(suffix))
        && !legacyBinaryBlocked.contains(suffix);
    QString contentQ;
    if (isTextExt) {
        if (zipXmlExtractable.contains(suffix)) {
            if (suffix == QStringLiteral("pdf")) {
                contentQ = DocumentParser::extractPdfText(fp);
            } else {
                contentQ = DocumentParser::extractTextQString(fp);
            }
        } else {
            // Read textual content only. (Never feed binary bytes to AI.)
            std::ifstream f(fp.toStdString(), std::ios::binary);
            if (f.is_open()) {
                std::string buf;
                buf.resize(8000);
                f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                buf.resize(static_cast<size_t>(f.gcount()));
                contentQ = QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
            }
        }
    }

    if (zipXmlExtractable.contains(suffix) && contentQ.trimmed().isEmpty()) {
        qDebug() << "MainWindow: extract empty for Office/PDF; using filename stub" << suffix << fp;
        contentQ = QStringLiteral("[Text extraction empty — filename: %1]").arg(filename);
    }

    if (suffix == QStringLiteral("pdf") && contentQ.trimmed().size() > 10) {
        qDebug() << "MainWindow: PDF extracted text length" << contentQ.size() << "for" << fp;
    }

    const bool contentReadable = !contentQ.trimmed().isEmpty();

    // Token/context guard: truncate to 3000 chars before prompt (only when readable).
    if (contentReadable && contentQ.size() > 3000) {
        contentQ = contentQ.left(3000) + QStringLiteral("\n") +
                   LanguageManager::instance().getText(QStringLiteral("...[內容過長已截斷]"));
    }
    const std::string content = contentQ.toStdString();

    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析中…")));

    QFuture<std::string> future = QtConcurrent::run([this, filename, content, rejectedTagsCsv, existingTags, contentReadable, suffix]() {
        // existingTags param is used for "historical tags"; rejectedTagsCsv used to constrain outputs
        return llamaEngine.suggestTags(filename.toStdString(),
                                      content,
                                      rejectedTagsCsv.toStdString(),
                                      existingTags.toStdString(),
                                      contentReadable,
                                      suffix.toStdString());
    });
    watcher->setFuture(future);
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::cancelAnalysis() {
    cancelFlag.store(true);
    m_pendingPrioritySingleFile.clear();
    m_analysisUiWorkActive = false;
    stopAnalysisSpinner();
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("取消中…")));
}

void MainWindow::startBatchAnalysis() {
    if (m_isBatchMode) return;

    m_analysisQueue.clear();
    m_totalBatchSize = 0;
    m_pendingResults.clear();

    // Enqueue all files currently displayed in fileList (current folder first)
    QStringList batchPaths;
    for (int i = 0; i < fileList->count(); ++i) {
        auto *it = fileList->item(i);
        if (!it) continue;
        const QString absPath = it->data(Qt::UserRole).toString();
        if (absPath.isEmpty()) continue;
        QFileInfo fi(absPath);
        if (!fi.exists() || !fi.isFile()) continue;
        if (!isAnalyzableFile(fi)) continue;
        batchPaths << QDir::cleanPath(absPath);
    }
    const QString focus = (fileListMode == FileListMode::PhysicalFolder) ? QDir::cleanPath(currentPath) : QString();
    prioritizeAnalysisPaths(batchPaths, focus);
    for (const QString &p : batchPaths) m_analysisQueue.enqueue(p);

    m_totalBatchSize = m_analysisQueue.size();
    if (m_totalBatchSize <= 0) return;

    m_batchTriggeredByBackgroundAuto = false;
    m_batchHashToPaths.clear();
    m_batchNameConflictPaths.clear();
    m_batchCompletedCount = 0;
    m_folderReportAiTagAdds = 0;
    m_isBatchMode = true;
    beginBatchAnalysisUi();
    processNextInQueue();
}

void MainWindow::processNextInQueue() {
    if (!m_isBatchMode) return;

    if (m_analysisQueue.isEmpty()) {
        // Completed
        const bool finishedBgBatch = m_batchTriggeredByBackgroundAuto;
        m_isBatchMode = false;
        m_currentAnalyzingFile.clear();
        m_backgroundAnalyzeFolderLabel.clear();
        m_priorityFolderBannerPath.clear();
        m_showRestartBackgroundPrompt = false;
        if (m_btnRestartBackgroundAnalyze)
            m_btnRestartBackgroundAnalyze->setVisible(false);
        updateBackgroundStatusLabel();
        flushPendingBatchResults();
        syncBatchProgressBars();

        // Restore UI
        if (btnBatchAnalyze) btnBatchAnalyze->setEnabled(true);
        if (btnStopBatchAnalyze) btnStopBatchAnalyze->setEnabled(false);
        if (btnAnalyzeFile) btnAnalyzeFile->setEnabled(!currentFilePath().isEmpty());
        syncBatchAnalyzeButtonLabel();
        refreshCurrentAnalysisTargetUi();

        // Refresh once at end for UI correctness
        updateTagList();
        reloadCurrentFileListPanel();

        showFolderAnalysisReport();
        if (m_bgAutoAnalyzeEnabled && m_bgAutoAnalyzeDebounce && !rootPath.trimmed().isEmpty()) {
            if (finishedBgBatch) {
                QTimer::singleShot(1500, this, [this]() {
                    if (!m_bgAutoAnalyzeDebounce || !m_bgAutoAnalyzeEnabled || rootPath.trimmed().isEmpty()
                        || m_isBatchMode)
                        return;
                    m_bgAutoAnalyzeDebounce->start();
                });
            } else {
                m_bgAutoAnalyzeDebounce->start();
            }
        }
        refreshFileAndFolderAnalysisIndicators();
        ensureAnalysisIndicatorTimer();
        return;
    }

    const QString nextFile = m_analysisQueue.dequeue();
    m_currentAnalyzingFile = nextFile;
    {
        const QFileInfo nfi(nextFile);
        QString dname = nfi.dir().dirName();
        if (dname.isEmpty())
            dname = QFileInfo(nfi.absolutePath()).fileName();
        if (dname.isEmpty() || dname == QLatin1String(".") || dname == QLatin1String("/"))
            dname = nfi.absolutePath();
        m_backgroundAnalyzeFolderLabel = dname;
    }
    updateBackgroundStatusLabel();
    const int nowDone = m_totalBatchSize - m_analysisQueue.size();
    if (lblBatchStatus) {
        lblBatchStatus->setText(QStringLiteral("%1: %2 (%3/%4)")
                                    .arg(LanguageManager::instance().getText(QStringLiteral("正在資料夾分析")))
                                    .arg(QFileInfo(nextFile).fileName())
                                    .arg(nowDone)
                                    .arg(m_totalBatchSize));
    }

    analyzeFileForPath(nextFile);
    refreshCurrentAnalysisTargetUi();
}

void MainWindow::collectUnanalyzedPathsFromWorkspace(int maxFiles, QStringList *out)
{
    if (!out || maxFiles <= 0 || rootPath.trimmed().isEmpty()) return;

    QStringList roots;
    if (!m_recursiveWatchPaths.isEmpty()) {
        for (const QString &d : m_recursiveWatchPaths) {
            const QString c = QDir::cleanPath(d);
            if (!c.isEmpty()) roots << c;
        }
        std::sort(roots.begin(), roots.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });
        roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    } else {
        roots << QDir::cleanPath(rootPath);
    }

    const bool recursive = chkRecursive && chkRecursive->isChecked();
    const QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QSet<QString> seen;
    for (const QString &rootDir : roots) {
        if (rootDir.isEmpty()) continue;
        QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, flags);
        while (it.hasNext()) {
            const QString p = QDir::cleanPath(it.next());
            if (p.contains(QStringLiteral("/.smartfile")) || p.contains(QStringLiteral("\\.smartfile"))) continue;
            if (seen.contains(p)) continue;
            seen.insert(p);
            const QFileInfo finfo(p);
            if (!isAnalyzableFile(finfo)) continue;
            if (m_aiSummaryByPath.contains(p)) continue;
            *out << p;
            if (out->size() >= maxFiles) return;
        }
    }
}

bool MainWindow::trySystemBypassPreset(const QFileInfo &fi, QString *summaryOut, QStringList *tagsOut) const
{
    if (!m_systemFileBypassEnabled || !summaryOut || !tagsOut) return false;
    summaryOut->clear();
    tagsOut->clear();

    const QString name = fi.fileName();
    const QString sfx = fi.suffix().toLower();
    auto &lm = LanguageManager::instance();

    QString absNorm = QDir::cleanPath(fi.absoluteFilePath());
    absNorm.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString lp = absNorm.toLower();

    auto pathHitsDevHeuristic = [&lp]() -> bool {
        static const QStringList markers = {QStringLiteral("/node_modules/"), QStringLiteral("/.git/"),
                                              QStringLiteral("/venv/"),      QStringLiteral("/build/"),
                                              QStringLiteral("/.idea/")};
        for (const QString &m : markers) {
            if (lp.contains(m, Qt::CaseInsensitive)) return true;
        }
        if (lp.contains(QStringLiteral("/appdata"), Qt::CaseInsensitive)) return true;
        return false;
    };

    const bool inDevPath = pathHitsDevHeuristic();
    if (inDevPath) {
        *summaryOut = lm.getText(QStringLiteral("bypass_summary_dev_dependency"));
        *tagsOut << lm.getText(QStringLiteral("bypass_tag_dev_system_file"));
        return true;
    }

    if (name.contains(QStringLiteral("替身")) || sfx == QStringLiteral("lnk") || sfx == QStringLiteral("alias")) {
        *summaryOut = lm.getText(QStringLiteral("bypass_summary_shortcut"));
        *tagsOut << lm.getText(QStringLiteral("bypass_tag_shortcut"));
        return true;
    }
    if (sfx == QStringLiteral("app") || sfx == QStringLiteral("exe") || sfx == QStringLiteral("bat")) {
        *summaryOut = lm.getText(QStringLiteral("bypass_summary_app"));
        *tagsOut << lm.getText(QStringLiteral("bypass_tag_app"));
        return true;
    }
    if (sfx == QStringLiteral("zip") || sfx == QStringLiteral("rar") || sfx == QStringLiteral("7z")) {
        *summaryOut = lm.getText(QStringLiteral("bypass_summary_archive"));
        *tagsOut << lm.getText(QStringLiteral("bypass_tag_archive"));
        return true;
    }
    return false;
}

void MainWindow::applyPresetBypassAnalysis(const QString &fp, const QString &summary, const QStringList &tags)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("summary"), summary);
    QJsonArray arr;
    for (const QString &t : tags) {
        const QString u = t.trimmed();
        if (!u.isEmpty()) arr.append(u);
    }
    obj.insert(QStringLiteral("tags"), arr);
    obj.insert(QStringLiteral("tags_are_manual"), true);
    obj.insert(QStringLiteral("skip_content_hash"), true);

    if (m_isBatchMode) {
        m_pendingResults.insert(fp, obj);
        ++m_batchCompletedCount;
        setUiBusy(false);
        if (batchProgressBar && m_totalBatchSize > 0) {
            batchProgressBar->setValue(qMin(m_totalBatchSize, m_batchCompletedCount));
        }
        syncBatchProgressBars();
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
        updateBackgroundStatusLabel();
        QTimer::singleShot(0, this, &MainWindow::processNextInQueue);
        refreshCurrentAnalysisTargetUi();
        return;
    }

    if (!summary.isEmpty()) m_aiSummaryByPath.insert(fp, summary);
    {
        QMutexLocker locker(&tagMutex);
        for (const QString &t : tags) {
            const QString u = t.trimmed();
            if (!u.isEmpty()) tagManager.addTag(fp, u, false);
        }
        tagManager.saveTags();
    }
    if (m_aiSummaryEdit && currentFilePath() == fp) m_aiSummaryEdit->setPlainText(summary);
    updateTagDisplayForFile(fp);
    updateTagList();
    reloadCurrentFileListPanel();
    reselectFileInList(fp);
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
    m_currentAnalyzingFile.clear();
    refreshCurrentAnalysisTargetUi();
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::applyColdArchiveAnalysis(const QString &fp, const QString &summary, const QStringList &tags)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("summary"), summary);
    QJsonArray arr;
    for (const QString &t : tags) {
        const QString u = t.trimmed();
        if (!u.isEmpty()) arr.append(u);
    }
    obj.insert(QStringLiteral("tags"), arr);
    obj.insert(QStringLiteral("tags_are_manual"), true);
    obj.insert(QStringLiteral("skip_content_hash"), true);

    if (m_isBatchMode) {
        m_pendingResults.insert(fp, obj);
        ++m_batchCompletedCount;
        setUiBusy(false);
        if (batchProgressBar && m_totalBatchSize > 0) {
            batchProgressBar->setValue(qMin(m_totalBatchSize, m_batchCompletedCount));
        }
        syncBatchProgressBars();
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
        updateBackgroundStatusLabel();
        QTimer::singleShot(0, this, &MainWindow::processNextInQueue);
        refreshCurrentAnalysisTargetUi();
        return;
    }

    if (!summary.isEmpty()) m_aiSummaryByPath.insert(fp, summary);
    {
        QMutexLocker locker(&tagMutex);
        for (const QString &t : tags) {
            const QString u = t.trimmed();
            if (!u.isEmpty()) tagManager.addTag(fp, u, false);
        }
        tagManager.saveTags();
    }
    if (m_aiSummaryEdit && currentFilePath() == fp) m_aiSummaryEdit->setPlainText(summary);
    updateTagDisplayForFile(fp);
    updateTagList();
    reloadCurrentFileListPanel();
    reselectFileInList(fp);
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
    m_currentAnalyzingFile.clear();
    refreshCurrentAnalysisTargetUi();
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

bool MainWindow::tryColdArchiveBypass(const QFileInfo &fi, bool forceLlm, QString *summaryOut, QStringList *tagsOut) const
{
    if (!summaryOut || !tagsOut) return false;
    summaryOut->clear();
    tagsOut->clear();
    if (m_coldArchiveYears <= 0 || forceLlm) return false;

    const QDateTime cutoff = QDateTime::currentDateTime().addYears(-m_coldArchiveYears);
    if (fi.lastModified() >= cutoff) return false;

    *summaryOut = QStringLiteral("因長時間未修改，系統已將其自動封存以節省分析算力。");
    *tagsOut << QStringLiteral("[AI] 封存冷資料");
    return true;
}

void MainWindow::loadBackgroundAutoAnalyzeSetting()
{
    QSettings s;
    m_bgAutoAnalyzeEnabled = s.value(QStringLiteral("workspace/background_auto_analysis"), false).toBool();
    m_systemFileBypassEnabled = s.value(QStringLiteral("workspace/system_file_bypass_filter"), true).toBool();
    applyFilesystemWatchPolicy();
    ensureRecursiveWatchCoversWorkspace();
    updateBackgroundStatusLabel();
    if (m_bgAutoAnalyzeEnabled && m_bgAutoAnalyzeDebounce && !rootPath.trimmed().isEmpty())
        m_bgAutoAnalyzeDebounce->start();

    loadColdArchiveYearsSetting();
}

void MainWindow::loadColdArchiveYearsSetting()
{
    QSettings s;
    m_coldArchiveYears = s.value(QStringLiteral("workspace/cold_archive_years"), 0).toInt();
}

void MainWindow::watchDirectoryRecursively(const QString &rootPathParam)
{
    if (!m_dirWatcher || rootPathParam.trimmed().isEmpty()) return;

    const QString clean = QDir::cleanPath(rootPathParam);
    QStringList toAdd;
    if (!m_recursiveWatchPaths.contains(clean)) {
        m_recursiveWatchPaths.insert(clean);
        toAdd << clean;
    }

    QDirIterator it(clean, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString d = QDir::cleanPath(it.next());
        if (d.contains(QStringLiteral("/.smartfile")) || d.contains(QStringLiteral("\\.smartfile"))) continue;
        if (!m_recursiveWatchPaths.contains(d)) {
            m_recursiveWatchPaths.insert(d);
            toAdd << d;
        }
    }

    for (const QString &p : toAdd) {
        if (!m_dirWatcher->addPath(p))
            qWarning() << "QFileSystemWatcher addPath failed:" << p;
    }
}

void MainWindow::applyFilesystemWatchPolicy()
{
    if (!m_dirWatcher) return;
    const QStringList oldDirs = m_dirWatcher->directories();
    if (!oldDirs.isEmpty()) m_dirWatcher->removePaths(oldDirs);
    m_recursiveWatchPaths.clear();

    if (rootPath.trimmed().isEmpty()) return;

    if (m_bgAutoAnalyzeEnabled) {
        watchDirectoryRecursively(rootPath);
    } else {
        const QString clean = QDir::cleanPath(rootPath);
        if (m_dirWatcher->addPath(clean))
            m_recursiveWatchPaths.insert(clean);
        else
            qWarning() << "QFileSystemWatcher addPath failed:" << clean;
    }
}

void MainWindow::ensureRecursiveWatchCoversWorkspace()
{
    if (!m_bgAutoAnalyzeEnabled || !m_dirWatcher || rootPath.trimmed().isEmpty()) return;

    const QString clean = QDir::cleanPath(rootPath);
    QStringList toAdd;
    if (!m_recursiveWatchPaths.contains(clean)) {
        m_recursiveWatchPaths.insert(clean);
        toAdd << clean;
    }

    QDirIterator it(clean, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString d = QDir::cleanPath(it.next());
        if (d.contains(QStringLiteral("/.smartfile")) || d.contains(QStringLiteral("\\.smartfile"))) continue;
        if (!m_recursiveWatchPaths.contains(d)) {
            m_recursiveWatchPaths.insert(d);
            toAdd << d;
        }
    }

    const QStringList watched = m_dirWatcher->directories();
    for (const QString &p : toAdd) {
        if (watched.contains(p)) continue;
        if (!m_dirWatcher->addPath(p))
            qWarning() << "QFileSystemWatcher addPath failed (dynamic):" << p;
    }
}

void MainWindow::primeAnalysisCacheFromDisk(const QString &sha256Hex)
{
    if (sha256Hex.isEmpty() || m_analysisByContentHash.contains(sha256Hex)) return;
    QJsonObject obj;
    {
        QMutexLocker locker(&tagMutex);
        if (tagManager.tryGetHashAnalysis(sha256Hex, &obj))
            m_analysisByContentHash.insert(sha256Hex, obj);
    }
}

void MainWindow::onBackgroundAutoAnalyzeDebounce()
{
    if (!m_bgAutoAnalyzeEnabled) {
        m_bgAnalyzeQueueRetries = 0;
        return;
    }
    if (rootPath.trimmed().isEmpty()) {
        m_bgAnalyzeQueueRetries = 0;
        return;
    }
    if (!llamaEngine.isModelLoaded()) {
        m_bgAnalyzeQueueRetries = 0;
        return;
    }
    if (m_isBatchMode) {
        m_bgAnalyzeQueueRetries = 0;
        return;
    }
    if (watcher && watcher->isRunning()) {
        if (m_bgAnalyzeQueueRetries < 50) {
            ++m_bgAnalyzeQueueRetries;
            QTimer::singleShot(1500, this, &MainWindow::onBackgroundAutoAnalyzeDebounce);
        } else {
            m_bgAnalyzeQueueRetries = 0;
        }
        return;
    }
    m_bgAnalyzeQueueRetries = 0;
    if (m_consolidateWatcher && m_consolidateWatcher->isRunning()) return;

    QStringList paths;
    collectUnanalyzedPathsFromWorkspace(10, &paths);
    if (paths.isEmpty()) return;

    const QString focus = (fileListMode == FileListMode::PhysicalFolder) ? QDir::cleanPath(currentPath) : QString();
    prioritizeAnalysisPaths(paths, focus);

    m_pendingResults.clear();
    m_analysisQueue.clear();
    for (const QString &p : paths) m_analysisQueue.enqueue(p);
    m_totalBatchSize = m_analysisQueue.size();
    if (m_totalBatchSize <= 0) return;

    m_batchTriggeredByBackgroundAuto = true;
    m_batchHashToPaths.clear();
    m_batchNameConflictPaths.clear();
    m_batchCompletedCount = 0;
    m_folderReportAiTagAdds = 0;
    m_isBatchMode = true;
    beginBatchAnalysisUi();
    updateBackgroundStatusLabel();
    processNextInQueue();
    updateBackgroundStatusLabel();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::beginBatchAnalysisUi()
{
    m_showRestartBackgroundPrompt = false;
    if (m_btnRestartBackgroundAnalyze)
        m_btnRestartBackgroundAnalyze->setVisible(false);

    if (batchProgressBar) {
        batchProgressBar->setRange(0, m_totalBatchSize);
        batchProgressBar->setValue(0);
        batchProgressBar->setFormat(QStringLiteral("%p%"));
        batchProgressBar->update();
        batchProgressBar->repaint();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (lblBatchStatus) {
        lblBatchStatus->setText(LanguageManager::instance().getText(QStringLiteral("正在資料夾分析")));
    }
    if (btnAnalyzeFile) btnAnalyzeFile->setEnabled(false);
    if (btnBatchAnalyze) btnBatchAnalyze->setEnabled(false);
    if (btnStopBatchAnalyze) btnStopBatchAnalyze->setEnabled(true);
    updateBackgroundStatusLabel();
    syncBatchProgressBars();
    syncBatchAnalyzeButtonLabel();
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::applyCachedAnalysisForHashHit(const QString &fp, const QJsonObject &cached, const QString &contentHashHex)
{
    const QJsonObject copy = QJsonDocument::fromJson(QJsonDocument(cached).toJson()).object();

    if (m_isBatchMode) {
        m_pendingResults.insert(fp, copy);
        if (!contentHashHex.isEmpty()) recordBatchPathForContentHash(contentHashHex, fp);
        return;
    }

    const QString summary = copy.value(QStringLiteral("summary")).toString().trimmed();
    if (!summary.isEmpty()) m_aiSummaryByPath.insert(fp, summary);

    {
        QMutexLocker locker(&tagMutex);
        const QJsonValue tagsV = copy.value(QStringLiteral("tags"));
        if (tagsV.isArray()) {
            const QJsonArray arr = tagsV.toArray();
            for (const auto &v : arr) {
                const QString t = v.toString().trimmed();
                if (t.isEmpty()) continue;
                tagManager.addTag(fp, QStringLiteral("[AI] ") + t, false);
            }
        }
        tagManager.saveTags();
    }

    if (m_aiSummaryEdit && currentFilePath() == fp) m_aiSummaryEdit->setPlainText(summary);
    updateTagDisplayForFile(fp);
    updateTagList();
    reloadCurrentFileListPanel();
    reselectFileInList(fp);
    m_currentAnalyzingFile.clear();
    refreshCurrentAnalysisTargetUi();
    refreshFileAndFolderAnalysisIndicators();
    ensureAnalysisIndicatorTimer();
}

void MainWindow::showFolderAnalysisReport()
{
    auto &lm = LanguageManager::instance();
    const int n = m_batchCompletedCount;
    const int x = m_folderReportAiTagAdds;

    auto filterMultiPath = [](const QMap<QString, QSet<QString>> &in) {
        QMap<QString, QSet<QString>> out;
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            if (it.value().size() >= 2) out.insert(it.key(), it.value());
        }
        return out;
    };

    const QMap<QString, QSet<QString>> hashGroups = filterMultiPath(m_batchHashToPaths);
    const QMap<QString, QSet<QString>> nameGroups = filterMultiPath(m_batchNameConflictPaths);

    int hashDupCount = 0;
    for (auto it = hashGroups.constBegin(); it != hashGroups.constEnd(); ++it) {
        hashDupCount += static_cast<int>(it.value().size()) - 1;
    }
    int nameDupCount = 0;
    for (auto it = nameGroups.constBegin(); it != nameGroups.constEnd(); ++it) {
        nameDupCount += static_cast<int>(it.value().size()) - 1;
    }

    const bool showTree = !hashGroups.isEmpty() || !nameGroups.isEmpty();
    const int yTotal = hashDupCount + nameDupCount;

    const bool silentBackground = m_batchTriggeredByBackgroundAuto;

    if (n <= 0 && x <= 0 && yTotal <= 0 && !showTree) {
        m_batchHashToPaths.clear();
        m_batchNameConflictPaths.clear();
        m_batchCompletedCount = 0;
        m_folderReportAiTagAdds = 0;
        m_batchTriggeredByBackgroundAuto = false;
        return;
    }

    if (silentBackground) {
        QStringList block;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        block << lm.getText(QStringLiteral("bg_log_completion")).arg(ts).arg(x).arg(yTotal);
        if (yTotal > 0) {
            QSet<QString> pathsOut;
            for (auto it = hashGroups.constBegin(); it != hashGroups.constEnd(); ++it) {
                for (const QString &p : it.value()) pathsOut.insert(p);
            }
            for (auto it = nameGroups.constBegin(); it != nameGroups.constEnd(); ++it) {
                for (const QString &p : it.value()) pathsOut.insert(p);
            }
            QStringList sorted;
            sorted.reserve(static_cast<int>(pathsOut.size()));
            for (const QString &p : pathsOut) sorted.append(p);
            std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
                return a.localeAwareCompare(b) < 0;
            });
            for (const QString &p : sorted) block << p;
        }
        appendTaskCenterLog(block.join(QStringLiteral("\n")));
        mergeTaskCenterRedundancyBatch(n, x, showTree ? hashGroups : QMap<QString, QSet<QString>>(),
                                       showTree ? nameGroups : QMap<QString, QSet<QString>>());

        m_batchHashToPaths.clear();
        m_batchNameConflictPaths.clear();
        m_batchCompletedCount = 0;
        m_folderReportAiTagAdds = 0;
        m_batchTriggeredByBackgroundAuto = false;
        return;
    }

    if (showTree) {
        mergeTaskCenterRedundancyBatch(n, x, hashGroups, nameGroups);
        RedundancyReportDialog dlg(this, n, x, hashDupCount, nameDupCount, hashGroups, nameGroups);
        connect(&dlg, &RedundancyReportDialog::redundantFilesRemoved, this, [this](const QStringList &paths) {
            {
                QMutexLocker locker(&tagMutex);
                for (const QString &p : paths) tagManager.removeFileMetadata(p, false);
                tagManager.saveTags();
            }
            for (const QString &p : paths) m_aiSummaryByPath.remove(p);
            scanFiles();
            updateTagList();
            if (m_bgAutoAnalyzeEnabled) ensureRecursiveWatchCoversWorkspace();
            pruneTaskCenterPersistentRedundancy(paths);
        });
        dlg.exec();
    } else {
        mergeTaskCenterRedundancyBatch(n, x, QMap<QString, QSet<QString>>(), QMap<QString, QSet<QString>>());
        const QString body = lm.getText(QStringLiteral("folder_report_body")).arg(n).arg(x).arg(yTotal);
        QMessageBox::information(this, lm.getText(QStringLiteral("folder_report_title")), body);
    }

    m_batchHashToPaths.clear();
    m_batchNameConflictPaths.clear();
    m_batchCompletedCount = 0;
    m_folderReportAiTagAdds = 0;
    m_batchTriggeredByBackgroundAuto = false;
}

void MainWindow::flushPendingBatchResults() {
    if (m_pendingResults.isEmpty()) return;

    int tagAddBatch = 0;
    for (auto it = m_pendingResults.constBegin(); it != m_pendingResults.constEnd(); ++it) {
        const QJsonValue tagsV = it.value().value(QStringLiteral("tags"));
        if (!tagsV.isArray()) continue;
        const QJsonArray arr = tagsV.toArray();
        for (const auto &v : arr) {
            if (!v.toString().trimmed().isEmpty()) ++tagAddBatch;
        }
    }
    m_folderReportAiTagAdds += tagAddBatch;

    QMutexLocker locker(&tagMutex);
    for (auto it = m_pendingResults.constBegin(); it != m_pendingResults.constEnd(); ++it) {
        const QString fp = it.key();
        const QJsonObject obj = it.value();
        if (obj.value(QStringLiteral("skip_content_hash")).toBool())
            continue;
        const QString hx = sha256HexOfFile(fp);
        if (!hx.isEmpty()) {
            tagManager.recordHashAnalysis(hx, obj, false);
            tagManager.setFileContentHash(fp, hx, false);
        }
    }

    for (auto it = m_pendingResults.constBegin(); it != m_pendingResults.constEnd(); ++it) {
        const QString fp = it.key();
        const QJsonObject obj = it.value();

        const QString summary = obj.value(QStringLiteral("summary")).toString().trimmed();
        if (!summary.isEmpty()) {
            m_aiSummaryByPath.insert(fp, summary);
        }

        const bool manualTags = obj.value(QStringLiteral("tags_are_manual")).toBool(false);
        const QJsonValue tagsV = obj.value(QStringLiteral("tags"));
        if (tagsV.isArray()) {
            const QJsonArray arr = tagsV.toArray();
            for (const auto &v : arr) {
                const QString t = v.toString().trimmed();
                if (t.isEmpty()) continue;
                tagManager.addTag(fp, manualTags ? t : (QStringLiteral("[AI] ") + t), false);
            }
        }
    }
    tagManager.saveTags();
    m_pendingResults.clear();
}

void MainWindow::onAnalysisFinished() {
    setUiBusy(false);

    const QString fp = m_currentAnalyzingFile.isEmpty() ? currentFilePath() : m_currentAnalyzingFile;
    const std::string raw = watcher->result();
    const QString qRaw = QString::fromStdString(raw);

    if (raw.rfind("Error:", 0) == 0) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析失敗")));
        QMessageBox::critical(this, QStringLiteral("Error"), qRaw);
        if (m_isBatchMode) {
            ++m_batchCompletedCount;
            setUiBusy(false);
            updateBackgroundStatusLabel();
            QTimer::singleShot(100, this, &MainWindow::processNextInQueue);
        }
        m_currentAnalyzingFile.clear();
        refreshCurrentAnalysisTargetUi();
        clearAnalysisWorkFlagsAndSyncUi();
        return;
    }

    if (cancelFlag.load()) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("已取消")));
        if (m_isBatchMode) {
            ++m_batchCompletedCount;
            setUiBusy(false);
            updateBackgroundStatusLabel();
            QTimer::singleShot(100, this, &MainWindow::processNextInQueue);
        }
        m_currentAnalyzingFile.clear();
        refreshCurrentAnalysisTargetUi();
        clearAnalysisWorkFlagsAndSyncUi();
        return;
    }

    auto stripMarkdownFences = [](QString t) -> QString {
        t = t.trimmed();
        t.remove(QRegularExpression(QStringLiteral("^```\\s*json\\s*"), QRegularExpression::CaseInsensitiveOption));
        t.remove(QRegularExpression(QStringLiteral("^```\\s*"), QRegularExpression::CaseInsensitiveOption));
        t.remove(QRegularExpression(QStringLiteral("```\\s*$")));
        return t.trimmed();
    };

    auto repairCommonJsonGlitches = [](QString jsonText) -> QString {
        // Repair common model glitch: stray empty string field like:  {"summary":"...","","tags":[...]}
        jsonText.replace(QRegularExpression(QStringLiteral(",\\s*\"\"\\s*")), QString());
        jsonText.replace(QRegularExpression(QStringLiteral("\"\"\\s*,")), QString());
        jsonText.replace(QRegularExpression(QStringLiteral(",\\s*,+")), QStringLiteral(","));
        jsonText.replace(QRegularExpression(QStringLiteral("\\{\\s*,")), QStringLiteral("{"));
        jsonText.replace(QRegularExpression(QStringLiteral(",\\s*\\}")), QStringLiteral("}"));
        return jsonText.trimmed();
    };

    auto extractJsonObjectCandidates = [&](const QString &s) -> QStringList {
        // Model sometimes outputs multiple JSON objects back-to-back.
        // Extract all minimal "{...}" blocks and let the parser decide.
        const QString t = stripMarkdownFences(s);
        QRegularExpression re(QStringLiteral("\\{.*?\\}"));
        re.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
        QStringList out;
        auto it = re.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            if (!m.hasMatch()) continue;
            const QString cand = m.captured(0).trimmed();
            if (!cand.isEmpty()) out << cand;
        }
        return out;
    };

    auto normalizeAiTag = [](QString tag) -> QString {
        tag = tag.trimmed().toLower();
        tag.replace(QRegularExpression(QStringLiteral("^[-•\\s]+")), QString());
        tag.replace(QRegularExpression(QStringLiteral("^[\"'`]+|[\"'`]+$")), QString());
        tag.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        tag = tag.trimmed();
        return tag;
    };

    QString summary;
    std::vector<QString> tags;

    {
        const QStringList candidates = extractJsonObjectCandidates(qRaw);
        QStringList tagsList;

        for (const QString &cand0 : candidates) {
            const QString cand = repairCommonJsonGlitches(cand0);
            if (cand.isEmpty()) continue;

            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(cand.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

            const QJsonObject obj = doc.object();

            const QString s = obj.value(QStringLiteral("summary")).toString().trimmed();
            if (summary.isEmpty() && !s.isEmpty()) summary = s;

            const QJsonValue tagsV = obj.value(QStringLiteral("tags"));
            if (tagsV.isArray()) {
                const QJsonArray arr = tagsV.toArray();
                for (const auto &v : arr) {
                    if (tagsList.size() >= 3) break; // hard limit across all blocks
                    QString t = normalizeAiTag(v.toString());
                    if (t.isEmpty()) continue;
                    if (t == QStringLiteral("ai")) continue; // drop meaningless tag
                    if (t.size() > 15) continue; // drop long phrases
                    tagsList << t;
                }
            }

            if (!summary.isEmpty() && tagsList.size() >= 3) break;
        }

        if (!tagsList.isEmpty()) {
            QSet<QString> seen;
            tags.clear();
            for (const QString &raw : tagsList) {
                if (tags.size() >= 3) break;
                QString t = TagManager::stripAiPrefix(raw.trimmed());
                t = normalizeAiTag(t);
                if (t.isEmpty()) continue;
                if (t == QStringLiteral("ai")) continue;
                if (t.size() > 15) continue;
                if (seen.contains(t)) continue;
                seen.insert(t);
                tags.push_back(t);
            }
        }
    }

    // Fallback: treat whole raw as summary + tags via legacy sanitizer
    if (summary.isEmpty()) summary = qRaw.trimmed();
    if (tags.empty()) {
        // Fallback tags: still enforce hard limits to avoid long-sentence hallucinations.
        const auto rawTags = sanitizeAiTags(qRaw);
        std::vector<QString> filtered;
        QSet<QString> seen;
        for (const auto &t0 : rawTags) {
            if (filtered.size() >= 3) break;
            QString t = normalizeAiTag(t0);
            if (t.isEmpty()) continue;
            if (t == QStringLiteral("ai")) continue;
            if (t.size() > 15) continue;
            if (seen.contains(t)) continue;
            seen.insert(t);
            filtered.push_back(t);
        }
        tags = filtered;
    }

    if (fp.isEmpty()) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成（無選取檔案）")));
        if (m_isBatchMode) {
            ++m_batchCompletedCount;
            QTimer::singleShot(100, this, &MainWindow::processNextInQueue);
        }
        updateBackgroundStatusLabel();
        m_currentAnalyzingFile.clear();
        refreshCurrentAnalysisTargetUi();
        clearAnalysisWorkFlagsAndSyncUi();
        return;
    }

    QString persistedHash;
    QJsonObject persistedCache;
    {
        const QString hx = sha256HexOfFile(fp);
        if (!hx.isEmpty() && (!summary.isEmpty() || !tags.empty())) {
            QJsonObject cache;
            cache.insert(QStringLiteral("summary"), summary);
            QJsonArray a;
            for (const auto &t : tags) a.append(t);
            cache.insert(QStringLiteral("tags"), a);
            m_analysisByContentHash.insert(hx, cache);
            persistedHash = hx;
            persistedCache = cache;
        }
    }

    if (!persistedHash.isEmpty()) {
        recordBatchPathForContentHash(persistedHash, fp);
    }

    if (m_isBatchMode) {
        ++m_batchCompletedCount;
    }
    updateBackgroundStatusLabel();

    // Smooth progress update (on completion)
    if (m_isBatchMode && batchProgressBar) {
        const int completed = m_batchCompletedCount;
        if (!m_batchProgressAnim) {
            m_batchProgressAnim = new QPropertyAnimation(batchProgressBar, "value", this);
            m_batchProgressAnim->setDuration(500);
            connect(m_batchProgressAnim, &QAbstractAnimation::finished, this, &MainWindow::syncBatchProgressBars);
            connect(m_batchProgressAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                if (m_taskCenterBatchProgress && m_isBatchMode && m_batchTriggeredByBackgroundAuto)
                    m_taskCenterBatchProgress->setValue(v.toInt());
            });
        }
        m_batchProgressAnim->stop();
        m_batchProgressAnim->setStartValue(batchProgressBar->value());
        m_batchProgressAnim->setEndValue(completed);
        m_batchProgressAnim->start();
    }

    if (m_isBatchMode) {
        // Buffer results and write once at the end.
        QJsonObject obj;
        obj.insert(QStringLiteral("summary"), summary);
        QJsonArray arr;
        for (const auto &t : tags) arr.append(t);
        obj.insert(QStringLiteral("tags"), arr);
        m_pendingResults.insert(fp, obj);
    } else {
        {
            QMutexLocker locker(&tagMutex);
            for (const auto &t : tags) tagManager.addTag(fp, QStringLiteral("[AI] ") + t, false);
            if (!persistedHash.isEmpty()) {
                tagManager.recordHashAnalysis(persistedHash, persistedCache, false);
                tagManager.setFileContentHash(fp, persistedHash, false);
            }
            tagManager.saveTags();
        }

        m_aiSummaryByPath.insert(fp, summary);
        if (m_aiSummaryEdit) m_aiSummaryEdit->setPlainText(summary);

        updateTagDisplayForFile(fp);
        updateTagList();
        reloadCurrentFileListPanel();
        reselectFileInList(fp);
    }

    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));

    if (m_isBatchMode) {
        QTimer::singleShot(100, this, &MainWindow::processNextInQueue);
    } else if (!m_pendingPrioritySingleFile.isEmpty()) {
        const QString nextP = QDir::cleanPath(m_pendingPrioritySingleFile);
        m_pendingPrioritySingleFile.clear();
        if (!nextP.isEmpty() && nextP != fp)
            QTimer::singleShot(0, this, [this, nextP]() { analyzeFileForPath(nextP, true); });
    } else if (m_bgAutoAnalyzeEnabled && m_bgAutoAnalyzeDebounce && !rootPath.trimmed().isEmpty()) {
        m_bgAutoAnalyzeDebounce->start();
    }

    m_currentAnalyzingFile.clear();
    refreshCurrentAnalysisTargetUi();
    clearAnalysisWorkFlagsAndSyncUi();
}

void MainWindow::generateTagFoldersWithAI() {
    if (m_isConsolidatingTags) return;
    if (!llamaEngine.isModelLoaded()) {
        QMessageBox::warning(this,
                             QStringLiteral("Smartflie"),
                             LanguageManager::instance().getText(QStringLiteral("模型自動載入失敗 (Auto-load failed)")));
        return;
    }

    struct TagCount {
        QString tag;
        int count = 0;
    };
    QVector<TagCount> ranked;
    QSet<QString> seen;
    {
        std::vector<QString> rawTags;
        {
            QMutexLocker locker(&tagMutex);
            rawTags = tagManager.getAllTags();
        }
        for (const QString &t : rawTags) {
            const QString tt = t.trimmed();
            if (!TagManager::hasAiPrefix(tt) || seen.contains(tt)) continue;
            seen.insert(tt);
            int n = 0;
            {
                QMutexLocker locker(&tagMutex);
                n = static_cast<int>(tagManager.getFilesByTag(tt).size());
            }
            ranked.push_back({tt, n});
        }
    }

    std::sort(ranked.begin(), ranked.end(), [](const TagCount &a, const TagCount &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.tag.localeAwareCompare(b.tag) < 0;
    });

    QStringList aiTags;
    const int cap = qMin(100, ranked.size());
    for (int i = 0; i < cap; ++i) aiTags << ranked[i].tag;

    if (aiTags.size() < 2) return;

    m_isConsolidatingTags = true;
    updateAllTexts();

    auto &lm = LanguageManager::instance();

    const QString drawersBlock = QStringLiteral(
        "[營運管理]\n"
        "[技術與開發]\n"
        "[財務與法務]\n"
        "[行銷與企劃]\n"
        "[人事與行政]\n"
        "[多媒體資源]\n"
        "[會議與報告]\n"
        "[其他雜項]\n");

    const QString systemPromptEn = QStringLiteral(
        "You are a constrained taxonomy assistant. You receive up to 100 existing AI tags (each begins with [AI]). "
        "You MUST assign each tag you output to EXACTLY ONE of the following EIGHT JSON keys. Keys MUST match character-for-character (including square brackets):\n"
        "%1"
        "Return ONLY one JSON object mapping each key to a JSON array of tag strings copied EXACTLY from the input list. "
        "ALL eight keys MUST appear. Use [] for categories with no tags. Each input tag appears at most once overall. "
        "If a tag fits nowhere, omit it. No markdown, no commentary.")
        .arg(drawersBlock);

    const QString systemPromptZh = QStringLiteral(
        "你是一位「受控分類」助理。以下最多 100 個既有的 AI 標籤（每個皆含 [AI] 前綴）。"
        "你必須將你要輸出的每一個標籤，嚴格分配到下列八個 JSON Key 之一；Key 必須與下列文字完全一致（含方括號）：\n"
        "%1"
        "僅輸出一個 JSON 物件：每個 Key 對應一個字串陣列，陣列中的標籤必須與輸入清單逐字一致。"
        "八個 Key 都必須出現；沒有標籤的類別請輸出空陣列 []。同一輸入標籤最多出現一次。無法歸類者可略過。不要 markdown 或說明。")
        .arg(drawersBlock);

    const QString systemPrompt = (lm.language() == LanguageManager::Language::EN_US) ? systemPromptEn : systemPromptZh;

    const QString userPrompt = QStringLiteral("Tags:\n- %1\n").arg(aiTags.join(QStringLiteral("\n- ")));

    const QString fullPrompt = QStringLiteral("System:\n%1\n\nUser:\n%2").arg(systemPrompt, userPrompt);

    if (!m_consolidateWatcher) {
        m_isConsolidatingTags = false;
        updateAllTexts();
        return;
    }

    m_consolidateWatcher->setFuture(QtConcurrent::run([this, fullPrompt]() {
        return llamaEngine.generateResponse(fullPrompt.toStdString());
    }));
}

static QString extractFirstBalancedJsonObject(const QString &rawIn)
{
    QString raw = rawIn;
    if (raw.size() > 400000)
        raw.truncate(400000);
    const int start = raw.indexOf(QLatin1Char('{'));
    if (start < 0)
        return QStringLiteral("{}");
    int depth = 0;
    bool inStr = false;
    for (int i = start; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (inStr) {
            if (c == QLatin1Char('\\') && i + 1 < raw.size()) {
                ++i;
                continue;
            }
            if (c == QLatin1Char('"'))
                inStr = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inStr = true;
            continue;
        }
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return raw.mid(start, i - start + 1);
        }
    }
    return QStringLiteral("{}");
}

static QString extractFirstBalancedJsonArray(const QString &rawIn)
{
    QString raw = rawIn;
    if (raw.size() > 400000)
        raw.truncate(400000);
    const int start = raw.indexOf(QLatin1Char('['));
    if (start < 0)
        return QStringLiteral("[]");
    int depth = 0;
    bool inStr = false;
    for (int i = start; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (inStr) {
            if (c == QLatin1Char('\\') && i + 1 < raw.size()) {
                ++i;
                continue;
            }
            if (c == QLatin1Char('"'))
                inStr = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inStr = true;
            continue;
        }
        if (c == QLatin1Char('['))
            ++depth;
        else if (c == QLatin1Char(']')) {
            --depth;
            if (depth == 0)
                return raw.mid(start, i - start + 1);
        }
    }
    return QStringLiteral("[]");
}

static void collectIntegersFromJsonArray(const QJsonArray &arr, QVector<int> *out)
{
    if (!out) return;
    for (const QJsonValue &v : arr) {
        if (v.isDouble()) {
            const int n = static_cast<int>(v.toDouble());
            if (n > 0) out->append(n);
        } else if (v.isString()) {
            bool ok = false;
            const int n = v.toString().trimmed().toInt(&ok);
            if (ok && n > 0) out->append(n);
        }
    }
}

static QVector<int> parseSemanticRetrieverIdList(const QString &raw)
{
    try {
        QVector<int> ids;

        const QString arrText = extractFirstBalancedJsonArray(raw);
        QJsonParseError e1{};
        const QJsonDocument d1 = QJsonDocument::fromJson(arrText.toUtf8(), &e1);
        if (e1.error == QJsonParseError::NoError && d1.isArray()) {
            collectIntegersFromJsonArray(d1.array(), &ids);
            if (!ids.isEmpty()) return ids;
        }

        const QString objText = extractFirstBalancedJsonObject(raw);
        QJsonParseError e2{};
        const QJsonDocument d2 = QJsonDocument::fromJson(objText.toUtf8(), &e2);
        if (e2.error == QJsonParseError::NoError && d2.isObject()) {
            const QJsonObject o = d2.object();
            const QStringList keyNames = {QStringLiteral("ids"), QStringLiteral("fileIds"), QStringLiteral("file_ids")};
            for (const QString &k : keyNames) {
                if (o.contains(k) && o.value(k).isArray()) {
                    collectIntegersFromJsonArray(o.value(k).toArray(), &ids);
                    if (!ids.isEmpty()) return ids;
                }
            }
        }

        return ids;
    } catch (...) {
        return {};
    }
}

void MainWindow::applyTagFolderClustersFromRaw(const QString &rawIn)
{
    try {
        QString capped = rawIn;
        if (capped.size() > 500000)
            capped.truncate(500000);

        const QString jsonText = extractFirstBalancedJsonObject(capped);

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, QStringLiteral("Smartflie"),
                                 QStringLiteral("Invalid JSON: %1").arg(err.errorString()));
            return;
        }

        QSet<QString> aiTagSet;
        {
            std::vector<QString> allTags;
            {
                QMutexLocker locker(&tagMutex);
                allTags = tagManager.getAllTags();
            }
            for (const QString &t : allTags) {
                const QString tt = t.trimmed();
                if (TagManager::hasAiPrefix(tt))
                    aiTagSet.insert(tt);
            }
        }

        auto resolveChild = [&](const QString &raw) -> QString {
            return fuzzyResolveAiTagKey(raw, aiTagSet);
        };

        const QJsonObject rootObj = doc.object();

        QHash<QString, QSet<QString>> bucket;
        for (const QString &k : sfFixedAiClusterDrawerKeys())
            bucket.insert(k, {});

        for (auto it = rootObj.begin(); it != rootObj.end(); ++it) {
            const QString canonKey = sfNormalizeDrawerJsonKeyToCanon(it.key());
            if (canonKey.isEmpty()) continue;

            QStringList members;
            const QJsonValue v = it.value();
            if (v.isArray()) {
                for (const auto &el : v.toArray()) {
                    const QString s = el.toString().trimmed();
                    if (!s.isEmpty()) members << s;
                }
            } else if (v.isString()) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty()) members << s;
            }

            for (const QString &m : members) {
                const QString child = resolveChild(m);
                if (child.isEmpty()) continue;
                bucket[canonKey].insert(child);
            }
        }

        QHash<QString, QString> newAiTagToDrawerKey;
        for (const QString &drawerKey : sfFixedAiClusterDrawerKeys()) {
            const QSet<QString> kids = bucket.value(drawerKey);
            for (const QString &child : kids)
                newAiTagToDrawerKey.insert(child, drawerKey);
        }

        QStringList synthParents;
        for (const QString &dk : sfFixedAiClusterDrawerKeys())
            synthParents.append(sfAiFolderTagForDrawerCanon(dk));
        {
            QMutexLocker locker(&tagMutex);
            tagManager.stripAiTagParentsForSyntheticFolders(synthParents, false);
            tagManager.saveTags();
        }

        QTimer::singleShot(0, this, [this, newAiTagToDrawerKey]() mutable {
            try {
                m_aiTagToDrawerKey.swap(newAiTagToDrawerKey);
                saveAiUiDrawerAssignments();
                if (m_systemTagListWidget && m_aiTagTreeWidget)
                    updateTagList();
                QMessageBox::information(
                    this, QStringLiteral("Smartflie"),
                    QStringLiteral("已套用 %1 個 AI 標籤的 UI 分類（未修改檔案標籤）。")
                        .arg(m_aiTagToDrawerKey.size()));
            } catch (const std::exception &e) {
                QMessageBox::critical(this, QStringLiteral("Smartflie"),
                                      QStringLiteral("套用 AI 分類 UI 時發生錯誤：%1")
                                          .arg(QString::fromUtf8(e.what())));
            } catch (...) {
                QMessageBox::critical(this, QStringLiteral("Smartflie"),
                                      QStringLiteral("套用 AI 分類 UI 時發生未知錯誤。"));
            }
        });
    } catch (const std::exception &e) {
        QMessageBox::warning(this, QStringLiteral("Smartflie"),
                             QStringLiteral("解析 AI 分類回應時發生錯誤：%1")
                                 .arg(QString::fromUtf8(e.what())));
    } catch (...) {
        QMessageBox::warning(this, QStringLiteral("Smartflie"),
                             QStringLiteral("解析 AI 分類回應時發生未知錯誤。"));
    }
}

void MainWindow::onTagFolderClustersFinished()
{
    if (!m_consolidateWatcher) {
        m_isConsolidatingTags = false;
        updateAllTexts();
        return;
    }

    QString raw = QString::fromStdString(m_consolidateWatcher->result());
    if (raw.size() > 500000)
        raw.truncate(500000);

    if (raw.trimmed().startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)) {
        m_isConsolidatingTags = false;
        updateAllTexts();
        QMessageBox::critical(this, QStringLiteral("Smartflie"), raw);
        return;
    }

    QTimer::singleShot(0, this, [this, raw]() {
        applyTagFolderClustersFromRaw(raw);
        m_isConsolidatingTags = false;
        updateAllTexts();
    });
}

void MainWindow::saveTags() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) return;
    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }
    lblStatus->setText(QStringLiteral("已儲存"));
    btnSaveTags->setEnabled(false);
}

void MainWindow::addTag() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Add Tag"), QStringLiteral("標籤:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || t.isEmpty()) return;

    {
        QMutexLocker locker(&tagMutex);
        tagManager.addTag(fp, t, true);
        tagManager.saveTags();
    }
    updateTagDisplayForFile(fp);
    updateTagList();
    reloadCurrentFileListPanel();
}

void MainWindow::removeTag() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }

    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getTags(fp);
    }
    if (tags.empty()) {
        QMessageBox::information(this, QStringLiteral("Info"), QStringLiteral("無標籤"));
        return;
    }

    QStringList items;
    for (const auto &t : tags) items << t;

    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("Remove"), QStringLiteral("選擇要移除的標籤:"), items, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    {
        QMutexLocker locker(&tagMutex);
        tagManager.removeTag(fp, chosen);
        tagManager.addRejectedTag(chosen);
        tagManager.saveTags();
    }
    updateTagDisplayForFile(fp);
    updateTagList();
    reloadCurrentFileListPanel();
}

void MainWindow::removeGlobalTag() {
    QString data;
    if (m_tagTabWidget && m_tagTabWidget->currentIndex() == 1) {
        if (!m_aiTagTreeWidget) return;
        const QList<QTreeWidgetItem *> sel = m_aiTagTreeWidget->selectedItems();
        if (sel.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇標籤"));
            return;
        }
        data = sel.first()->data(0, Qt::UserRole).toString();
    } else {
        if (!m_systemTagListWidget) return;
        const QList<QListWidgetItem *> sel = m_systemTagListWidget->selectedItems();
        if (sel.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇標籤"));
            return;
        }
        data = sel.first()->data(Qt::UserRole).toString();
    }

    if (data == QStringLiteral("ALL")) {
        QMessageBox::warning(this, tr("Warning"), tr("Cannot delete All Files"));
        return;
    }
    if (data.startsWith(QStringLiteral("SF_DRAWER:"))) {
        QMessageBox::information(this, QStringLiteral("Smartflie"), QStringLiteral("請選擇具體的 AI 標籤，而非分類資料夾。"));
        return;
    }
    const QString tag = normalizeDisplayTag(data);
    auto &lm = LanguageManager::instance();

    if (m_tagTabWidget && m_tagTabWidget->currentIndex() == 1 && TagManager::hasAiPrefix(tag)) {
        std::vector<QString> ch;
        {
            QMutexLocker locker(&tagMutex);
            ch = tagManager.directChildTags(tag);
        }
        if (!ch.empty()) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(lm.getText(QStringLiteral("刪除（全域）")));
            box.setText(lm.getText(QStringLiteral("tag_delete_parent_has_children")).arg(tag));
            QPushButton *bDissolve =
                box.addButton(lm.getText(QStringLiteral("tag_delete_dissolve")), QMessageBox::AcceptRole);
            QPushButton *bCascade =
                box.addButton(lm.getText(QStringLiteral("tag_delete_cascade")), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == bDissolve) {
                QMutexLocker locker(&tagMutex);
                tagManager.deleteTagDissolveChildren(tag, true);
                tagManager.addRejectedTag(tag);
            } else if (box.clickedButton() == bCascade) {
                QMutexLocker locker(&tagMutex);
                tagManager.deleteTagCascadeAi(tag, true);
                tagManager.addRejectedTag(tag);
            } else {
                return;
            }
        } else {
            const auto reply = QMessageBox::question(this, QStringLiteral("Delete"),
                                                     QStringLiteral("確定刪除標籤「%1」？").arg(tag),
                                                     QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
            {
                QMutexLocker locker(&tagMutex);
                tagManager.deleteTag(tag);
                tagManager.addRejectedTag(tag);
                tagManager.saveTags();
            }
        }
    } else {
        const auto reply = QMessageBox::question(this, QStringLiteral("Delete"),
                                                 QStringLiteral("確定刪除標籤「%1」？").arg(tag),
                                                 QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        {
            QMutexLocker locker(&tagMutex);
            tagManager.deleteTag(tag);
            tagManager.addRejectedTag(tag);
            tagManager.saveTags();
        }
    }

    if (TagManager::hasAiPrefix(tag)) {
        m_aiTagToDrawerKey.remove(tag);
        saveAiUiDrawerAssignments();
    }

    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    updateTagList();
    reloadCurrentFileListPanel();
}

void MainWindow::rebuildAddExistingTagMenu() {
    auto *menu = new QMenu(this);

    std::vector<QString> allTags;
    {
        QMutexLocker locker(&tagMutex);
        allTags = tagManager.getAllTags();
    }
    QStringList history;
    for (const auto &t : allTags) history << normalizeDisplayTag(t);

    auto addCategory = [&](const QString &name, const QStringList &preset) {
        QMenu *sub = menu->addMenu(LanguageManager::instance().getText(name));
        for (const QString &t : preset) {
            const QString canon = normalizeDisplayTag(t);
            const QString baseZh = systemTagBaseZh(canon).isEmpty() ? canon : systemTagBaseZh(canon);
            const QString emoji = systemTagEmojiPrefix(canon);
            const QString display = (LanguageManager::instance().getText(baseZh) == baseZh)
                                        ? canon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            QAction *a = sub->addAction(display.trimmed());
            a->setData(canon);
            connect(a, &QAction::triggered, this, [this, a]() {
                const QString fp = currentFilePath();
                if (fp.isEmpty()) return;
                const QString tag = a->data().toString();
                {
                    QMutexLocker locker(&tagMutex);
                    tagManager.addTag(fp, tag, true);
                    tagManager.saveTags();
                }
                updateTagDisplayForFile(fp);
                updateTagList();
                reloadCurrentFileListPanel();
            });
        }
        if (!history.isEmpty()) {
            sub->addSeparator();
            for (const QString &t : history) {
                const QString canon = normalizeDisplayTag(t);
                const QString baseZh = systemTagBaseZh(canon).isEmpty() ? canon : systemTagBaseZh(canon);
                const QString emoji = systemTagEmojiPrefix(canon);
                QString display = (LanguageManager::instance().getText(baseZh) == baseZh)
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
                display = display.trimmed();
                if (TagManager::hasAiPrefix(canon)) display = tagLibraryLabelStripAiBadge(display);
                QAction *a = sub->addAction(display);
                a->setData(canon);
                connect(a, &QAction::triggered, this, [this, a]() {
                    const QString fp = currentFilePath();
                    if (fp.isEmpty()) return;
                    const QString tag = a->data().toString();
                    {
                        QMutexLocker locker(&tagMutex);
                        tagManager.addTag(fp, tag, true);
                        tagManager.saveTags();
                    }
                    updateTagDisplayForFile(fp);
                    updateTagList();
                    reloadCurrentFileListPanel();
                });
            }
        }
    };

    addCategory(QStringLiteral("🖼️ 圖片"), {QStringLiteral("相片"), QStringLiteral("截圖")});
    addCategory(QStringLiteral("🎬 影片"), {QStringLiteral("剪輯"), QStringLiteral("錄影")});
    addCategory(QStringLiteral("🎧 音訊"), {QStringLiteral("音樂"), QStringLiteral("錄音")});
    addCategory(QStringLiteral("📄 文件"), {QStringLiteral("報告"), QStringLiteral("簡報")});
    addCategory(QStringLiteral("📦 壓縮檔"), {QStringLiteral("備份"), QStringLiteral("打包")});
    addCategory(QStringLiteral("🧩 專案"), {QStringLiteral("程式碼"), QStringLiteral("研究")});

    btnAddExistingTag->setMenu(menu);
}

QStringList MainWindow::getFastPathTags(const QString &filename) {
    QStringList tags;
    const QString lower = filename.toLower();

    if (lower.contains(QStringLiteral("hw")) || lower.contains(QStringLiteral("homework")) || lower.contains(QStringLiteral("作業")) || lower.contains(QStringLiteral("報告")))
        tags << QStringLiteral("🎒學校作業");
    if (lower.contains(QStringLiteral("receipt")) || lower.contains(QStringLiteral("invoice")) || lower.contains(QStringLiteral("收據")) || lower.contains(QStringLiteral("發票")))
        tags << QStringLiteral("💰財務");
    if (lower.contains(QStringLiteral("setup")) || lower.contains(QStringLiteral("install")) || lower.contains(QStringLiteral("installer")) || lower.contains(QStringLiteral("安裝")))
        tags << QStringLiteral("💻安裝檔");
    if (lower.contains(QStringLiteral("backup")) || lower.contains(QStringLiteral("備份"))) tags << QStringLiteral("📦備份檔");
    if (lower.contains(QStringLiteral("meeting")) || lower.contains(QStringLiteral("會議"))) tags << QStringLiteral("🗓️會議");
    if (lower.contains(QStringLiteral("resume")) || lower.contains(QStringLiteral("cv")) || lower.contains(QStringLiteral("履歷"))) tags << QStringLiteral("🧑‍💼履歷");

    if (lower.endsWith(QStringLiteral(".exe")) || lower.endsWith(QStringLiteral(".dmg")) || lower.endsWith(QStringLiteral(".pkg")) || lower.endsWith(QStringLiteral(".msi")))
        tags << QStringLiteral("💻應用程式");
    if (lower.endsWith(QStringLiteral(".cpp")) || lower.endsWith(QStringLiteral(".h")) || lower.endsWith(QStringLiteral(".hpp")) || lower.endsWith(QStringLiteral(".c")) || lower.endsWith(QStringLiteral(".rs")) || lower.endsWith(QStringLiteral(".go")) || lower.endsWith(QStringLiteral(".py")) || lower.endsWith(QStringLiteral(".js")) || lower.endsWith(QStringLiteral(".ts")) || lower.endsWith(QStringLiteral(".java")) || lower.endsWith(QStringLiteral(".cs")))
        tags << QStringLiteral("⌨️程式碼");
    if (lower.endsWith(QStringLiteral(".pdf")) || lower.endsWith(QStringLiteral(".docx")) || lower.endsWith(QStringLiteral(".docm"))
        || lower.endsWith(QStringLiteral(".dotx")) || lower.endsWith(QStringLiteral(".dotm")) || lower.endsWith(QStringLiteral(".xlsx"))
        || lower.endsWith(QStringLiteral(".xlsm")) || lower.endsWith(QStringLiteral(".xltx")) || lower.endsWith(QStringLiteral(".xltm"))
        || lower.endsWith(QStringLiteral(".pptx")) || lower.endsWith(QStringLiteral(".pptm")) || lower.endsWith(QStringLiteral(".potx"))
        || lower.endsWith(QStringLiteral(".potm")) || lower.endsWith(QStringLiteral(".odt")) || lower.endsWith(QStringLiteral(".ods"))
        || lower.endsWith(QStringLiteral(".odp")) || lower.endsWith(QStringLiteral(".epub")) || lower.endsWith(QStringLiteral(".txt"))
        || lower.endsWith(QStringLiteral(".md")) || lower.endsWith(QStringLiteral(".rtf")))
        tags << kTagDoc;
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg")) || lower.endsWith(QStringLiteral(".png")) || lower.endsWith(QStringLiteral(".gif")) || lower.endsWith(QStringLiteral(".webp")) || lower.endsWith(QStringLiteral(".heic")) || lower.endsWith(QStringLiteral(".bmp")))
        tags << kTagImage;
    if (lower.endsWith(QStringLiteral(".mp4")) || lower.endsWith(QStringLiteral(".mov")) || lower.endsWith(QStringLiteral(".mkv")) || lower.endsWith(QStringLiteral(".avi")) || lower.endsWith(QStringLiteral(".webm")))
        tags << kTagVideo;
    if (lower.endsWith(QStringLiteral(".mp3")) || lower.endsWith(QStringLiteral(".wav")) || lower.endsWith(QStringLiteral(".m4a")) || lower.endsWith(QStringLiteral(".flac")) || lower.endsWith(QStringLiteral(".aac")))
        tags << kTagAudio;
    if (lower.endsWith(QStringLiteral(".zip")) || lower.endsWith(QStringLiteral(".rar")) || lower.endsWith(QStringLiteral(".7z")) || lower.endsWith(QStringLiteral(".tar")) || lower.endsWith(QStringLiteral(".gz")))
        tags << QStringLiteral("📦壓縮檔");
    if (lower.endsWith(QStringLiteral(".json")) || lower.endsWith(QStringLiteral(".xml")) || lower.endsWith(QStringLiteral(".yaml")) || lower.endsWith(QStringLiteral(".yml")) || lower.endsWith(QStringLiteral(".toml")) || lower.endsWith(QStringLiteral(".ini")))
        tags << QStringLiteral("🧩設定");
    if (lower.endsWith(QStringLiteral(".blend")) || lower.endsWith(QStringLiteral(".psd")) || lower.endsWith(QStringLiteral(".ai"))) tags << QStringLiteral("🎨設計");
    if (lower.endsWith(QStringLiteral(".sqlite")) || lower.endsWith(QStringLiteral(".db"))) tags << QStringLiteral("🗄️資料庫");

    tags.removeDuplicates();
    return tags;
}
