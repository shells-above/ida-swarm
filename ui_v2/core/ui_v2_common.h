#pragma once

// This file must be included FIRST in all ui_v2 files
// It handles the proper include order: IDA -> std -> Qt

// IDA headers (must come first)
#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <bytes.hpp>
#include <name.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <lines.hpp>
#include <segment.hpp>
#include <search.hpp>
#include <xref.hpp>
#include <nalt.hpp>
#include <entry.hpp>
#include <auto.hpp>
#include <strlist.hpp>
#include <diskio.hpp>


// Undefine problematic macros from IDA
#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef wait
#undef fgetc
#undef snprintf

// std lib
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Qt headers
// Qt Core
#include <QtGlobal>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QTimer>
#include <QMetaProperty>
#include <QPointer>
#include <QList>
#include <QHash>
#include <QHashFunctions>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QUndoStack>
#include <QDebug>
#include <QRandomGenerator>
#include <QFile>
#include <QTextStream>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <QSystemTrayIcon>

// Qt GUI
#include <QColor>
#include <QFont>
#include <QMargins>
#include <QRect>
#include <QPoint>
#include <QPointF>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPixmap>
#include <QPixmapCache>
#include <QScreen>
#include <QDesktopWidget>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QTextCharFormat>

// Qt Widgets
#include <QWidget>
#include <QMainWindow>
#include <QDockWidget>
#include <QDialog>
#include <QAbstractScrollArea>
#include <QScrollArea>
#include <QScrollBar>
#include <QLineEdit>
#include <QTextEdit>
#include <QTextBrowser>
#include <QLabel>
#include <QFontComboBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QProgressBar>
#include <QListView>
#include <QListWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QTableView>
#include <QTableWidget>
#include <QAbstractItemModel>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QToolTip>
#include <QShortcut>
#include <QDateTimeEdit>

// Qt Layouts
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>

// Qt Graphics
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGraphicsBlurEffect>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>

// Qt Animation
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>

// Qt Events
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QContextMenuEvent>

// Qt Text
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextList>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextDocumentWriter>
#include <QSyntaxHighlighter>

// Qt Network
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// Qt Math
#include <QtMath>

// Re-define macros to prevent usage
#define fopen dont_use_fopen
#define fclose dont_use_fclose
#define fread dont_use_fread
#define fwrite dont_use_fwrite
#define fgetc dont_use_fgetc
#define snprintf dont_use_snprintf

// Hash function for QUuid to use in std::unordered_map
struct QUuidHash {
    std::size_t operator()(const QUuid& uuid) const {
        return qHash(uuid);
    }
};

// Also provide std::hash specialization for Qt namespace
namespace std {
    template<>
    struct hash<QT::QUuid> {
        std::size_t operator()(const QT::QUuid& uuid) const {
            return qHash(uuid);
        }
    };
}