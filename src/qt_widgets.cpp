//
// Created by user on 6/30/25.
//

#include "qt_widgets.h"

#include "main_form.h"


namespace llm_re {
    // Config implementation
    bool Config::save_to_file(const std::string& path) const {
        try {
            json j;

            // API settings
            j["api"]["api_key"] = api.api_key;
            j["api"]["base_url"] = api.base_url;
            j["api"]["model"] = api::model_to_string(api.model);
            j["api"]["max_tokens"] = api.max_tokens;
            j["api"]["max_thinking_tokens"] = api.max_thinking_tokens;
            j["api"]["temperature"] = api.temperature;

            // Agent settings
            j["agent"]["max_iterations"] = agent.max_iterations;
            j["agent"]["enable_thinking"] = agent.enable_thinking;
            j["agent"]["enable_interleaved_thinking"] = agent.enable_interleaved_thinking;
            j["agent"]["enable_deep_analysis"] = agent.enable_deep_analysis;
            j["agent"]["verbose_logging"] = agent.verbose_logging;

            // UI settings
            j["ui"]["log_buffer_size"] = ui.log_buffer_size;
            j["ui"]["auto_scroll"] = ui.auto_scroll;
            j["ui"]["theme"] = ui.theme;
            j["ui"]["font_size"] = ui.font_size;
            j["ui"]["show_timestamps"] = ui.show_timestamps;
            j["ui"]["show_tool_details"] = ui.show_tool_details;

            // Export settings
            j["export"]["path"] = export_settings.path;

            j["debug_mode"] = debug_mode;

            std::ofstream file(path);
            file << j.dump(2);

            return true;
        } catch (...) {
            return false;
        }
    }

    bool Config::load_from_file(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file) return false;

            json j;
            file >> j;

            // API settings
            if (j.contains("api")) {
                api.api_key = j["api"].value("api_key", api.api_key);
                api.base_url = j["api"].value("base_url", api.base_url);
                if (j["api"].contains("model")) {
                    api.model = api::model_from_string(j["api"]["model"]);
                }
                api.max_tokens = j["api"].value("max_tokens", api.max_tokens);
                api.max_thinking_tokens = j["api"].value("max_thinking_tokens", api.max_thinking_tokens);
                api.temperature = j["api"].value("temperature", api.temperature);
            }

            // Agent settings
            if (j.contains("agent")) {
                agent.max_iterations = j["agent"].value("max_iterations", agent.max_iterations);
                agent.enable_thinking = j["agent"].value("enable_thinking", agent.enable_thinking);
                agent.enable_interleaved_thinking = j["agent"].value("enable_interleaved_thinking", agent.enable_interleaved_thinking);
                agent.enable_deep_analysis = j["agent"].value("enable_deep_analysis", agent.enable_deep_analysis);
                agent.verbose_logging = j["agent"].value("verbose_logging", agent.verbose_logging);
            }

            // UI settings
            if (j.contains("ui")) {
                ui.log_buffer_size = j["ui"].value("log_buffer_size", ui.log_buffer_size);
                ui.auto_scroll = j["ui"].value("auto_scroll", ui.auto_scroll);
                ui.theme = j["ui"].value("theme", ui.theme);
                ui.font_size = j["ui"].value("font_size", ui.font_size);
                ui.show_timestamps = j["ui"].value("show_timestamps", ui.show_timestamps);
                ui.show_tool_details = j["ui"].value("show_tool_details", ui.show_tool_details);
            }

            // Export settings
            if (j.contains("export")) {
                export_settings.path = j["export"].value("path", export_settings.path);
            }

            debug_mode = j.value("debug_mode", debug_mode);

            return true;
        } catch (...) {
            return false;
        }
    }
}


namespace llm_re::ui {

CollapsibleMessageWidget::CollapsibleMessageWidget(const QString& title, QWidget* parent)
    : QWidget(parent) {
    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create header button
    header_button = new QPushButton(title);
    // theme will handle styling
    header_button->setProperty("class", "collapsible-header");

    connect(header_button, &QPushButton::clicked, this, &CollapsibleMessageWidget::on_header_clicked);
    layout->addWidget(header_button);

    // Content widget (initially visible)
    content_widget = new QWidget();
    content_widget->setVisible(!collapsed);
    layout->addWidget(content_widget);
}

void CollapsibleMessageWidget::set_content(QWidget* widget) {
    if (content_widget->layout()) {
        delete content_widget->layout();
    }

    QVBoxLayout* content_layout = new QVBoxLayout(content_widget);
    content_layout->setContentsMargins(10, 5, 5, 5);
    content_layout->addWidget(widget);
}

void CollapsibleMessageWidget::set_collapsed(bool collapse) {
    collapsed = collapse;
    content_widget->setVisible(!collapsed);
    emit toggled(collapsed);
}

void CollapsibleMessageWidget::toggle_collapsed() {
    set_collapsed(!collapsed);
}

void CollapsibleMessageWidget::on_header_clicked() {
    toggle_collapsed();
}

// CodeViewer implementation
class CHighlighter : public QSyntaxHighlighter {
public:
    CHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

protected:
    void highlightBlock(const QString& text) override {
        // Keywords
        QTextCharFormat keyword_format;
        keyword_format.setForeground(QColor(ColorScheme::KEYWORD));
        keyword_format.setFontWeight(QFont::Bold);

        QStringList keywords = {
            "\\bint\\b", "\\bchar\\b", "\\bvoid\\b", "\\bfloat\\b", "\\bdouble\\b",
            "\\bif\\b", "\\belse\\b", "\\bfor\\b", "\\bwhile\\b", "\\breturn\\b",
            "\\bstruct\\b", "\\bclass\\b", "\\bpublic\\b", "\\bprivate\\b", "\\bprotected\\b",
            "\\bconst\\b", "\\bstatic\\b", "\\btypedef\\b", "\\benum\\b", "\\bunion\\b"
        };

        for (const QString& pattern : keywords) {
            QRegularExpression expression(pattern);
            QRegularExpressionMatchIterator it = expression.globalMatch(text);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), keyword_format);
            }
        }

        // Strings
        QTextCharFormat string_format;
        string_format.setForeground(QColor(ColorScheme::STRING));

        QRegularExpression string_expr("\".*\"|'.'");
        QRegularExpressionMatchIterator string_it = string_expr.globalMatch(text);
        while (string_it.hasNext()) {
            QRegularExpressionMatch match = string_it.next();
            setFormat(match.capturedStart(), match.capturedLength(), string_format);
        }

        // Comments
        QTextCharFormat comment_format;
        comment_format.setForeground(QColor(ColorScheme::COMMENT));
        comment_format.setFontItalic(true);

        QRegularExpression comment_expr("//[^\n]*");
        QRegularExpressionMatchIterator comment_it = comment_expr.globalMatch(text);
        while (comment_it.hasNext()) {
            QRegularExpressionMatch match = comment_it.next();
            setFormat(match.capturedStart(), match.capturedLength(), comment_format);
        }

        // Numbers
        QTextCharFormat number_format;
        number_format.setForeground(QColor(ColorScheme::NUMBER));

        QRegularExpression number_expr("\\b[0-9]+\\b|\\b0x[0-9a-fA-F]+\\b");
        QRegularExpressionMatchIterator number_it = number_expr.globalMatch(text);
        while (number_it.hasNext()) {
            QRegularExpressionMatch match = number_it.next();
            setFormat(match.capturedStart(), match.capturedLength(), number_format);
        }

        // Function names
        QTextCharFormat function_format;
        function_format.setForeground(QColor(ColorScheme::FUNCTION));

        QRegularExpression func_expr("\\b[a-zA-Z_][a-zA-Z0-9_]*(?=\\s*\\()");
        QRegularExpressionMatchIterator func_it = func_expr.globalMatch(text);
        while (func_it.hasNext()) {
            QRegularExpressionMatch match = func_it.next();
            setFormat(match.capturedStart(), match.capturedLength(), function_format);
        }
    }
};

CodeViewer::CodeViewer(Language lang, QWidget* parent)
    : QTextEdit(parent), language(lang) {
    setReadOnly(true);
    setFont(QFont("Consolas", 10));

    // Set up syntax highlighter based on language
    apply_syntax_highlighting();
}

void CodeViewer::set_code(const QString& code) {
    setPlainText(code);
}

void CodeViewer::set_language(Language lang) {
    language = lang;
    apply_syntax_highlighting();
}

void CodeViewer::apply_syntax_highlighting() {
    switch (language) {
        case C:
            new CHighlighter(document());
            break;
        case Assembly:
            highlight_assembly();
            break;
        case JSON:
            highlight_json();
            break;
        case Markdown:
            highlight_markdown();
            break;
    }
}

void CodeViewer::highlight_assembly() {
    // Simple assembly highlighter
    class AsmHighlighter : public QSyntaxHighlighter {
    public:
        AsmHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

    protected:
        void highlightBlock(const QString& text) override {
            // Instructions
            QTextCharFormat inst_format;
            inst_format.setForeground(QColor(ColorScheme::KEYWORD));
            inst_format.setFontWeight(QFont::Bold);

            QRegularExpression inst_expr("\\b(mov|push|pop|call|jmp|je|jne|jz|jnz|cmp|add|sub|mul|div|and|or|xor|ret|lea|test)\\b");
            auto matches = inst_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), inst_format);
            }

            // Registers
            QTextCharFormat reg_format;
            reg_format.setForeground(QColor(ColorScheme::FUNCTION));

            QRegularExpression reg_expr("\\b(rax|rbx|rcx|rdx|rsi|rdi|rbp|rsp|eax|ebx|ecx|edx|esi|edi|ebp|esp|ax|bx|cx|dx|al|ah|bl|bh|cl|ch|dl|dh)\\b");
            matches = reg_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), reg_format);
            }

            // Addresses
            QTextCharFormat addr_format;
            addr_format.setForeground(QColor(ColorScheme::ADDRESS));

            QRegularExpression addr_expr("0x[0-9a-fA-F]+");
            matches = addr_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), addr_format);
            }
        }
    };

    new AsmHighlighter(document());
}

void CodeViewer::highlight_json() {
    // JSON highlighter implementation
    class JsonHighlighter : public QSyntaxHighlighter {
    public:
        JsonHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

    protected:
        void highlightBlock(const QString& text) override {
            // Strings
            QTextCharFormat string_format;
            string_format.setForeground(QColor(ColorScheme::STRING));

            QRegularExpression string_expr("\"[^\"]*\"");
            auto matches = string_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), string_format);
            }

            // Numbers
            QTextCharFormat number_format;
            number_format.setForeground(QColor(ColorScheme::NUMBER));

            QRegularExpression number_expr("-?\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b");
            matches = number_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), number_format);
            }

            // Keywords
            QTextCharFormat keyword_format;
            keyword_format.setForeground(QColor(ColorScheme::KEYWORD));
            keyword_format.setFontWeight(QFont::Bold);

            QRegularExpression keyword_expr("\\b(true|false|null)\\b");
            matches = keyword_expr.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), keyword_format);
            }
        }
    };

    new JsonHighlighter(document());
}

void CodeViewer::highlight_markdown() {
    // Markdown highlighter - simplified
    class MarkdownHighlighter : public QSyntaxHighlighter {
    public:
        MarkdownHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

    protected:
        void highlightBlock(const QString& text) override {
            // Headers
            QTextCharFormat header_format;
            header_format.setForeground(QColor(ColorScheme::KEYWORD));
            header_format.setFontWeight(QFont::Bold);

            QRegularExpression header_expr("^#+\\s.*");
            auto match = header_expr.match(text);
            if (match.hasMatch()) {
                setFormat(match.capturedStart(), match.capturedLength(), header_format);
            }

            // Bold
            QTextCharFormat bold_format;
            bold_format.setFontWeight(QFont::Bold);

            QRegularExpression bold_expr("\\*\\*[^*]+\\*\\*|__[^_]+__");
            auto matches = bold_expr.globalMatch(text);
            while (matches.hasNext()) {
                match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), bold_format);
            }

            // Code
            QTextCharFormat code_format;
            code_format.setForeground(QColor(ColorScheme::STRING));
            code_format.setFontFamily("Consolas");

            QRegularExpression code_expr("`[^`]+`");
            matches = code_expr.globalMatch(text);
            while (matches.hasNext()) {
                match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), code_format);
            }
        }
    };

    new MarkdownHighlighter(document());
}

