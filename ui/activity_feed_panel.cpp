#include "activity_feed_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QListWidgetItem>
#include <QFont>
#include <QResizeEvent>
#include <algorithm>

namespace llm_re::ui {

// ========================================================================
// ActivityFeedPanel Implementation
// ========================================================================

ActivityFeedPanel::ActivityFeedPanel(QWidget* parent)
    : QWidget(parent)
    , next_color_index_(0) {

    // Initialize color palette for agents
    color_palette_ = {
        QColor(52, 152, 219),   // Blue
        QColor(46, 204, 113),   // Green
        QColor(155, 89, 182),   // Purple
        QColor(241, 196, 15),   // Yellow
        QColor(231, 76, 60),    // Red
        QColor(26, 188, 156),   // Turquoise
        QColor(230, 126, 34),   // Orange
        QColor(149, 165, 166),  // Gray
        QColor(52, 73, 94),     // Dark blue
        QColor(192, 57, 43)     // Dark red
    };

    setup_ui();
    setup_connections();

    // Auto-scroll timer
    auto_scroll_timer_ = new QTimer(this);
    auto_scroll_timer_->setInterval(100);
    connect(auto_scroll_timer_, &QTimer::timeout,
            this, &ActivityFeedPanel::ensure_latest_visible);
}

void ActivityFeedPanel::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);

    // Create vertical splitter
    main_splitter_ = new QSplitter(Qt::Vertical, this);

    // === Discovery Feed (Top - Full Width) ===
    discovery_container_ = new QWidget(this);
    auto* discovery_layout = new QVBoxLayout(discovery_container_);

    // Header with clear button
    auto* discovery_header_layout = new QHBoxLayout();
    discovery_header_ = new QLabel("Discoveries", this);
    QFont header_font = discovery_header_->font();
    header_font.setBold(true);
    header_font.setPointSize(header_font.pointSize() + 2);
    discovery_header_->setFont(header_font);

    clear_discovery_button_ = new QPushButton("Clear", this);
    clear_discovery_button_->setMaximumWidth(60);

    discovery_header_layout->addWidget(discovery_header_);
    discovery_header_layout->addStretch();
    discovery_header_layout->addWidget(clear_discovery_button_);

    // Discovery list - full width, prominent display
    discovery_list_ = new QListWidget(this);
    discovery_list_->setAlternatingRowColors(true);
    discovery_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    discovery_list_->setResizeMode(QListView::Adjust);
    discovery_list_->setStyleSheet(
        "QListWidget { background-color: #2b2b2b; border: 1px solid #444; }"
        "QListWidget::item { background-color: transparent; border: none; }"
        "QListWidget::item:hover { background-color: #3a3a3a; }"
    );

    discovery_layout->addLayout(discovery_header_layout);
    discovery_layout->addWidget(discovery_list_);

    // === Status Ticker (Bottom) ===
    status_container_ = new QWidget(this);
    auto* status_layout = new QVBoxLayout(status_container_);
    status_layout->setSpacing(2);
    status_layout->setContentsMargins(5, 5, 5, 5);

    // Header
    status_header_ = new QLabel("Agent Status", this);
    QFont status_font = status_header_->font();
    status_font.setBold(true);
    status_header_->setFont(status_font);

    // Status list
    status_list_ = new QListWidget(this);
    status_list_->setFlow(QListView::TopToBottom);
    status_list_->setWrapping(false);
    status_list_->setResizeMode(QListView::Adjust);
    status_list_->setSelectionMode(QAbstractItemView::NoSelection);
    status_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    status_list_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    status_list_->setStyleSheet(
        "QListWidget { background-color: #1a1a1a; border: 1px solid #333; }"
        "QListWidget::item { background: transparent; padding: 0px; margin: 0px; }"
    );

    status_layout->addWidget(status_header_);
    status_layout->addWidget(status_list_);

    // Add to splitter with initial 80/20 split
    main_splitter_->addWidget(discovery_container_);
    main_splitter_->addWidget(status_container_);

    // Set initial sizes (80/20 split) but allow user to resize
    QList<int> sizes;
    sizes << 400 << 100;  // Initial pixel sizes (will be proportional)
    main_splitter_->setSizes(sizes);

    // Set minimum sizes so neither can be completely collapsed
    discovery_container_->setMinimumHeight(100);
    status_container_->setMinimumHeight(50);

    // Make the splitter handle more visible
    main_splitter_->setHandleWidth(5);
    main_splitter_->setStyleSheet(
        "QSplitter::handle { background-color: #555; }"
        "QSplitter::handle:hover { background-color: #777; }"
    );

    main_layout->addWidget(main_splitter_);
}

