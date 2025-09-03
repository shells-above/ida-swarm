#pragma once

// CRITICAL: Include order matters for IDA Qt plugins!
// 1. Standard library headers
// 2. IDA SDK headers (which may define macros like 'emit')
// 3. Qt headers (which also define 'emit' as a macro)

// Step 1: Standard library headers
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <functional>
#include <algorithm>

// Step 2: IDA SDK headers - these MUST come before Qt
#include <pro.h>
#include <kernwin.hpp>

// Save Qt's emit macro if it's already defined
#ifdef emit
#define QT_EMIT_WAS_DEFINED
#pragma push_macro("emit")
#undef emit
#endif

// Include any headers that might conflict with Qt
#include "../agent/event_bus.h"
#include "../orchestrator/orchestrator.h"

// Step 3: Restore Qt's emit macro before including Qt headers
#ifdef QT_EMIT_WAS_DEFINED
#pragma pop_macro("emit")
#undef QT_EMIT_WAS_DEFINED
#endif

// Now include Qt headers - emit macro is properly restored
#include <QMainWindow>
#include <QWidget>
#include <QObject>
#include <QString>

// Forward declarations for Qt classes to minimize includes
QT_BEGIN_NAMESPACE
class QTextEdit;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QListWidget;
class QLabel;
class QTabWidget;
class QPlainTextEdit;
class QTreeWidget;
class QProgressBar;
class QComboBox;
class QGroupBox;
class QSplitter;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QMenu;
class QMenuBar;
class QAction;
class QStatusBar;
class QDateTime;
class QScrollBar;
class QTimer;
QT_END_NAMESPACE