void CodeViewer::highlight_line(int line, QColor color) {
    QTextCursor cursor(document()->findBlockByLineNumber(line - 1));
    QTextBlockFormat format;
    format.setBackground(color);
    cursor.setBlockFormat(format);
}

void CodeViewer::clear_highlights() {
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.clearBackground();
    cursor.setBlockFormat(format);
}

// ToolExecutionWidget implementation
ToolExecutionWidget::ToolExecutionWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Create splitter
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // Tool execution tree
    execution_tree = new QTreeWidget();
    execution_tree->setHeaderLabels({"Tool", "Status", "Time", "Duration"});
    execution_tree->setAlternatingRowColors(true);
    splitter->addWidget(execution_tree);

    // Result viewer
    QWidget* right_panel = new QWidget();
    QVBoxLayout* right_layout = new QVBoxLayout(right_panel);

    result_viewer = new QTextBrowser();
    result_viewer->setFont(QFont("Consolas", 9));
    
    // Fix scrollbar issue: ensure horizontal scrollbar doesn't obscure content
    result_viewer->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    result_viewer->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    right_layout->addWidget(result_viewer);

    // Progress section
    status_label = new QLabel("Ready");
    right_layout->addWidget(status_label);

    progress_bar = new QProgressBar();
    progress_bar->setVisible(false);
    right_layout->addWidget(progress_bar);

    splitter->addWidget(right_panel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    layout->addWidget(splitter);

    // Connect signals
    connect(execution_tree, &QTreeWidget::itemSelectionChanged,
            this, &ToolExecutionWidget::on_item_selected);
}

void ToolExecutionWidget::add_tool_call(const std::string& tool_id, const std::string& tool_name, const json& input) {
    QTreeWidgetItem* item = new QTreeWidgetItem(execution_tree);
    item->setText(0, QString::fromStdString(tool_name));  // Display tool name
    item->setText(1, "Running...");
    item->setText(2, QTime::currentTime().toString("hh:mm:ss"));

    // Store tool ID for matching and input data
    item->setData(0, Qt::UserRole, QString::fromStdString(tool_id));  // Store tool ID for matching
    item->setData(0, Qt::UserRole + 1, QString::fromStdString(input.dump()));  // Store input data
    item->setData(2, Qt::UserRole, QDateTime::currentDateTime());  // Store start time

    execution_tree->scrollToItem(item);
}

void ToolExecutionWidget::update_tool_result(const std::string& tool_id, const json& result) {
    // Find the tool with exact matching ID
    for (int i = execution_tree->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* item = execution_tree->topLevelItem(i);
        QString stored_tool_id = item->data(0, Qt::UserRole).toString();
        if (stored_tool_id == QString::fromStdString(tool_id)) {
            // Calculate duration
            QDateTime start_time = item->data(2, Qt::UserRole).toDateTime();
            int duration_ms = start_time.msecsTo(QDateTime::currentDateTime());
            item->setText(3, QString("%1 ms").arg(duration_ms));

            // Update status based on result
            if (result.contains("success") && result["success"].get<bool>()) {
                item->setText(1, "Success");
                item->setForeground(1, QColor(0, 200, 0));  // Green
            } else {
                item->setText(1, "Failed");
                item->setForeground(1, QColor(200, 0, 0));  // Red

                // Add error as child if present
                if (result.contains("error")) {
                    QTreeWidgetItem* error_item = new QTreeWidgetItem(item);
                    error_item->setText(0, "Error");
                    error_item->setText(1, QString::fromStdString(result["error"].get<std::string>()));
                    error_item->setForeground(1, QColor(200, 0, 0));  // Red
                }
            }

            // Store result data
            item->setData(1, Qt::UserRole, QString::fromStdString(result.dump()));
            break;
        }
    }
}

void ToolExecutionWidget::set_progress(int value, const std::string& status) {
    progress_bar->setVisible(value >= 0 && value <= 100);
    progress_bar->setValue(value);
    status_label->setText(QString::fromStdString(status));
}

void ToolExecutionWidget::on_item_selected() {
    auto items = execution_tree->selectedItems();
    if (items.isEmpty()) return;

    QTreeWidgetItem* item = items.first();

    // Get theme info
    MainForm* main_form = get_main_form();
    bool is_dark_theme = false;
    if (main_form) {
        const Config* config = main_form->get_config();
        is_dark_theme = (config->ui.theme == 0 || config->ui.theme == 1);
    }

    // Theme-aware colors
    QString bg_color = is_dark_theme ? "#2b2b2b" : "#f0f0f0";
    QString text_color = is_dark_theme ? "#ffffff" : "#000000";
    QString code_bg = is_dark_theme ? "#1e1e1e" : "#f5f5f5";

    // Show input and result
    QString html = QString(R"(
        <html>
        <head>
        <style>
            body { background-color: %1; color: %2; font-family: Arial, sans-serif; padding-bottom: 20px; }
            pre { background-color: %3; color: %2; padding: 10px; border-radius: 5px; overflow-x: auto; }
            h3, h4 { color: %2; }
            b { color: %2; }
        </style>
        </head>
        <body>
    )").arg(bg_color).arg(text_color).arg(code_bg);

    html += "<h3>Tool: " + item->text(0) + "</h3>";
    html += "<p><b>Status:</b> " + item->text(1) + "</p>";
    html += "<p><b>Time:</b> " + item->text(2) + "</p>";
    html += "<p><b>Duration:</b> " + item->text(3) + "</p>";

    // Input
    QString input_str = item->data(0, Qt::UserRole + 1).toString();
    if (!input_str.isEmpty()) {
        try {
            json input = json::parse(input_str.toStdString());
            html += "<h4>Input:</h4><pre>" +
                    QString::fromStdString(input.dump(2)) + "</pre>";
        } catch (...) {}
    }

    // Result
    QString result_str = item->data(1, Qt::UserRole).toString();
    if (!result_str.isEmpty()) {
        try {
            json result = json::parse(result_str.toStdString());
            html += "<h4>Result:</h4><pre>" +
                    QString::fromStdString(result.dump(2)) + "</pre>";
        } catch (...) {}
    }

    html += "</body></html>";
    result_viewer->setHtml(html);
}

// SessionTimelineWidget implementation
SessionTimelineWidget::SessionTimelineWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(150);
    setMouseTracking(true);
}

void SessionTimelineWidget::add_event(const Event& event) {
    events.push_back(event);
    update();
}

void SessionTimelineWidget::clear_events() {
    events.clear();
    update();
}

void SessionTimelineWidget::set_session_info(const std::string& task, const api::TokenUsage& usage) {
    session_task = task;
    token_usage = usage;
    update();
}

void SessionTimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get theme from main form
    MainForm* main_form = get_main_form();
    bool is_dark_theme = false;
    if (main_form) {
        const Config* config = main_form->get_config();
        is_dark_theme = (config->ui.theme == 0 || config->ui.theme == 1);
    }

    // Draw background based on theme
    if (is_dark_theme) {
        painter.fillRect(rect(), QColor(0x3c, 0x3c, 0x3c));  // Dark gray like other widgets
    } else {
        painter.fillRect(rect(), Qt::white);
    }

    // Set text color based on theme
    if (is_dark_theme) {
        painter.setPen(Qt::white);
    } else {
        painter.setPen(Qt::black);
    }

    // Draw timeline
    draw_timeline(painter);

    // Draw events
    if (!events.empty()) {
        auto start_time = events.front().timestamp;
        auto end_time = events.back().timestamp;
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        int timeline_start = 40;
        int timeline_end = width() - 40;
        int timeline_y = height() / 2;

        for (const auto& evt : events) {
            auto event_time = std::chrono::duration_cast<std::chrono::milliseconds>(evt.timestamp - start_time).count();
            int x = timeline_start + (event_time * (timeline_end - timeline_start)) / std::max(duration, (long long)1);
            draw_event(painter, evt, x, timeline_y);
        }
    }

    // Draw session info with theme-aware text
    if (!session_task.empty()) {
        // Text is already set to correct color above
        painter.drawText(10, 20, QString("Task: %1").arg(QString::fromStdString(session_task)));
        painter.drawText(10, height() - 20,
            QString("Tokens: %1 in / %2 out / %3 cache read / %4 cache write")
                .arg(token_usage.input_tokens)
                .arg(token_usage.output_tokens)
                .arg(token_usage.cache_read_tokens)
                .arg(token_usage.cache_creation_tokens));
    }

    // Draw hover tooltip with theme colors
    if (hover_event && !hover_event->description.empty()) {
        QPoint cursor_pos = mapFromGlobal(QCursor::pos());
        QRect tooltip_rect(cursor_pos.x() + 10, cursor_pos.y() - 30, 250, 60);

        // Ensure tooltip stays within widget
        if (tooltip_rect.right() > width()) {
            tooltip_rect.moveRight(cursor_pos.x() - 10);
        }
        if (tooltip_rect.top() < 0) {
            tooltip_rect.moveTop(cursor_pos.y() + 10);
        }

        // Theme-aware tooltip
        if (is_dark_theme) {
            painter.fillRect(tooltip_rect, QColor(70, 70, 70, 230));  // Dark tooltip
            painter.setPen(Qt::white);
        } else {
            painter.fillRect(tooltip_rect, QColor(255, 255, 200, 230));  // Light tooltip
            painter.setPen(Qt::black);
        }

        painter.drawRect(tooltip_rect);
        painter.drawText(tooltip_rect.adjusted(5, 5, -5, -5),
                        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                        QString::fromStdString(hover_event->description));
    }
}

void SessionTimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    Event* evt = event_at_point(event->pos());
    if (evt) {
        hover_event = *evt;
    } else {
        hover_event = std::nullopt;
    }
    update();
}

SessionTimelineWidget::Event* SessionTimelineWidget::event_at_point(const QPoint& point) {
    if (events.empty()) return nullptr;

    auto start_time = events.front().timestamp;
    auto end_time = events.back().timestamp;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    int timeline_start = 40;
    int timeline_end = width() - 40;
    int timeline_y = height() / 2;

    for (auto& evt : events) {
        auto event_time = std::chrono::duration_cast<std::chrono::milliseconds>(evt.timestamp - start_time).count();
        int x = timeline_start + (event_time * (timeline_end - timeline_start)) / std::max(duration, (long long)1);

        QRect event_rect(x - 8, timeline_y - 8, 16, 16);
        if (event_rect.contains(point)) {
            return &evt;
        }
    }

    return nullptr;
}

void SessionTimelineWidget::draw_timeline(QPainter& painter) {
    int timeline_start = 40;
    int timeline_end = width() - 40;
    int y = height() / 2;

    // Save current pen
    QPen current_pen = painter.pen();

    // Draw horizontal line
    painter.setPen(QPen(current_pen.color(), 2));  // Use current color
    painter.drawLine(timeline_start, y, timeline_end, y);

    // Draw tick marks
    painter.setPen(QPen(current_pen.color(), 1));
    for (int i = 0; i <= 10; i++) {
        int x = timeline_start + i * (timeline_end - timeline_start) / 10;
        painter.drawLine(x, y - 5, x, y + 5);
    }
}