void ActivityFeedPanel::setup_connections() {
    connect(clear_discovery_button_, &QPushButton::clicked,
            this, &ActivityFeedPanel::clear_discovery_feed);
}

void ActivityFeedPanel::add_status_update(const std::string& agent_id,
                                          const std::string& status_text,
                                          const std::string& emoji) {
    // Check if we should update existing item
    auto it = agent_status_items_.find(agent_id);
    if (it != agent_status_items_.end()) {
        // Update existing status
        update_agent_status(agent_id, status_text, emoji);
        return;
    }

    // Create new status item
    auto* item_widget = new StatusFeedItem(
        agent_id,
        status_text,
        emoji,
        get_agent_color(agent_id),
        this
    );

    // Extract numeric part for sorting
    int sort_value = 999999;  // Default for unknown agents
    if (agent_id.starts_with("agent_")) {
        try {
            sort_value = std::stoi(agent_id.substr(6));  // Extract number after "agent_"
        } catch (...) {
            // Keep default sort value
        }
    }

    // Find insertion position based on numeric sort
    int insert_row = 0;
    for (int i = 0; i < status_list_->count(); ++i) {
        auto* widget = dynamic_cast<StatusFeedItem*>(status_list_->itemWidget(status_list_->item(i)));
        if (widget) {
            std::string other_id = widget->get_agent_id();
            int other_sort_value = 999999;
            if (other_id.starts_with("agent_")) {
                try {
                    other_sort_value = std::stoi(other_id.substr(6));
                } catch (...) {}
            }

            if (sort_value < other_sort_value) {
                insert_row = i;
                break;
            }
            insert_row = i + 1;
        }
    }

    auto* list_item = new QListWidgetItem();
    // Add small padding to prevent text cutoff
    QSize hint = item_widget->sizeHint();
    hint.setHeight(hint.height() + 4);  // Just 4 pixels to prevent cutoff
    // Make width stretch to full list width
    hint.setWidth(status_list_->viewport()->width() - 4);
    list_item->setSizeHint(hint);

    status_list_->insertItem(insert_row, list_item);
    status_list_->setItemWidget(list_item, item_widget);

    // Track the item for updates
    agent_status_items_[agent_id] = list_item;

    // Auto-scroll to latest
    auto_scroll_timer_->start();
}

void ActivityFeedPanel::update_agent_status(const std::string& agent_id,
                                            const std::string& status_text,
                                            const std::string& emoji) {
    auto it = agent_status_items_.find(agent_id);
    if (it != agent_status_items_.end()) {
        auto* widget = dynamic_cast<StatusFeedItem*>(
            status_list_->itemWidget(it->second)
        );
        if (widget) {
            widget->update_status(status_text, emoji);
        }
    } else {
        // If not found, add as new
        add_status_update(agent_id, status_text, emoji);
    }
}

void ActivityFeedPanel::add_discovery(const std::string& agent_id,
                                      const std::string& discovery_type,
                                      const std::string& description,
                                      const std::string& emoji,
                                      const std::string& location,
                                      int importance_level) {
    auto* item_widget = new DiscoveryFeedItem(
        agent_id,
        discovery_type,
        description,
        emoji,
        location,
        importance_level,
        get_agent_color(agent_id),
        this
    );

    auto* list_item = new QListWidgetItem(discovery_list_);
    discovery_list_->addItem(list_item);
    discovery_list_->setItemWidget(list_item, item_widget);

    // Let Qt calculate the proper size
    list_item->setSizeHint(item_widget->sizeHint());

    // Auto-scroll to latest
    auto_scroll_timer_->start();
}

void ActivityFeedPanel::clear_status_feed() {
    status_list_->clear();
    agent_status_items_.clear();
}

void ActivityFeedPanel::clear_discovery_feed() {
    discovery_list_->clear();
}

void ActivityFeedPanel::clear_all_feeds() {
    clear_status_feed();
    clear_discovery_feed();
}

QColor ActivityFeedPanel::get_agent_color(const std::string& agent_id) {
    auto it = agent_colors_.find(agent_id);
    if (it != agent_colors_.end()) {
        return it->second;
    }

    // Assign new color
    QColor color = color_palette_[next_color_index_ % color_palette_.size()];
    next_color_index_++;
    agent_colors_[agent_id] = color;
    return color;
}

QString ActivityFeedPanel::format_timestamp(const QDateTime& time) {
    return time.toString("hh:mm:ss");
}

void ActivityFeedPanel::ensure_latest_visible() {
    // Auto-scroll status list
    if (status_list_->count() > 0) {
        status_list_->scrollToBottom();
    }

    // Auto-scroll discovery list
    if (discovery_list_->count() > 0) {
        discovery_list_->scrollToBottom();
    }

    // Stop timer after scrolling
    auto_scroll_timer_->stop();
}

