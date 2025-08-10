#pragma once

// This file must be included FIRST in all ui_v2 files
// It handles the proper include order: IDA -> std -> Qt

// Include base common header (IDA SDK without kernwin, std lib, json)
#include "../../core/common_base.h"
#include "../../api/api_common.h"  // For LogLevel enum

// Qt headers
#include <QtGlobal>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QTimer>
#include <QThread>
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
#include <QColor>
#include <QFont>
#include <QMargins>
#include <QRect>
#include <QPoint>
#include <QPointF>
#include <QGraphicsScale>
#include <QGraphicsRotation>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QDir>
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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGraphicsBlurEffect>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QContextMenuEvent>
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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtMath>


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

// NOW include kernwin.hpp after all Qt headers are already included
// This way it won't conflict with Qt includes
#include <kernwin.hpp>

// Re-define macros to prevent usage after kernwin.hpp
#define fopen dont_use_fopen
#define fclose dont_use_fclose
#define fread dont_use_fread
#define fwrite dont_use_fwrite
#define fgetc dont_use_fgetc
#define snprintf dont_use_snprintf