void SessionTimelineWidget::draw_event(QPainter& painter, const Event& event, int x, int y) {
    // Get theme
    MainForm* main_form = get_main_form();
    bool is_dark_theme = false;
    if (main_form) {
        const Config* config = main_form->get_config();
        is_dark_theme = (config->ui.theme == 0 || config->ui.theme == 1);
    }

    // Select color based on event type
    QColor color;

    if (event.type == "start") {
        color = Qt::green;
    } else if (event.type == "tool") {
        color = is_dark_theme ? QColor(100, 150, 255) : Qt::blue;  // Lighter blue for dark theme
    } else if (event.type == "message") {
        color = is_dark_theme ? QColor(150, 150, 255) : QColor(100, 100, 255);  // Lighter for dark theme
    } else if (event.type == "error") {
        color = Qt::red;
    } else if (event.type == "complete") {
        color = is_dark_theme ? QColor(100, 255, 100) : Qt::darkGreen;  // Lighter green for dark theme
    } else {
        color = Qt::gray;
    }

    // Draw event marker
    painter.setPen(QPen(color, 2));
    painter.setBrush(color);
    painter.drawEllipse(QPoint(x, y), 6, 6);
}

// SearchDialog implementation
SearchDialog::SearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Advanced Search");
    setModal(false);
    resize(700, 500);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Search input section
    QHBoxLayout* search_layout = new QHBoxLayout();

    search_edit = new QLineEdit();
    search_edit->setPlaceholderText("Enter search query...");
    search_layout->addWidget(search_edit);

    search_type = new QComboBox();
    search_type->addItems({"All", "Logs", "Messages", "Memory"});
    search_layout->addWidget(search_type);

    QPushButton* search_button = new QPushButton("Search");
    connect(search_button, &QPushButton::clicked, this, &SearchDialog::perform_search);
    search_layout->addWidget(search_button);

    layout->addLayout(search_layout);

    // Search options
    QHBoxLayout* options_layout = new QHBoxLayout();

    case_sensitive = new QCheckBox("Case sensitive");
    options_layout->addWidget(case_sensitive);

    regex_search = new QCheckBox("Regular expression");
    options_layout->addWidget(regex_search);

    whole_words = new QCheckBox("Whole words");
    options_layout->addWidget(whole_words);

    options_layout->addStretch();
    layout->addLayout(options_layout);

    // Results tree
    results_tree = new QTreeWidget();
    results_tree->setHeaderLabels({"Type", "Location", "Match"});
    results_tree->setAlternatingRowColors(true);
    results_tree->setSortingEnabled(true);
    connect(results_tree, &QTreeWidget::itemDoubleClicked,
            this, &SearchDialog::on_result_double_clicked);
    layout->addWidget(results_tree);

    // Status bar
    QStatusBar* status_bar = new QStatusBar();
    layout->addWidget(status_bar);

    // Connect enter key
    connect(search_edit, &QLineEdit::returnPressed, this, &SearchDialog::perform_search);
}

void SearchDialog::set_search_data(const std::vector<LogEntry>& logs,
                                  const std::vector<messages::Message>& messages,
                                  const json& memory) {
    search_logs = logs;
    search_messages = messages;
    search_memory = memory;
}

void SearchDialog::perform_search() {
    results_tree->clear();

    std::string query = search_edit->text().toStdString();
    if (query.empty()) return;

    SearchType type = static_cast<SearchType>(search_type->currentIndex());

    // Perform search based on type
    std::vector<SearchResult> results;

    if (type == All || type == Logs) {
        auto log_results = search_in_logs(query);
        results.insert(results.end(), log_results.begin(), log_results.end());
    }

    if (type == All || type == Messages) {
        auto msg_results = search_in_messages(query);
        results.insert(results.end(), msg_results.begin(), msg_results.end());
    }

    if (type == All || type == Memory) {
        auto mem_results = search_in_memory(query);
        results.insert(results.end(), mem_results.begin(), mem_results.end());
    }

    // Populate results tree
    for (const auto& result : results) {
        QTreeWidgetItem* item = new QTreeWidgetItem(results_tree);

        QString type_str;
        switch (result.type) {
            case Logs: type_str = "Log"; break;
            case Messages: type_str = "Message"; break;
            case Memory: type_str = "Memory"; break;
            default: type_str = "Unknown";
        }
        item->setText(0, type_str);
        item->setText(1, QString::fromStdString(result.context));
        item->setText(2, QString::fromStdString(result.match));

        // Store full result data
        QVariant var;
        var.setValue(result);
        item->setData(0, Qt::UserRole, var);
    }

    // Update status
    setWindowTitle(QString("Search Results - %1 matches found").arg(results.size()));
}

void SearchDialog::on_result_double_clicked(QTreeWidgetItem* item) {
    if (!item) return;

    SearchResult result = item->data(0, Qt::UserRole).value<SearchResult>();
    emit result_selected(result);
}

std::vector<SearchDialog::SearchResult> SearchDialog::search_in_logs(const std::string& query) {
    std::vector<SearchResult> results;

    for (size_t i = 0; i < search_logs.size(); ++i) {
        const auto& entry = search_logs[i];

        bool match = false;
        std::string match_text;

        if (regex_search->isChecked()) {
            try {
                std::regex re(query, case_sensitive->isChecked() ? std::regex::ECMAScript : std::regex::icase);
                std::smatch m;
                if (std::regex_search(entry.message, m, re)) {
                    match = true;
                    match_text = m.str();
                }
            } catch (...) {
                // Invalid regex
                continue;
            }
        } else {
            std::string haystack = entry.message;
            std::string needle = query;

            if (!case_sensitive->isChecked()) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }

            if (haystack.find(needle) != std::string::npos) {
                match = true;
                match_text = entry.message;
            }
        }

        if (match) {
            SearchResult result;
            result.type = Logs;
            result.context = LogEntry::level_to_string(entry.level) + " Line " + std::to_string(i + 1);
            result.match = match_text;
            result.line_number = i;
            result.metadata["timestamp"] = std::chrono::system_clock::to_time_t(entry.timestamp);
            result.metadata["level"] = entry.level;
            results.push_back(result);
        }
    }

    return results;
}

std::vector<SearchDialog::SearchResult> SearchDialog::search_in_messages(const std::string& query) {
    std::vector<SearchResult> results;

    for (size_t i = 0; i < search_messages.size(); ++i) {
        const auto& msg = search_messages[i];

        // Extract text content from message
        std::string content;
        for (const auto& c : msg.contents()) {
            if (auto text = dynamic_cast<const messages::TextContent*>(c.get())) {
                content += text->text + " ";
            } else if (auto tool = dynamic_cast<const messages::ToolUseContent*>(c.get())) {
                content += "Tool: " + tool->name + " ";
            }
        }

        if (!case_sensitive->isChecked()) {
            std::transform(content.begin(), content.end(), content.begin(), ::tolower);
        }

        std::string search_query = query;
        if (!case_sensitive->isChecked()) {
            std::transform(search_query.begin(), search_query.end(), search_query.begin(), ::tolower);
        }

        if (content.find(search_query) != std::string::npos) {
            SearchResult result;
            result.type = Messages;
            result.context = messages::role_to_string(msg.role()) + " - Message " + std::to_string(i + 1);
            result.match = content.substr(0, 200) + (content.length() > 200 ? "..." : "");
            result.line_number = i;
            result.metadata["role"] = messages::role_to_string(msg.role());
            result.metadata["index"] = i;
            results.push_back(result);
        }
    }

    return results;
}

std::vector<SearchDialog::SearchResult> SearchDialog::search_in_memory(const std::string& query) {
    std::vector<SearchResult> results;

    if (search_memory.contains("functions")) {
        for (const auto& func : search_memory["functions"]) {
            // Search in various fields
            std::string combined;
            if (func.contains("name")) combined += func["name"].get<std::string>() + " ";
            if (func.contains("descriptions")) {
                for (const auto& [level, desc] : func["descriptions"].items()) {
                    combined += desc.get<std::string>() + " ";
                }
            }

            if (!case_sensitive->isChecked()) {
                std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
            }

            std::string search_query = query;
            if (!case_sensitive->isChecked()) {
                std::transform(search_query.begin(), search_query.end(), search_query.begin(), ::tolower);
            }

            if (combined.find(search_query) != std::string::npos) {
                SearchResult result;
                result.type = Memory;
                result.context = "Function " + func["address"].get<std::string>();
                result.match = func.value("name", "unknown");
                result.metadata = func;
                results.push_back(result);
            }
        }
    }

    return results;
}

// StatsDashboard implementation
struct StatsDashboard::ChartWidget : public QWidget {
    QString title;
    std::vector<std::pair<QString, double>> data;

    ChartWidget(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent), title(title) {
        setMinimumHeight(200);
    }

    void set_data(const std::vector<std::pair<QString, double>>& new_data) {
        data = new_data;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw background
        painter.fillRect(rect(), Qt::white);
        painter.setPen(Qt::black);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        // Draw title
        QFont title_font = font();
        title_font.setBold(true);
        painter.setFont(title_font);
        painter.drawText(rect().adjusted(10, 10, -10, -10), Qt::AlignTop | Qt::AlignHCenter, title);

        if (data.empty()) return;

        // Simple bar chart
        int margin = 20;
        int title_height = 30;
        int chart_height = height() - title_height - 2 * margin;
        int chart_width = width() - 2 * margin;
        int bar_width = chart_width / data.size();

        // Find max value
        double max_value = 0;
        for (const auto& [label, value] : data) {
            max_value = std::max(max_value, value);
        }

        // Draw bars
        painter.setFont(font());
        for (size_t i = 0; i < data.size(); ++i) {
            const auto& [label, value] = data[i];

            int bar_height = (value / max_value) * chart_height;
            int x = margin + i * bar_width + bar_width / 4;
            int y = height() - margin - bar_height;
            int w = bar_width / 2;

            // Draw bar
            painter.fillRect(x, y, w, bar_height, QColor(100, 150, 255));
            painter.setPen(Qt::black);
            painter.drawRect(x, y, w, bar_height);

            // Draw label
            painter.save();
            painter.translate(x + w/2, height() - margin + 15);
            painter.rotate(-45);
            painter.drawText(0, 0, label);
            painter.restore();

            // Draw value
            painter.drawText(x, y - 5, w, 20, Qt::AlignCenter, QString::number(value, 'f', 0));
        }
    }
};

StatsDashboard::StatsDashboard(QWidget* parent) : QWidget(parent) {
    layout = new QGridLayout(this);

    // Create charts
    tool_chart = new ChartWidget("Tool Calls");
    layout->addWidget(tool_chart, 0, 1);

    time_chart = new ChartWidget("Execution Time");
    layout->addWidget(time_chart, 1, 0);

    // Summary browser
    summary_browser = new QTextBrowser();
    summary_browser->setMinimumHeight(150);
    layout->addWidget(summary_browser, 1, 1);
}

void StatsDashboard::update_stats(const json& agent_state,
                                 const std::vector<SessionInfo>& sessions,
                                 const json& tool_stats) {
    // Update tool chart
    update_tool_chart(tool_stats);

    // Update time chart
    update_time_chart(sessions);

    // Update summary
    json stats;
    stats["total_sessions"] = sessions.size();
    stats["total_time_ms"] = 0;

    for (const auto& session : sessions) {
        stats["total_time_ms"] = stats["total_time_ms"].get<int>() + session.duration_ms;
    }

    summary_browser->setHtml(generate_summary_html(stats));
}

QString StatsDashboard::generate_summary_html(const json& stats) {
    QString html = "<h3>Summary Statistics</h3>";
    html += "<table style='width: 100%;'>";

    html += QString("<tr><td><b>Total Sessions:</b></td><td>%1</td></tr>")
        .arg(stats.value("total_sessions", 0));

    int total_ms = stats.value("total_time_ms", 0);
    html += QString("<tr><td><b>Total Time:</b></td><td>%1s</td></tr>")
        .arg(total_ms / 1000.0, 0, 'f', 2);

    html += "</table>";

    return html;
}

