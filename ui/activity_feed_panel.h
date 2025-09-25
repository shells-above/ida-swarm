#pragma once

#include "ui_common.h"
#include <QWidget>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDateTime>
#include <QTimer>
#include <QPainter>
#include <map>
#include <vector>

namespace llm_re::ui {

// Custom label that automatically elides text
class ElidingLabel : public QWidget {
    Q_OBJECT

public:
    explicit ElidingLabel(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setText(const QString& text) {
        full_text_ = text;
        update();  // Trigger repaint
    }

    QString text() const { return full_text_; }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setPen(QColor("#ddd"));

        QFontMetrics metrics(font());
        // Use full width of the widget
        QString elided = metrics.elidedText(full_text_, Qt::ElideRight, width());
        // Draw text using the full rect
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    QSize sizeHint() const override {
        QFontMetrics metrics(font());
        return QSize(metrics.horizontalAdvance(full_text_), metrics.height());
    }

    QSize minimumSizeHint() const override {
        return QSize(20, QFontMetrics(font()).height());
    }

private:
    QString full_text_;
};

// Forward declarations
class StatusFeedItem;
class DiscoveryFeedItem;

// Main activity feed panel containing status and discovery feeds
class ActivityFeedPanel : public QWidget {
    Q_OBJECT

public:
    explicit ActivityFeedPanel(QWidget* parent = nullptr);
    ~ActivityFeedPanel() = default;

    // Add status update from agent
    void add_status_update(const std::string& agent_id,
                          const std::string& status_text,
                          const std::string& emoji);

    // Add discovery from agent
    void add_discovery(const std::string& agent_id,
                       const std::string& discovery_type,
                       const std::string& description,
                       const std::string& emoji,
                       const std::string& location = "",
                       int importance_level = 1);

    // Clear feeds
    void clear_status_feed();
    void clear_discovery_feed();
    void clear_all_feeds();

    // Update agent status (replace existing if present)
    void update_agent_status(const std::string& agent_id,
                            const std::string& status_text,
                            const std::string& emoji);

signals:
    void status_item_clicked(const std::string& agent_id);
    void discovery_item_clicked(const std::string& agent_id, const std::string& location);

private:
    // Layout components
    QSplitter* main_splitter_;  // Now vertical splitter

    // Status feed (left side)
    QWidget* status_container_;
    QListWidget* status_list_;
    QLabel* status_header_;
    QPushButton* clear_status_button_;

    // Discovery feed (right side)
    QWidget* discovery_container_;
    QListWidget* discovery_list_;
    QLabel* discovery_header_;
    QPushButton* clear_discovery_button_;

    // Agent color mapping
    std::map<std::string, QColor> agent_colors_;
    std::vector<QColor> color_palette_;
    int next_color_index_ = 0;

    // Track current agent statuses (for updates)
    std::map<std::string, QListWidgetItem*> agent_status_items_;

    // Helper methods
    QColor get_agent_color(const std::string& agent_id);
    QString format_timestamp(const QDateTime& time);
    void setup_ui();
    void setup_connections();

    // Auto-scroll timer
    QTimer* auto_scroll_timer_;
    void ensure_latest_visible();
};

// Custom widget for status feed items
class StatusFeedItem : public QWidget {
    Q_OBJECT

public:
    StatusFeedItem(const std::string& agent_id,
                   const std::string& status_text,
                   const std::string& emoji,
                   const QColor& agent_color,
                   QWidget* parent = nullptr);

    void update_status(const std::string& new_status, const std::string& emoji);
    const std::string& get_agent_id() const { return agent_id_; }

private:
    std::string agent_id_;
    QLabel* emoji_label_;
    QLabel* agent_label_;
    ElidingLabel* status_label_;  // Changed to ElidingLabel
    QLabel* time_label_;

    void setup_ui(const QColor& agent_color,
                  const std::string& status_text,
                  const std::string& emoji);
};

// Custom widget for discovery feed items
class DiscoveryFeedItem : public QWidget {
    Q_OBJECT

public:
    DiscoveryFeedItem(const std::string& agent_id,
                      const std::string& discovery_type,
                      const std::string& description,
                      const std::string& emoji,
                      const std::string& location,
                      int importance_level,
                      const QColor& agent_color,
                      QWidget* parent = nullptr);

private:
    std::string agent_id_;
    std::string location_;
    QLabel* icon_label_;
    QLabel* agent_label_;
    QLabel* description_label_;
    QLabel* location_label_;
    QLabel* time_label_;

    void setup_ui(const std::string& discovery_type,
                  const std::string& description,
                  const std::string& emoji,
                  const std::string& location,
                  int importance_level,
                  const QColor& agent_color);
    QColor get_importance_color(int level);
};

} // namespace llm_re::ui