// ========================================================================
// StatusFeedItem Implementation
// ========================================================================

StatusFeedItem::StatusFeedItem(const std::string& agent_id,
                               const std::string& status_text,
                               const std::string& emoji,
                               const QColor& agent_color,
                               QWidget* parent)
    : QWidget(parent)
    , agent_id_(agent_id) {
    setup_ui(agent_color, status_text, emoji);
}

void StatusFeedItem::setup_ui(const QColor& agent_color,
                               const std::string& status_text,
                               const std::string& emoji) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // Emoji
    emoji_label_ = new QLabel(this);
    emoji_label_->setText(QString::fromStdString(emoji));
    emoji_label_->setFixedWidth(20);

    // Agent name (bold) with colon
    agent_label_ = new QLabel(QString::fromStdString(agent_id_) + ":", this);
    agent_label_->setStyleSheet("QLabel { font-weight: bold; }");
    agent_label_->setFixedWidth(70);

    // Status text - uses custom ElidingLabel
    status_label_ = new ElidingLabel(this);
    status_label_->setText(QString::fromStdString(status_text));

    // Hidden timestamp (for update tracking)
    time_label_ = new QLabel(QDateTime::currentDateTime().toString("hh:mm:ss"), this);
    time_label_->hide();

    layout->addWidget(emoji_label_);
    layout->addWidget(agent_label_);
    layout->addWidget(status_label_, 1);  // Stretch factor for status
}

void StatusFeedItem::update_status(const std::string& new_status,
                                   const std::string& emoji) {
    status_label_->setText(QString::fromStdString(new_status));
    emoji_label_->setText(QString::fromStdString(emoji));
    time_label_->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
}

// ========================================================================
// DiscoveryFeedItem Implementation
// ========================================================================

DiscoveryFeedItem::DiscoveryFeedItem(const std::string& agent_id,
                                     const std::string& discovery_type,
                                     const std::string& description,
                                     const std::string& emoji,
                                     const std::string& location,
                                     int importance_level,
                                     const QColor& agent_color,
                                     QWidget* parent)
    : QWidget(parent)
    , agent_id_(agent_id)
    , location_(location) {
    setup_ui(discovery_type, description, emoji, location, importance_level, agent_color);
}

void DiscoveryFeedItem::setup_ui(const std::string& discovery_type,
                                 const std::string& description,
                                 const std::string& emoji,
                                 const std::string& location,
                                 int importance_level,
                                 const QColor& agent_color) {
    auto* main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(8, 6, 8, 6);
    main_layout->setSpacing(10);

    // Emoji on the left
    icon_label_ = new QLabel(QString::fromStdString(emoji), this);
    icon_label_->setFixedWidth(24);
    icon_label_->setAlignment(Qt::AlignTop);
    QFont emoji_font = icon_label_->font();
    emoji_font.setPointSize(emoji_font.pointSize() + 1);
    icon_label_->setFont(emoji_font);

    // Content layout (agent, description, location)
    auto* content_layout = new QVBoxLayout();
    content_layout->setSpacing(3);

    // Agent, location, and timestamp on same line
    auto* header_layout = new QHBoxLayout();

    // Agent name
    agent_label_ = new QLabel(QString::fromStdString(agent_id_), this);
    agent_label_->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; }");

    // Location right after agent name if provided
    if (!location.empty()) {
        location_label_ = new QLabel(QString("@ %1").arg(QString::fromStdString(location)), this);
        location_label_->setStyleSheet("QLabel { color: #888; font-family: monospace; font-size: 11px; }");
    }

    time_label_ = new QLabel(QDateTime::currentDateTime().toString("hh:mm:ss"), this);
    time_label_->setStyleSheet("QLabel { color: #888; font-size: 10px; }");

    header_layout->addWidget(agent_label_);
    if (!location.empty()) {
        header_layout->addWidget(location_label_);
    }
    header_layout->addStretch();
    header_layout->addWidget(time_label_);

    // Description - compact
    description_label_ = new QLabel(QString::fromStdString(description), this);
    description_label_->setWordWrap(true);
    description_label_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 13px; }")
        .arg(get_importance_color(importance_level).name())
    );

    content_layout->addLayout(header_layout);
    content_layout->addWidget(description_label_);

    main_layout->addWidget(icon_label_);
    main_layout->addLayout(content_layout, 1);
}


QColor DiscoveryFeedItem::get_importance_color(int level) {
    switch(level) {
        case 3: return QColor(231, 76, 60);    // High - Red
        case 2: return QColor(241, 196, 15);   // Medium - Yellow
        case 1: return QColor(46, 204, 113);   // Low - Green
        default: return QColor(189, 195, 199); // Default - Gray
    }
}

} // namespace llm_re::ui