void StatsDashboard::update_tool_chart(const json& tool_stats) {
    std::vector<std::pair<QString, double>> data;

    if (tool_stats.is_object()) {
        for (const auto& [tool_name, count] : tool_stats.items()) {
            data.push_back({
                QString::fromStdString(tool_name),
                count.get<double>()
            });
        }
    }

    // Sort by count
    std::sort(data.begin(), data.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Keep top 5
    if (data.size() > 5) {
        data.resize(5);
    }

    tool_chart->set_data(data);
}

void StatsDashboard::update_time_chart(const std::vector<SessionInfo>& sessions) {
    std::vector<std::pair<QString, double>> data;

    // Show last 5 sessions
    int start = std::max(0, (int)sessions.size() - 5);
    for (int i = start; i < sessions.size(); ++i) {
        data.push_back({
            QString("Session %1").arg(i + 1),
            sessions[i].duration_ms / 1000.0  // Convert to seconds
        });
    }

    time_chart->set_data(data);
}

// ExportDialog implementation
ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Export Session Data");
    setModal(true);
    resize(400, 300);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Options group
    QGroupBox* options_group = new QGroupBox("Export Options");
    QVBoxLayout* options_layout = new QVBoxLayout(options_group);

    include_logs = new QCheckBox("Include logs");
    include_logs->setChecked(true);
    options_layout->addWidget(include_logs);

    include_messages = new QCheckBox("Include messages");
    include_messages->setChecked(true);
    options_layout->addWidget(include_messages);

    include_memory = new QCheckBox("Include memory snapshot");
    include_memory->setChecked(true);
    options_layout->addWidget(include_memory);

    include_stats = new QCheckBox("Include statistics");
    include_stats->setChecked(true);
    options_layout->addWidget(include_stats);

    include_timeline = new QCheckBox("Include timeline");
    include_timeline->setChecked(false);
    options_layout->addWidget(include_timeline);

    layout->addWidget(options_group);

    // Format selection
    QHBoxLayout* format_layout = new QHBoxLayout();
    format_layout->addWidget(new QLabel("Format:"));

    format_combo = new QComboBox();
    format_combo->addItems({"Markdown", "JSON"});
    connect(format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::on_format_changed);
    format_layout->addWidget(format_combo);

    format_layout->addStretch();
    layout->addLayout(format_layout);

    // Template selection
    QHBoxLayout* template_layout = new QHBoxLayout();
    template_layout->addWidget(new QLabel("Template:"));

    template_edit = new QLineEdit();
    template_edit->setPlaceholderText("Optional custom template file");
    template_layout->addWidget(template_edit);

    browse_template = new QPushButton("Browse...");
    connect(browse_template, &QPushButton::clicked, this, &ExportDialog::on_browse_template);
    template_layout->addWidget(browse_template);

    layout->addLayout(template_layout);

    // Dialog buttons
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ExportDialog::ExportOptions ExportDialog::get_options() const {
    ExportOptions options;
    options.logs = include_logs->isChecked();
    options.messages = include_messages->isChecked();
    options.memory = include_memory->isChecked();
    options.statistics = include_stats->isChecked();
    options.timeline = include_timeline->isChecked();
    options.format = static_cast<ExportFormat>(format_combo->currentIndex());
    options.custom_template = template_edit->text().toStdString();
    return options;
}

void ExportDialog::on_browse_template() {
    QString filename = QFileDialog::getOpenFileName(this,
        "Select Template File", "", "Template Files (*.tpl *.html *.md);;All Files (*)");

    if (!filename.isEmpty()) {
        template_edit->setText(filename);
    }
}

void ExportDialog::on_format_changed(int index) {
    // Enable/disable template option based on format
    bool enable_template = (index == Markdown);
    template_edit->setEnabled(enable_template);
    browse_template->setEnabled(enable_template);
}

// ConfigWidget implementation
ConfigWidget::ConfigWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* main_layout = new QVBoxLayout(this);

    // Create tabs
    QTabWidget* tabs = new QTabWidget();
    main_layout->addWidget(tabs);

    // API Settings Tab
    QWidget* api_tab = new QWidget();
    QFormLayout* api_layout = new QFormLayout(api_tab);

    api_key_edit = new QLineEdit();
    api_key_edit->setEchoMode(QLineEdit::Password);
    api_layout->addRow("API Key:", api_key_edit);

    test_api_button = new QPushButton("Test Connection");
    connect(test_api_button, &QPushButton::clicked, this, &ConfigWidget::on_test_api);
    api_status_label = new QLabel("Not tested");
    QHBoxLayout* test_layout = new QHBoxLayout();
    test_layout->addWidget(test_api_button);
    test_layout->addWidget(api_status_label);
    test_layout->addStretch();
    api_layout->addRow("", test_layout);

    tabs->addTab(api_tab, "API");

    // Model Settings Tab
    QWidget* model_tab = new QWidget();
    QFormLayout* model_layout = new QFormLayout(model_tab);

    model_combo = new QComboBox();
    model_combo->addItems({"Opus 4", "Sonnet 4", "Sonnet 3.7", "Haiku 3.5"});
    model_layout->addRow("Model:", model_combo);

    max_tokens_spin = new QSpinBox();
    max_tokens_spin->setRange(100, 200000);
    max_tokens_spin->setValue(8192);
    model_layout->addRow("Max Tokens:", max_tokens_spin);

    max_thinking_tokens_spin = new QSpinBox();
    max_thinking_tokens_spin->setRange(1024, 8192);
    max_thinking_tokens_spin->setValue(2048);
    model_layout->addRow("Max Thinking Tokens:", max_thinking_tokens_spin);

    max_iterations_spin = new QSpinBox();
    max_iterations_spin->setRange(1, 200);
    max_iterations_spin->setValue(100);
    model_layout->addRow("Max Iterations:", max_iterations_spin);

    temperature_spin = new QDoubleSpinBox();
    temperature_spin->setRange(0.0, 1.0);
    temperature_spin->setSingleStep(0.1);
    temperature_spin->setValue(0.0);
    model_layout->addRow("Temperature:", temperature_spin);

    enable_thinking_check = new QCheckBox("Enable thinking mode");
    enable_thinking_check->setChecked(false);
    model_layout->addRow("", enable_thinking_check);

    enable_interleaved_thinking_check = new QCheckBox("Enable interleaved thinking mode");
    enable_interleaved_thinking_check->setChecked(false);
    model_layout->addRow("", enable_interleaved_thinking_check);

    enable_deep_analysis_check = new QCheckBox("Enable deep analysis mode");
    enable_deep_analysis_check->setChecked(false);
    model_layout->addRow("", enable_deep_analysis_check);

    tabs->addTab(model_tab, "Model");

    // UI Settings Tab
    QWidget* ui_tab = new QWidget();
    QFormLayout* ui_layout = new QFormLayout(ui_tab);

    log_buffer_spin = new QSpinBox();
    log_buffer_spin->setRange(100, 10000);
    log_buffer_spin->setValue(1000);
    ui_layout->addRow("Log Buffer Size:", log_buffer_spin);

    auto_scroll_check = new QCheckBox("Auto-scroll logs");
    auto_scroll_check->setChecked(true);
    ui_layout->addRow("", auto_scroll_check);

    theme_combo = new QComboBox();
    theme_combo->addItems({"Default", "Dark", "Light"});
    ui_layout->addRow("Theme:", theme_combo);

    font_size_spin = new QSpinBox();
    font_size_spin->setRange(8, 20);
    font_size_spin->setValue(10);
    ui_layout->addRow("Font Size:", font_size_spin);

    tabs->addTab(ui_tab, "UI");

    // Export Settings Tab
    QWidget* export_tab = new QWidget();
    QFormLayout* export_layout = new QFormLayout(export_tab);

    QHBoxLayout* path_layout = new QHBoxLayout();
    export_path_edit = new QLineEdit();
    path_layout->addWidget(export_path_edit);

    QPushButton* browse_button = new QPushButton("Browse...");
    connect(browse_button, &QPushButton::clicked, this, &ConfigWidget::on_browse_export_path);
    path_layout->addWidget(browse_button);

    export_layout->addRow("Export Path:", path_layout);


    tabs->addTab(export_tab, "Export");

    // Advanced Settings Tab
    QWidget* advanced_tab = new QWidget();
    QVBoxLayout* advanced_layout = new QVBoxLayout(advanced_tab);

    QFormLayout* advanced_form = new QFormLayout();

    debug_mode_check = new QCheckBox("Enable debug mode");
    advanced_form->addRow("", debug_mode_check);

    advanced_layout->addLayout(advanced_form);
    advanced_layout->addStretch();

    tabs->addTab(advanced_tab, "Advanced");

    // Bottom buttons
    QHBoxLayout* button_layout = new QHBoxLayout();

    QPushButton* reset_button = new QPushButton("Reset to Defaults");
    connect(reset_button, &QPushButton::clicked, this, &ConfigWidget::on_reset_defaults);
    button_layout->addWidget(reset_button);

    button_layout->addStretch();

    QPushButton* save_button = new QPushButton("Save");
    connect(save_button, &QPushButton::clicked, [this]() { emit settings_changed(); });
    button_layout->addWidget(save_button);

    main_layout->addLayout(button_layout);
}

void ConfigWidget::load_settings(const Config& config) {
    api_key_edit->setText(QString::fromStdString(config.api.api_key));

    // Map model enum to combo index
    switch (config.api.model) {
        case api::Model::Opus4: model_combo->setCurrentIndex(0); break;
        case api::Model::Sonnet4: model_combo->setCurrentIndex(1); break;
        case api::Model::Sonnet37: model_combo->setCurrentIndex(2); break;
        case api::Model::Haiku35: model_combo->setCurrentIndex(3); break;
    }

    max_tokens_spin->setValue(config.api.max_tokens);
    max_thinking_tokens_spin->setValue(config.api.max_thinking_tokens);
    max_iterations_spin->setValue(config.agent.max_iterations);
    temperature_spin->setValue(config.api.temperature);
    enable_thinking_check->setChecked(config.agent.enable_thinking);
    enable_interleaved_thinking_check->setChecked(config.agent.enable_interleaved_thinking);
    enable_deep_analysis_check->setChecked(config.agent.enable_deep_analysis);

    log_buffer_spin->setValue(config.ui.log_buffer_size);
    auto_scroll_check->setChecked(config.ui.auto_scroll);
    theme_combo->setCurrentIndex(config.ui.theme);
    font_size_spin->setValue(config.ui.font_size);

    export_path_edit->setText(QString::fromStdString(config.export_settings.path));

    debug_mode_check->setChecked(config.debug_mode);
}

void ConfigWidget::save_settings(Config& config) {
    config.api.api_key = api_key_edit->text().toStdString();

    // Map combo index to model enum
    switch (model_combo->currentIndex()) {
        case 0: config.api.model = api::Model::Opus4; break;
        case 1: config.api.model = api::Model::Sonnet4; break;
        case 2: config.api.model = api::Model::Sonnet37; break;
        case 3: config.api.model = api::Model::Haiku35; break;
    }

    config.api.max_tokens = max_tokens_spin->value();
    config.api.max_thinking_tokens = max_thinking_tokens_spin->value();
    config.agent.max_iterations = max_iterations_spin->value();
    config.api.temperature = temperature_spin->value();
    config.agent.enable_thinking = enable_thinking_check->isChecked();
    config.agent.enable_interleaved_thinking = enable_interleaved_thinking_check->isChecked();
    config.agent.enable_deep_analysis = enable_deep_analysis_check->isChecked();

    config.ui.log_buffer_size = log_buffer_spin->value();
    config.ui.auto_scroll = auto_scroll_check->isChecked();
    config.ui.theme = theme_combo->currentIndex();
    config.ui.font_size = font_size_spin->value();

    config.export_settings.path = export_path_edit->text().toStdString();

    config.debug_mode = debug_mode_check->isChecked();
}

void ConfigWidget::on_test_api() {
    api_status_label->setText("Testing...");
    api_status_label->setStyleSheet("color: orange;");

    // Simple validation - just check if API key is provided
    QTimer::singleShot(500, [this]() {
        bool success = !api_key_edit->text().isEmpty();

        if (success) {
            api_status_label->setText("API key provided");
            api_status_label->setStyleSheet("color: green;");
        } else {
            api_status_label->setText("No API key");
            api_status_label->setStyleSheet("color: red;");
        }
    });
}

void ConfigWidget::on_browse_export_path() {
    QString dir = QFileDialog::getExistingDirectory(this,
        "Select Export Directory", export_path_edit->text());

    if (!dir.isEmpty()) {
        export_path_edit->setText(dir);
    }
}

void ConfigWidget::on_reset_defaults() {
    if (QMessageBox::question(this, "Reset Settings",
                            "Are you sure you want to reset all settings to defaults?",
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        Config default_config;
        load_settings(default_config);
    }
}

// TaskTemplateWidget implementation
TaskTemplateWidget::TaskTemplateWidget(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* main_layout = new QHBoxLayout(this);

    // Left side - template list
    QVBoxLayout* left_layout = new QVBoxLayout();

    template_list = new QListWidget();
    connect(template_list, &QListWidget::currentRowChanged,
            this, &TaskTemplateWidget::on_template_selected);
    left_layout->addWidget(template_list);

    // Buttons
    QHBoxLayout* button_layout = new QHBoxLayout();

    new_button = new QPushButton("New");
    connect(new_button, &QPushButton::clicked, this, &TaskTemplateWidget::on_new_template);
    button_layout->addWidget(new_button);

    edit_button = new QPushButton("Edit");
    edit_button->setEnabled(false);
    connect(edit_button, &QPushButton::clicked, this, &TaskTemplateWidget::on_edit_template);
    button_layout->addWidget(edit_button);

    delete_button = new QPushButton("Delete");
    delete_button->setEnabled(false);
    connect(delete_button, &QPushButton::clicked, this, &TaskTemplateWidget::on_delete_template);
    button_layout->addWidget(delete_button);

    left_layout->addLayout(button_layout);
    main_layout->addLayout(left_layout);

    // Right side - preview
    QVBoxLayout* right_layout = new QVBoxLayout();

    right_layout->addWidget(new QLabel("Preview:"));

    template_preview = new QTextEdit();
    template_preview->setReadOnly(true);
    right_layout->addWidget(template_preview);

    use_button = new QPushButton("Use Template");
    use_button->setEnabled(false);
    connect(use_button, &QPushButton::clicked, this, &TaskTemplateWidget::on_use_template);
    right_layout->addWidget(use_button);

    main_layout->addLayout(right_layout);
    main_layout->setStretchFactor(left_layout, 1);
    main_layout->setStretchFactor(right_layout, 2);

    // Load templates
    load_templates();
}

void TaskTemplateWidget::load_templates() {
    // Load default templates
    templates.clear();

    // Function analysis template
    TaskTemplate func_template;
    func_template.name = "Analyze Function";
    func_template.description = "Comprehensive function analysis";
    func_template.task = "Analyze the function at address {address}. "
                        "Provide:\n"
                        "1. Function purpose and behavior\n"
                        "2. Parameter analysis\n"
                        "3. Return value analysis\n"
                        "4. Key algorithms or logic\n"
                        "5. Potential vulnerabilities";
    func_template.variables["address"] = "current_ea";
    templates.push_back(func_template);

    // Vulnerability search
    TaskTemplate vuln_template_phase1;
    vuln_template_phase1.name = "Finding Vulnerabilities - Phase 1";
    vuln_template_phase1.description = "Hunt for promising vulnerabilities";
    vuln_template_phase1.task = R"(Think like a security researcher analyzing this binary for vulnerabilities, but maintain scientific rigor.

**Step 1: Attack Surface Mapping**
- Identify what an attacker can control (input vectors, files, network data, IPC)
- Document EXACTLY how an attacker would provide this input
- Verify these inputs are actually reachable in practice

**Step 2: Initial Analysis**
Look for potentially vulnerable patterns:
- Memory safety issues (buffer overflows, use-after-free, double-free)
- Integer arithmetic issues (overflow, underflow, signedness)
- Race conditions and TOCTOU bugs
- Logic flaws and assumption violations
- Type confusion and casting issues
- Injection vulnerabilities (command, SQL, format string)
- Improper input validation

**Step 3: Hypothesis Formation**
For each potential issue:
1. Form a NULL HYPOTHESIS: "This code is secure because..."
2. Identify what evidence would DISPROVE the null hypothesis
3. Document your assumptions vs. verified facts

**Step 4: Initial Verification**
Before claiming ANY vulnerability:
- Trace the COMPLETE path from input to potentially vulnerable code
- Identify existing safety mechanisms (bounds checks, validations, locks)
- Document the specific conditions required to reach the vulnerable code
- Verify your understanding of the code logic is correct

**Step 5: Evidence Collection**
Use store_analysis to document:
- The specific input you control and how
- The exact potentially vulnerable code location
- CONCRETE EVIDENCE of the issue (not just suspicion)
- Any safety checks that might prevent exploitation
- Why you believe the null hypothesis is false

Only proceed to submit_final_report if you have CONCRETE EVIDENCE of a vulnerability, not just complex code that "looks suspicious.")";
    templates.push_back(vuln_template_phase1);

    TaskTemplate vuln_template_phase2;
    vuln_template_phase2.name = "Finding Vulnerabilities - Phase 2";
    vuln_template_phase2.description = "Deep dive and prove exploitability";
    vuln_template_phase2.task = R"(You've identified a potential vulnerability. Now PROVE it exists and is exploitable.

**Step 1: Vulnerability Proof**
Provide concrete evidence appropriate to the vulnerability type:
- For memory corruption: Show what gets corrupted and how
- For race conditions: Demonstrate the race window and impact
- For logic bugs: Show the violated assumption and consequence
- For injection: Show unsanitized input reaching a dangerous sink
- For integer issues: Show the calculation and overflow/underflow

**Step 2: Trigger Requirements**
Document EXACTLY how to trigger the issue:
- Specific input values or sequences
- Timing requirements (for races)
- State requirements (what must be true before trigger)
- Environmental requirements (permissions, config, etc.)

**Step 3: Constraint Analysis**
Document ALL constraints:
- Input format and size requirements
- Authentication/permission requirements
- Timing windows and reliability
- Platform or version dependencies
- Required preconditions or program state

**Step 4: Safety Mechanism Analysis**
Identify anything that prevents exploitation:
- Input validation or sanitization
- Bounds checking or size limits
- Synchronization mechanisms (for races)
- Compiler protections (stack canaries, FORTIFY_SOURCE)
- OS protections (ASLR, DEP, sandboxing)

**Step 5: Exploitability Assessment**
Determine what primitives this gives an attacker:
- Information disclosure (what can be leaked?)
- Memory corruption (arbitrary write? limited write?)
- Code execution potential
- Privilege escalation possibility
- Denial of service only

If you cannot provide concrete evidence and a reliable trigger, state "This vulnerability is UNPROVEN" and either:
1. Return to Phase 1 for more analysis
2. Pivot to a different potential vulnerability)";
    templates.push_back(vuln_template_phase2);

    TaskTemplate vuln_template_phase3;
    vuln_template_phase3.name = "Finding Vulnerabilities - Phase 3";
    vuln_template_phase3.description = "Build proof-of-concept exploit";
    vuln_template_phase3.task = R"(You've PROVEN a vulnerability exists. Now create a proof of concept.

**Prerequisites (must be completed):**
- [ ] Concrete evidence the vulnerability exists
- [ ] Reliable trigger conditions documented
- [ ] Understanding of the impact/primitives gained
- [ ] Identification of any reliability issues

**Step 1: Minimal Trigger**
Create the simplest input that demonstrates the issue:
- Remove all unnecessary complexity
- Document why each part of the input is necessary
- Explain what happens at each step

**Step 2: Exploitation Strategy**
Choose and document your approach based on the vulnerability type:
- Memory corruption: What do you overwrite and why?
- Race condition: How do you win the race reliably?
- Logic bug: What assumption do you violate?
- Injection: What payload do you inject?
- Info leak: What sensitive data can you extract?

**Step 3: Proof of Concept Code**
Provide working code with:
- Setup phase (preparing environment/state)
- Trigger phase (exploiting the vulnerability)
- Verification phase (proving it worked)
- Clear comments explaining each step

**Step 4: Verification Instructions**
Document how to verify the PoC:
- How to compile/run it
- What output indicates success
- What debugging would show
- Expected behavior (crash, leak, execution, etc.)

**Step 5: Limitations and Reliability**
Be honest about:
- Success rate and reliability
- Platform/version dependencies
- Conditions where this fails
- Distance from full weaponization

Remember: A PoC must demonstrate actual unintended behavior. Simply calling an API with unusual inputs is not a vulnerability unless it causes security-relevant misbehavior.)";
    templates.push_back(vuln_template_phase3);

    // Crypto identification
    TaskTemplate crypto_template;
    crypto_template.name = "Identify Cryptography";
    crypto_template.description = "Find and identify cryptographic routines";
    crypto_template.task = "Identify cryptographic algorithms and routines in this binary. "
                          "Look for:\n"
                          "- Encryption/decryption functions\n"
                          "- Hash functions\n"
                          "- Key generation or management\n"
                          "- Common crypto constants";
    templates.push_back(crypto_template);

    // String decoding
    TaskTemplate string_template;
    string_template.name = "Decode Strings";
    string_template.description = "Find and decode obfuscated strings";
    string_template.task = "Find obfuscated or encoded strings in the binary and attempt to decode them. "
                          "Look for string decoding routines and analyze their output.";
    templates.push_back(string_template);

    // Control flow
    TaskTemplate flow_template;
    flow_template.name = "Analyze Control Flow";
    flow_template.description = "Analyze complex control flow";
    flow_template.task = "Analyze the control flow starting from {address}. "
                        "Identify:\n"
                        "- Main execution paths\n"
                        "- Conditional branches and their purposes\n"
                        "- Loops and their termination conditions\n"
                        "- Error handling paths";
    flow_template.variables["address"] = "current_ea";
    templates.push_back(flow_template);

    // Update UI
    template_list->clear();
    for (const auto& tmpl : templates) {
        template_list->addItem(QString::fromStdString(tmpl.name));
    }
}

void TaskTemplateWidget::save_templates() {
    // Save custom templates to settings
    QSettings settings("llm_re", "templates");
    settings.beginWriteArray("templates");

    for (size_t i = 0; i < templates.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", QString::fromStdString(templates[i].name));
        settings.setValue("description", QString::fromStdString(templates[i].description));
        settings.setValue("task", QString::fromStdString(templates[i].task));

        // Save variables
        settings.beginWriteArray("variables");
        int j = 0;
        for (const auto& [key, value] : templates[i].variables) {
            settings.setArrayIndex(j++);
            settings.setValue("key", QString::fromStdString(key));
            settings.setValue("value", QString::fromStdString(value));
        }
        settings.endArray();
    }

    settings.endArray();
}

void TaskTemplateWidget::on_template_selected() {
    int index = template_list->currentRow();
    if (index < 0 || index >= templates.size()) {
        template_preview->clear();
        use_button->setEnabled(false);
        edit_button->setEnabled(false);
        delete_button->setEnabled(false);
        return;
    }

    const auto& tmpl = templates[index];

    QString preview = QString("<h3>%1</h3>").arg(QString::fromStdString(tmpl.name));
    preview += QString("<p><i>%1</i></p>").arg(QString::fromStdString(tmpl.description));
    preview += "<hr>";
    preview += QString("<pre>%1</pre>").arg(QString::fromStdString(tmpl.task));

    if (!tmpl.variables.empty()) {
        preview += "<hr><p><b>Variables:</b></p><ul>";
        for (const auto& [key, value] : tmpl.variables) {
            preview += QString("<li>{%1} = %2</li>")
                .arg(QString::fromStdString(key))
                .arg(QString::fromStdString(value));
        }
        preview += "</ul>";
    }

    template_preview->setHtml(preview);
    use_button->setEnabled(true);
    edit_button->setEnabled(true);
    delete_button->setEnabled(true);
}

void TaskTemplateWidget::on_use_template() {
    int index = template_list->currentRow();
    if (index >= 0 && index < templates.size()) {
        emit template_selected(templates[index]);

        // Close the parent dialog if it exists
        QDialog* parent_dialog = qobject_cast<QDialog*>(window());
        if (parent_dialog) {
            parent_dialog->accept();
        }
    }
}

void TaskTemplateWidget::on_edit_template() {
    // TODO: Implement template editor dialog
}

void TaskTemplateWidget::on_new_template() {
    // TODO: Implement new template dialog
}

void TaskTemplateWidget::on_delete_template() {
    int index = template_list->currentRow();
    if (index >= 0 && index < templates.size()) {
        if (QMessageBox::question(this, "Delete Template",
                                QString("Delete template '%1'?")
                                    .arg(QString::fromStdString(templates[index].name)),
                                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            templates.erase(templates.begin() + index);
            save_templates();
            load_templates();
        }
    }
}

MemoryDockWidget::MemoryDockWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    tabs_ = new QTabWidget();
    layout->addWidget(tabs_);

    // Create context menu actions
    copy_action_ = new QAction("Copy", this);
    copy_action_->setShortcut(QKeySequence::Copy);
    connect(copy_action_, &QAction::triggered, this, &MemoryDockWidget::on_copy_analysis);

    export_action_ = new QAction("Export...", this);
    connect(export_action_, &QAction::triggered, this, &MemoryDockWidget::on_export_analysis);

    goto_address_action_ = new QAction("Go to Address", this);
    goto_address_action_->setShortcut(Qt::Key_G);
    connect(goto_address_action_, &QAction::triggered, this, &MemoryDockWidget::on_goto_address);

    // Tab 1: Timeline View
    QWidget* timeline_tab = new QWidget();
    QVBoxLayout* timeline_layout = new QVBoxLayout(timeline_tab);

    // Timeline filters
    QHBoxLayout* timeline_filter_layout = new QHBoxLayout();
    timeline_filter_layout->addWidget(new QLabel("Type:"));
    timeline_filter_ = new QComboBox();
    timeline_filter_->addItems({"All", "Note", "Finding", "Hypothesis", "Question", "Analysis", "Deep Analysis"});
    connect(timeline_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDockWidget::on_timeline_filter_changed);
    timeline_filter_layout->addWidget(timeline_filter_);

    timeline_filter_layout->addWidget(new QLabel("From:"));
    date_from_ = new QDateEdit(QDate::currentDate().addDays(-7));
    date_from_->setCalendarPopup(true);
    connect(date_from_, &QDateEdit::dateChanged, this, &MemoryDockWidget::on_timeline_filter_changed);
    timeline_filter_layout->addWidget(date_from_);

    timeline_filter_layout->addWidget(new QLabel("To:"));
    date_to_ = new QDateEdit(QDate::currentDate());
    date_to_->setCalendarPopup(true);
    connect(date_to_, &QDateEdit::dateChanged, this, &MemoryDockWidget::on_timeline_filter_changed);
    timeline_filter_layout->addWidget(date_to_);

    timeline_filter_layout->addStretch();
    timeline_layout->addLayout(timeline_filter_layout);

    // Timeline tree
    QSplitter* timeline_splitter = new QSplitter(Qt::Vertical);

    timeline_tree_ = new QTreeWidget();
    timeline_tree_->setHeaderLabels({"Time", "Type", "Preview", "Function"});
    timeline_tree_->setSortingEnabled(true);
    timeline_tree_->setAlternatingRowColors(true);
    timeline_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(timeline_tree_, &QTreeWidget::customContextMenuRequested,
            this, &MemoryDockWidget::on_context_menu);
    connect(timeline_tree_, &QTreeWidget::currentItemChanged,
            this, &MemoryDockWidget::on_timeline_item_selected);
    timeline_splitter->addWidget(timeline_tree_);

    timeline_viewer_ = new QTextEdit();
    timeline_viewer_->setReadOnly(true);
    timeline_splitter->addWidget(timeline_viewer_);

    timeline_layout->addWidget(timeline_splitter);
    tabs_->addTab(timeline_tab, "Timeline");

    // Tab 2: Function View
    QWidget* function_tab = new QWidget();
    QVBoxLayout* function_layout = new QVBoxLayout(function_tab);

    // Function search
    QHBoxLayout* function_search_layout = new QHBoxLayout();
    function_search_ = new QLineEdit();
    function_search_->setPlaceholderText("Search functions...");
    connect(function_search_, &QLineEdit::textChanged,
            this, &MemoryDockWidget::on_function_search_changed);
    function_search_layout->addWidget(function_search_);

    expand_all_ = new QPushButton("Expand All");
    connect(expand_all_, &QPushButton::clicked, [this]() { function_tree_->expandAll(); });
    function_search_layout->addWidget(expand_all_);

    collapse_all_ = new QPushButton("Collapse All");
    connect(collapse_all_, &QPushButton::clicked, [this]() { function_tree_->collapseAll(); });
    function_search_layout->addWidget(collapse_all_);

    function_layout->addLayout(function_search_layout);

    // Function tree
    QSplitter* function_splitter = new QSplitter(Qt::Vertical);

    function_tree_ = new QTreeWidget();
    function_tree_->setHeaderLabels({"Function", "Count", "Latest"});
    function_tree_->setSortingEnabled(true);
    function_tree_->setAlternatingRowColors(true);
    function_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(function_tree_, &QTreeWidget::customContextMenuRequested,
            this, &MemoryDockWidget::on_context_menu);
    connect(function_tree_, &QTreeWidget::currentItemChanged,
            this, &MemoryDockWidget::on_function_item_selected);
    function_splitter->addWidget(function_tree_);

    function_viewer_ = new QTextEdit();
    function_viewer_->setReadOnly(true);
    function_splitter->addWidget(function_viewer_);

    function_layout->addWidget(function_splitter);
    tabs_->addTab(function_tab, "By Function");

    // Tab 3: Analysis Browser
    QWidget* browser_tab = new QWidget();
    QVBoxLayout* browser_layout = new QVBoxLayout(browser_tab);

    // Search and filters
    QHBoxLayout* browser_filter_layout = new QHBoxLayout();

    search_edit_ = new QLineEdit();
    search_edit_->setPlaceholderText("Search analyses...");
    connect(search_edit_, &QLineEdit::textChanged,
            this, &MemoryDockWidget::on_analysis_search_changed);
    browser_filter_layout->addWidget(search_edit_);

    type_filter_ = new QComboBox();
    type_filter_->addItems({"All Types", "Note", "Finding", "Hypothesis", "Question", "Analysis", "Deep Analysis"});
    connect(type_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDockWidget::on_analysis_search_changed);
    browser_filter_layout->addWidget(type_filter_);

    sort_by_ = new QComboBox();
    sort_by_->addItems({"Newest First", "Oldest First", "Type", "Address"});
    connect(sort_by_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDockWidget::on_sort_changed);
    browser_filter_layout->addWidget(sort_by_);

    browser_layout->addLayout(browser_filter_layout);

    // Analysis info label
    analysis_info_label_ = new QLabel("0 analyses");
    analysis_info_label_->setStyleSheet("padding: 5px; background-color: #f0f0f0;");
    browser_layout->addWidget(analysis_info_label_);

    // Analysis list and viewer
    QSplitter* browser_splitter = new QSplitter(Qt::Vertical);

    analysis_list_ = new QListWidget();
    analysis_list_->setAlternatingRowColors(true);
    analysis_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(analysis_list_, &QListWidget::customContextMenuRequested,
            this, &MemoryDockWidget::on_context_menu);
    connect(analysis_list_, &QListWidget::currentRowChanged,
            this, &MemoryDockWidget::on_analysis_item_selected);
    browser_splitter->addWidget(analysis_list_);

    analysis_viewer_ = new QTextEdit();
    analysis_viewer_->setReadOnly(true);
    browser_splitter->addWidget(analysis_viewer_);

    browser_layout->addWidget(browser_splitter);
    tabs_->addTab(browser_tab, "Browse");

    // Tab 4: Relationships (simplified for now)
    QWidget* relationship_tab = new QWidget();
    QVBoxLayout* relationship_layout = new QVBoxLayout(relationship_tab);

    relationship_type_ = new QComboBox();
    relationship_type_->addItems({"All Relationships", "Same Function", "Related Functions", "Same Type"});
    relationship_layout->addWidget(relationship_type_);

    relationship_view_ = new QGraphicsView();
    relationship_layout->addWidget(relationship_view_);

    tabs_->addTab(relationship_tab, "Relationships");

    // Tab 5: Statistics
    QWidget* stats_tab = new QWidget();
    QVBoxLayout* stats_layout = new QVBoxLayout(stats_tab);

    refresh_stats_ = new QPushButton("Refresh Statistics");
    connect(refresh_stats_, &QPushButton::clicked, this, &MemoryDockWidget::update_statistics);
    stats_layout->addWidget(refresh_stats_);

    stats_browser_ = new QTextBrowser();
    stats_layout->addWidget(stats_browser_);

    tabs_->addTab(stats_tab, "Statistics");

    // Apply theme
    apply_theme();
}

QString MemoryDockWidget::format_analysis_preview(const AnalysisEntry& entry, int max_length) {
    QString content = QString::fromStdString(entry.content);
    // Remove newlines and extra spaces for preview
    content = content.simplified();

    if (content.length() > max_length) {
        content = content.left(max_length) + "...";
    }

    return content;
}

QString MemoryDockWidget::format_timestamp(std::time_t timestamp) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    QDateTime now = QDateTime::currentDateTime();

    qint64 secs = dt.secsTo(now);

    if (secs < 60) return "Just now";
    if (secs < 3600) return QString("%1m ago").arg(secs / 60);
    if (secs < 86400) return QString("%1h ago").arg(secs / 3600);
    if (secs < 604800) return QString("%1d ago").arg(secs / 86400);

    return dt.toString("MMM d, yyyy");
}

QString MemoryDockWidget::get_function_name(ea_t address) {
    if (address == BADADDR) return "Global";

    qstring func_name;
    if (get_func_name(&func_name, address) > 0) {
        return QString::fromStdString(func_name.c_str());
    }

    return QString("sub_%1").arg(address, 0, 16);
}

QIcon MemoryDockWidget::get_type_icon(const std::string& type) {
    // You could create actual icons, but for now use colored circles
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(get_type_color(type));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(2, 2, 12, 12);

    return QIcon(pixmap);
}

QColor MemoryDockWidget::get_type_color(const std::string& type) {
    if (type == "finding") return QColor(255, 100, 100);    // Red
    if (type == "hypothesis") return QColor(255, 200, 100); // Orange
    if (type == "question") return QColor(100, 150, 255);   // Blue
    if (type == "note") return QColor(150, 150, 150);       // Gray
    if (type == "analysis") return QColor(100, 255, 100);   // Green
    if (type.find("deep_analysis") != std::string::npos) return QColor(200, 100, 255); // Purple
    return QColor(100, 100, 100);  // Default gray
}

void MemoryDockWidget::populate_timeline_view() {
    timeline_tree_->clear();
    if (!memory_) return;

    std::string type_filter = timeline_filter_->currentText().toLower().toStdString();
    if (type_filter == "all") type_filter = "";

    auto analyses = memory_->get_analysis("", std::nullopt, type_filter, "");

    // Filter by date
    QDateTime from = QDateTime(date_from_->date()).toUTC();
    QDateTime to = QDateTime(date_to_->date().addDays(1)).toUTC();

    for (const auto& entry : analyses) {
        QDateTime entry_time = QDateTime::fromSecsSinceEpoch(entry.timestamp);
        if (entry_time < from || entry_time > to) continue;

        QTreeWidgetItem* item = new QTreeWidgetItem(timeline_tree_);
        item->setText(0, format_timestamp(entry.timestamp));
        item->setText(1, QString::fromStdString(entry.type));
        item->setText(2, format_analysis_preview(entry));

        if (entry.address) {
            item->setText(3, get_function_name(*entry.address));
        } else if (!entry.related_addresses.empty()) {
            item->setText(3, QString("%1 functions").arg(entry.related_addresses.size()));
        }

        item->setIcon(1, get_type_icon(entry.type));
        item->setData(0, Qt::UserRole, QString::fromStdString(entry.key));

        // Highlight if it's for current address
        if (current_address_ != BADADDR) {
            if ((entry.address && *entry.address == current_address_) ||
                std::find(entry.related_addresses.begin(), entry.related_addresses.end(),
                         current_address_) != entry.related_addresses.end()) {
                item->setBackground(0, QColor(255, 255, 200));
            }
        }
    }

    timeline_tree_->sortByColumn(0, Qt::DescendingOrder);
}

void MemoryDockWidget::populate_function_view() {
    function_tree_->clear();
    if (!memory_) return;

    // Group analyses by function
    std::map<ea_t, std::vector<AnalysisEntry>> by_function;
    auto all_analyses = memory_->get_analysis();

    for (const auto& entry : all_analyses) {
        if (entry.address) {
            by_function[*entry.address].push_back(entry);
        }
        for (ea_t addr : entry.related_addresses) {
            by_function[addr].push_back(entry);
        }
    }

    // Also add a "Global" category for analyses without addresses
    std::vector<AnalysisEntry> global_analyses;
    for (const auto& entry : all_analyses) {
        if (!entry.address && entry.related_addresses.empty()) {
            global_analyses.push_back(entry);
        }
    }

    // Create tree items
    QString search_text = function_search_->text().toLower();

    // Add global analyses if any
    if (!global_analyses.empty()) {
        QTreeWidgetItem* global_item = new QTreeWidgetItem(function_tree_);
        global_item->setText(0, "Global Analyses");
        global_item->setText(1, QString::number(global_analyses.size()));
        global_item->setIcon(0, get_type_icon(""));

        for (const auto& entry : global_analyses) {
            QTreeWidgetItem* child = new QTreeWidgetItem(global_item);
            child->setText(0, format_analysis_preview(entry, 80));
            child->setText(1, QString::fromStdString(entry.type));
            child->setText(2, format_timestamp(entry.timestamp));
            child->setIcon(1, get_type_icon(entry.type));
            child->setData(0, Qt::UserRole, QString::fromStdString(entry.key));
        }
    }

    // Add function-specific analyses
    for (const auto& [addr, entries] : by_function) {
        QString func_name = get_function_name(addr);

        if (!search_text.isEmpty() && !func_name.toLower().contains(search_text)) {
            continue;
        }

        QTreeWidgetItem* func_item = new QTreeWidgetItem(function_tree_);
        func_item->setText(0, func_name);
        func_item->setText(1, QString::number(entries.size()));

        // Find latest entry
        auto latest = std::max_element(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        func_item->setText(2, format_timestamp(latest->timestamp));

        // Add children for each analysis
        std::map<std::string, int> type_counts;
        for (const auto& entry : entries) {
            type_counts[entry.type]++;

            QTreeWidgetItem* child = new QTreeWidgetItem(func_item);
            child->setText(0, format_analysis_preview(entry, 80));
            child->setText(1, QString::fromStdString(entry.type));
            child->setText(2, format_timestamp(entry.timestamp));
            child->setIcon(1, get_type_icon(entry.type));
            child->setData(0, Qt::UserRole, QString::fromStdString(entry.key));
        }

        // Update function item tooltip with type breakdown
        QString tooltip = QString("Address: 0x%1\n").arg(addr, 0, 16);
        for (const auto& [type, count] : type_counts) {
            tooltip += QString("%1: %2\n").arg(QString::fromStdString(type)).arg(count);
        }
        func_item->setToolTip(0, tooltip);

        // Highlight current function
        if (addr == current_address_) {
            func_item->setBackground(0, QColor(200, 255, 200));
        }
    }

    function_tree_->sortByColumn(1, Qt::DescendingOrder);
}

void MemoryDockWidget::populate_analysis_browser() {
    analysis_list_->clear();
    if (!memory_) return;

    std::string search_pattern = search_edit_->text().toStdString();
    std::string type_filter = "";

    if (type_filter_->currentIndex() > 0) {
        type_filter = type_filter_->currentText().toLower().toStdString();
    }

    auto analyses = memory_->get_analysis("", std::nullopt, type_filter, search_pattern);

    // Sort based on selection
    switch (sort_by_->currentIndex()) {
        case 0: // Newest first
            std::sort(analyses.begin(), analyses.end(),
                [](const auto& a, const auto& b) { return a.timestamp > b.timestamp; });
            break;
        case 1: // Oldest first
            std::sort(analyses.begin(), analyses.end(),
                [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
            break;
        case 2: // Type
            std::sort(analyses.begin(), analyses.end(),
                [](const auto& a, const auto& b) { return a.type < b.type; });
            break;
        case 3: // Address
            std::sort(analyses.begin(), analyses.end(),
                [](const auto& a, const auto& b) {
                    ea_t addr_a = a.address ? *a.address : BADADDR;
                    ea_t addr_b = b.address ? *b.address : BADADDR;
                    return addr_a < addr_b;
                });
            break;
    }

    // Populate list
    for (const auto& entry : analyses) {
        QListWidgetItem* item = new QListWidgetItem(analysis_list_);

        // Format display text
        QString display = QString("[%1] ").arg(QString::fromStdString(entry.type).toUpper());
        display += format_analysis_preview(entry, 150);

        item->setText(display);
        item->setIcon(get_type_icon(entry.type));
        item->setData(Qt::UserRole, QString::fromStdString(entry.key));

        // Add tooltip with more info
        QString tooltip = QString("Key: %1\n").arg(QString::fromStdString(entry.key));
        tooltip += QString("Time: %1\n").arg(QDateTime::fromSecsSinceEpoch(entry.timestamp).toString());
        if (entry.address) {
            tooltip += QString("Address: 0x%1 (%2)").arg(*entry.address, 0, 16)
                      .arg(get_function_name(*entry.address));
        }
        item->setToolTip(tooltip);
    }

    // Update info label
    analysis_info_label_->setText(QString("%1 analyses found").arg(analyses.size()));
}

void MemoryDockWidget::update_statistics() {
    if (!memory_) return;

    auto all_analyses = memory_->get_analysis();

    // Calculate statistics
    std::map<std::string, int> type_counts;
    std::map<ea_t, int> function_counts;
    int total_count = all_analyses.size();
    std::time_t oldest = std::time(nullptr);
    std::time_t newest = 0;

    for (const auto& entry : all_analyses) {
        type_counts[entry.type]++;

        if (entry.address) {
            function_counts[*entry.address]++;
        }
        for (ea_t addr : entry.related_addresses) {
            function_counts[addr]++;
        }

        oldest = std::min(oldest, entry.timestamp);
        newest = std::max(newest, entry.timestamp);
    }

    // Find most analyzed functions
    std::vector<std::pair<ea_t, int>> top_functions;
    for (const auto& [addr, count] : function_counts) {
        top_functions.push_back({addr, count});
    }
    std::sort(top_functions.begin(), top_functions.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Generate HTML
    QString html = R"(
        <html>
        <head>
        <style>
            body { font-family: Arial, sans-serif; padding: 10px; }
            h3 { color: #2c3e50; }
            .stat-box { background: #f8f9fa; padding: 10px; margin: 5px 0; border-radius: 5px; }
            .stat-label { font-weight: bold; color: #7f8c8d; }
            .stat-value { font-size: 1.2em; color: #2c3e50; }
            table { width: 100%; border-collapse: collapse; margin: 10px 0; }
            th { background: #ecf0f1; padding: 5px; text-align: left; }
            td { padding: 5px; border-bottom: 1px solid #ecf0f1; }
            .type-note { color: #95a5a6; }
            .type-finding { color: #e74c3c; }
            .type-hypothesis { color: #f39c12; }
            .type-question { color: #3498db; }
            .type-analysis { color: #27ae60; }
            .type-deep_analysis { color: #9b59b6; }
        </style>
        </head>
        <body>
    )";

    html += "<h3>Memory Statistics</h3>";

    // Overview
    html += "<div class='stat-box'>";
    html += QString("<span class='stat-label'>Total Analyses:</span> <span class='stat-value'>%1</span><br>").arg(total_count);
    html += QString("<span class='stat-label'>Date Range:</span> %1 to %2<br>")
            .arg(QDateTime::fromSecsSinceEpoch(oldest).toString("MMM d, yyyy"))
            .arg(QDateTime::fromSecsSinceEpoch(newest).toString("MMM d, yyyy"));
    html += QString("<span class='stat-label'>Functions Analyzed:</span> %1").arg(function_counts.size());
    html += "</div>";

    // Analysis types
    html += "<h4>Analysis Types</h4>";
    html += "<table>";
    html += "<tr><th>Type</th><th>Count</th><th>Percentage</th></tr>";

    for (const auto& [type, count] : type_counts) {
        double percentage = (count * 100.0) / total_count;
        html += QString("<tr><td class='type-%1'>%2</td><td>%3</td><td>%4%</td></tr>")
                .arg(QString::fromStdString(type))
                .arg(QString::fromStdString(type).toUpper())
                .arg(count)
                .arg(percentage, 0, 'f', 1);
    }
    html += "</table>";

    // Top analyzed functions
    html += "<h4>Most Analyzed Functions</h4>";
    html += "<table>";
    html += "<tr><th>Function</th><th>Analyses</th></tr>";

    int shown = 0;
    for (const auto& [addr, count] : top_functions) {
        if (shown++ >= 10) break;

        QString func_name = get_function_name(addr);
        html += QString("<tr><td>%1</td><td>%2</td></tr>")
                .arg(func_name)
                .arg(count);
    }
    html += "</table>";

    html += "</body></html>";

    stats_browser_->setHtml(html);
}

void MemoryDockWidget::refresh_views() {
    // Refresh the current tab
    int current_tab = tabs_->currentIndex();

    switch (current_tab) {
        case 0: populate_timeline_view(); break;
        case 1: populate_function_view(); break;
        case 2: populate_analysis_browser(); break;
        case 3: build_relationship_graph(); break;
        case 4: update_statistics(); break;
    }
}

void MemoryDockWidget::apply_theme() {
    // Get theme info from main form
    MainForm* main_form = get_main_form();
    bool is_dark_theme = false;
    if (main_form) {
        const Config* config = main_form->get_config();
        is_dark_theme = (config->ui.theme == 0 || config->ui.theme == 1);
    }

    if (is_dark_theme) {
        // Dark theme adjustments
        analysis_info_label_->setStyleSheet("padding: 5px; background-color: #3c3c3c; color: #ffffff;");
    } else {
        // Light theme
        analysis_info_label_->setStyleSheet("padding: 5px; background-color: #f0f0f0; color: #000000;");
    }
}

void MemoryDockWidget::update_memory(std::shared_ptr<BinaryMemory> memory) {
    memory_ = memory;
    refresh_views();
}

void MemoryDockWidget::set_current_address(ea_t address) {
    current_address_ = address;
    refresh_views();
}

void MemoryDockWidget::build_relationship_graph() {
    // Create a simple graph showing relationships between analyses
    QGraphicsScene* scene = new QGraphicsScene();
    relationship_view_->setScene(scene);

    if (!memory_) return;

    auto all_analyses = memory_->get_analysis();

    // Group by relationship type based on combo selection
    std::map<std::string, std::vector<AnalysisEntry>> groups;

    switch (relationship_type_->currentIndex()) {
        case 0: // All relationships
            for (const auto& entry : all_analyses) {
                groups[entry.type].push_back(entry);
            }
            break;

        case 1: // Same function
            {
                std::map<ea_t, std::vector<AnalysisEntry>> by_func;
                for (const auto& entry : all_analyses) {
                    if (entry.address) {
                        by_func[*entry.address].push_back(entry);
                    }
                }
                for (const auto& [addr, entries] : by_func) {
                    if (entries.size() > 1) {
                        groups[get_function_name(addr).toStdString()] = entries;
                    }
                }
            }
            break;

        case 2: // Related functions
            for (const auto& entry : all_analyses) {
                if (entry.related_addresses.size() > 1) {
                    groups["Multi-function"].push_back(entry);
                }
            }
            break;

        case 3: // Same type
            for (const auto& entry : all_analyses) {
                groups[entry.type].push_back(entry);
            }
            break;
    }

    // Create visual representation
    int y_offset = 0;
    for (const auto& [group_name, entries] : groups) {
        // Group header
        QGraphicsTextItem* header = scene->addText(QString::fromStdString(group_name));
        header->setPos(10, y_offset);
        QFont header_font = header->font();
        header_font.setBold(true);
        header->setFont(header_font);

        y_offset += 30;

        // Draw entries in this group
        int x_offset = 30;
        for (size_t i = 0; i < entries.size() && i < 10; ++i) {
            const auto& entry = entries[i];

            // Create box for entry
            QGraphicsRectItem* box = scene->addRect(x_offset, y_offset, 150, 60);
            box->setBrush(QBrush(get_type_color(entry.type).lighter(150)));
            box->setPen(QPen(get_type_color(entry.type)));

            // Add text
            QGraphicsTextItem* text = scene->addText(format_analysis_preview(entry, 50));
            text->setPos(x_offset + 5, y_offset + 5);
            text->setTextWidth(140);

            // Draw connections if same function
            if (relationship_type_->currentIndex() == 1 && i > 0) {
                QGraphicsLineItem* line = scene->addLine(
                    x_offset - 10, y_offset + 30,
                    x_offset, y_offset + 30
                );
                line->setPen(QPen(Qt::gray, 1, Qt::DashLine));
            }

            x_offset += 170;
            if (x_offset > 800) {
                x_offset = 30;
                y_offset += 80;
            }
        }

        y_offset += 100;
    }

    relationship_view_->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void MemoryDockWidget::on_timeline_filter_changed() {
    populate_timeline_view();
}

void MemoryDockWidget::on_timeline_item_selected() {
    QList<QTreeWidgetItem*> selected = timeline_tree_->selectedItems();
    if (selected.isEmpty()) {
        timeline_viewer_->clear();
        return;
    }

    QTreeWidgetItem* item = selected.first();
    QString key = item->data(0, Qt::UserRole).toString();

    if (!memory_ || key.isEmpty()) return;

    std::vector<AnalysisEntry> analyses = memory_->get_analysis(key.toStdString());
    if (analyses.empty()) return;

    const auto& entry = analyses[0];

    // Format detailed view
    QString html = "<html><body style='font-family: Arial; padding: 10px;'>";

    // Header
    html += QString("<h3>%1</h3>").arg(QString::fromStdString(entry.type).toUpper());

    // Metadata
    html += "<div style='background: #f0f0f0; padding: 10px; margin: 10px 0; border-radius: 5px;'>";
    html += QString("<b>Key:</b> %1<br>").arg(QString::fromStdString(entry.key));
    html += QString("<b>Timestamp:</b> %1<br>").arg(
        QDateTime::fromSecsSinceEpoch(entry.timestamp).toString("yyyy-MM-dd hh:mm:ss"));

    if (entry.address) {
        html += QString("<b>Address:</b> 0x%1 (%2)<br>")
                .arg(*entry.address, 0, 16)
                .arg(get_function_name(*entry.address));
    }

    if (!entry.related_addresses.empty()) {
        html += "<b>Related Functions:</b><ul>";
        for (ea_t addr : entry.related_addresses) {
            html += QString("<li>0x%1 - %2</li>")
                    .arg(addr, 0, 16)
                    .arg(get_function_name(addr));
        }
        html += "</ul>";
    }
    html += "</div>";

    // Content
    html += "<div style='margin-top: 20px;'>";
    html += "<h4>Content:</h4>";
    html += "<pre style='background: #f8f8f8; padding: 10px; border-radius: 5px; white-space: pre-wrap;'>";
    html += QString::fromStdString(entry.content).toHtmlEscaped();
    html += "</pre>";
    html += "</div>";

    html += "</body></html>";

    timeline_viewer_->setHtml(html);
}

void MemoryDockWidget::on_function_search_changed() {
    populate_function_view();
}

void MemoryDockWidget::on_function_item_selected() {
    QList<QTreeWidgetItem*> selected = function_tree_->selectedItems();
    if (selected.isEmpty()) {
        function_viewer_->clear();
        return;
    }

    QTreeWidgetItem* item = selected.first();

    // Check if it's a child item (specific analysis)
    if (item->parent()) {
        QString key = item->data(0, Qt::UserRole).toString();
        if (!memory_ || key.isEmpty()) return;

        auto analyses = memory_->get_analysis(key.toStdString());
        if (!analyses.empty()) {
            const auto& entry = analyses[0];

            QString content = QString::fromStdString(entry.content);
            function_viewer_->setPlainText(content);
        }
    } else {
        // It's a function item - show summary
        QString html = "<html><body style='font-family: Arial; padding: 10px;'>";
        html += QString("<h3>%1</h3>").arg(item->text(0));
        html += QString("<p>Total analyses: %1</p>").arg(item->text(1));
        html += QString("<p>Latest: %1</p>").arg(item->text(2));

        // Show breakdown by type
        std::map<std::string, int> type_counts;
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            std::string type = child->text(1).toStdString();
            type_counts[type]++;
        }

        html += "<h4>Analysis Types:</h4><ul>";
        for (const auto& [type, count] : type_counts) {
            html += QString("<li>%1: %2</li>")
                    .arg(QString::fromStdString(type))
                    .arg(count);
        }
        html += "</ul>";

        html += "</body></html>";
        function_viewer_->setHtml(html);
    }
}

void MemoryDockWidget::on_analysis_search_changed() {
    populate_analysis_browser();
}

void MemoryDockWidget::on_analysis_item_selected() {
    QList<QListWidgetItem*> selected = analysis_list_->selectedItems();
    if (selected.isEmpty()) {
        analysis_viewer_->clear();
        return;
    }

    QListWidgetItem* item = selected.first();
    QString key = item->data(Qt::UserRole).toString();

    if (!memory_ || key.isEmpty()) return;

    auto analyses = memory_->get_analysis(key.toStdString());
    if (analyses.empty()) return;

    const auto& entry = analyses[0];

    // Format for viewer with syntax highlighting if applicable
    QString content = QString::fromStdString(entry.content);

    // Check if it looks like code or structured data
    if (entry.type == "analysis" || content.contains("```") ||
        content.contains("function") || content.contains("0x")) {

        // Apply basic formatting
        content.replace(QRegExp("(0x[0-9a-fA-F]+)"), "<span style='color: #0066cc;'>\\1</span>");
        content.replace(QRegExp("\\b(function|if|else|for|while|return)\\b"),
                       "<span style='color: #ff6600; font-weight: bold;'>\\1</span>");

        QString html = "<html><body style='font-family: Consolas, monospace; padding: 10px;'>";
        html += "<pre style='white-space: pre-wrap;'>" + content + "</pre>";
        html += "</body></html>";

        analysis_viewer_->setHtml(html);
    } else {
        analysis_viewer_->setPlainText(content);
    }
}

void MemoryDockWidget::on_sort_changed() {
    populate_analysis_browser();
}

void MemoryDockWidget::on_context_menu(const QPoint& pos) {
    QMenu menu(this);

    // Determine which widget triggered the menu
    QWidget* sender_widget = qobject_cast<QWidget*>(sender());

    // Get selected item data
    QString selected_key;
    ea_t selected_address = BADADDR;

    if (sender_widget == timeline_tree_) {
        QTreeWidgetItem* item = timeline_tree_->itemAt(pos);
        if (item) {
            selected_key = item->data(0, Qt::UserRole).toString();
        }
    } else if (sender_widget == function_tree_) {
        QTreeWidgetItem* item = function_tree_->itemAt(pos);
        if (item && item->parent()) {  // Only for analysis items, not function headers
            selected_key = item->data(0, Qt::UserRole).toString();
        }
    } else if (sender_widget == analysis_list_) {
        QListWidgetItem* item = analysis_list_->itemAt(pos);
        if (item) {
            selected_key = item->data(Qt::UserRole).toString();
        }
    }

    if (!selected_key.isEmpty() && memory_) {
        auto analyses = memory_->get_analysis(selected_key.toStdString());
        if (!analyses.empty() && analyses[0].address) {
            selected_address = *analyses[0].address;
        }
    }

    // Add actions
    menu.addAction(copy_action_);
    menu.addAction(export_action_);
    menu.addSeparator();

    if (selected_address != BADADDR) {
        menu.addAction(goto_address_action_);
        menu.addSeparator();
    }

    // Store selected data for action handlers
    copy_action_->setData(selected_key);
    export_action_->setData(selected_key);
    goto_address_action_->setData(QVariant::fromValue(selected_address));

    menu.exec(sender_widget->mapToGlobal(pos));
}

void MemoryDockWidget::on_copy_analysis() {
    QString key = copy_action_->data().toString();
    if (key.isEmpty() || !memory_) return;

    auto analyses = memory_->get_analysis(key.toStdString());
    if (analyses.empty()) return;

    const auto& entry = analyses[0];

    // Format for clipboard
    QString text = QString("=== %1 ===\n").arg(QString::fromStdString(entry.type).toUpper());
    text += QString("Key: %1\n").arg(QString::fromStdString(entry.key));
    text += QString("Time: %1\n").arg(
        QDateTime::fromSecsSinceEpoch(entry.timestamp).toString("yyyy-MM-dd hh:mm:ss"));

    if (entry.address) {
        text += QString("Address: 0x%1\n").arg(*entry.address, 0, 16);
    }

    text += "\n" + QString::fromStdString(entry.content);

    QApplication::clipboard()->setText(text);

    // Show brief notification (would need to implement or use status bar)
    msg("Analysis copied to clipboard\n");
}

void MemoryDockWidget::on_export_analysis() {
    QString key = export_action_->data().toString();
    if (key.isEmpty() || !memory_) return;

    // Show file dialog
    QString filename = QFileDialog::getSaveFileName(this,
        "Export Analysis", "", "Markdown Files (*.md);;Text Files (*.txt);;All Files (*)");

    if (filename.isEmpty()) return;

    auto analyses = memory_->get_analysis(key.toStdString());
    if (analyses.empty()) return;

    const auto& entry = analyses[0];

    // Write to file
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);

        // Write markdown format
        stream << "# " << QString::fromStdString(entry.type).toUpper() << "\n\n";
        stream << "**Key:** " << QString::fromStdString(entry.key) << "\n";
        stream << "**Date:** " << QDateTime::fromSecsSinceEpoch(entry.timestamp).toString() << "\n";

        if (entry.address) {
            stream << "**Address:** 0x" << QString::number(*entry.address, 16) << "\n";
            stream << "**Function:** " << get_function_name(*entry.address) << "\n";
        }

        if (!entry.related_addresses.empty()) {
            stream << "\n## Related Functions\n";
            for (ea_t addr : entry.related_addresses) {
                stream << "- 0x" << QString::number(addr, 16) << " - "
                       << get_function_name(addr) << "\n";
            }
        }

        stream << "\n## Content\n\n";
        stream << QString::fromStdString(entry.content);

        file.close();
        msg("Exported analysis to %s\n", filename.toStdString().c_str());
    }
}

void MemoryDockWidget::on_goto_address() {
    ea_t address = goto_address_action_->data().value<ea_t>();
    if (address != BADADDR) {
        jumpto(address);
        emit address_selected(address);
    }
}

} // namespace llm_re::ui