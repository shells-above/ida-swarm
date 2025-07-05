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
            j["export"]["auto_export"] = export_settings.auto_export;
            j["export"]["format"] = export_settings.format;
            j["export"]["include_memory"] = export_settings.include_memory;
            j["export"]["include_logs"] = export_settings.include_logs;

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
                export_settings.auto_export = j["export"].value("auto_export", export_settings.auto_export);
                export_settings.format = j["export"].value("format", export_settings.format);
                export_settings.include_memory = j["export"].value("include_memory", export_settings.include_memory);
                export_settings.include_logs = j["export"].value("include_logs", export_settings.include_logs);
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
    format_combo->addItems({"Markdown", "HTML", "JSON", "PDF"});
    connect(format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::on_format_changed);
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
    bool enable_template = (index == Markdown || index == HTML);
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

    auto_export_check = new QCheckBox("Auto-export sessions");
    export_layout->addRow("", auto_export_check);

    export_format_combo = new QComboBox();
    export_format_combo->addItems({"Markdown", "HTML", "JSON"});
    export_layout->addRow("Default Format:", export_format_combo);

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
    auto_export_check->setChecked(config.export_settings.auto_export);
    export_format_combo->setCurrentIndex(config.export_settings.format);

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
    config.export_settings.auto_export = auto_export_check->isChecked();
    config.export_settings.format = export_format_combo->currentIndex();

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


CallGraphWidget::CallGraphWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(400, 400);

    // Set up for smooth rendering
    setAttribute(Qt::WA_NoSystemBackground);
}

void CallGraphWidget::update_graph(std::shared_ptr<BinaryMemory> memory) {
    memory_ = memory;
    nodes_.clear();
    edges_.clear();

    if (!memory_) return;

    // Get all analyzed functions
    auto functions = memory_->get_analyzed_functions();
    json snapshot = memory_->export_memory_snapshot();

    // Create a map for quick lookup
    std::map<ea_t, int> addr_to_node_index;

    // Create nodes
    for (const auto& [addr, name, level] : functions) {
        Node node;
        node.address = addr;
        node.name = QString::fromStdString(name.empty() ? format_address(addr) : name);
        node.level = static_cast<int>(level);
        node.is_anchor = memory_->is_anchor_point(addr);
        node.is_focus = (addr == memory_->get_current_focus());
        node.position = QPointF(0, 0);

        addr_to_node_index[addr] = nodes_.size();
        nodes_.push_back(node);
    }

    // Create edges from the snapshot data
    for (const auto& func : snapshot["functions"]) {
        // Parse the from address
        ea_t from_addr = 0;
        std::string from_str = func["address"].get<std::string>();
        if (from_str.find("0x") == 0) {
            from_addr = std::stoull(from_str.substr(2), nullptr, 16);
        } else {
            from_addr = std::stoull(from_str, nullptr, 16);
        }

        // Only process if this node exists
        if (addr_to_node_index.find(from_addr) == addr_to_node_index.end()) {
            continue;
        }

        // Add edges for callees
        if (func.contains("callees") && func["callees"].is_array()) {
            for (const auto& callee_json : func["callees"]) {
                std::string callee_str = callee_json.get<std::string>();
                ea_t to_addr = 0;
                if (callee_str.find("0x") == 0) {
                    to_addr = std::stoull(callee_str.substr(2), nullptr, 16);
                } else {
                    to_addr = std::stoull(callee_str, nullptr, 16);
                }

                // Only add edge if target node exists
                if (addr_to_node_index.find(to_addr) != addr_to_node_index.end()) {
                    edges_.push_back({from_addr, to_addr});
                }
            }
        }
    }

    // If we have nodes but no edges, create a simple layout
    if (!nodes_.empty() && edges_.empty()) {
        // Just arrange nodes in a grid
        int cols = std::ceil(std::sqrt(nodes_.size()));
        for (size_t i = 0; i < nodes_.size(); i++) {
            int row = i / cols;
            int col = i % cols;
            nodes_[i].position = QPointF(col * 100 - cols * 50, row * 100);
        }
    } else {
        // Use force-directed layout
        layout_graph();
    }

    update();
}

void CallGraphWidget::layout_graph() {
    if (nodes_.empty()) return;

    // Simple force-directed layout
    const int iterations = 100;
    const double k = 50.0;  // Ideal edge length
    const double c_rep = 10000.0;  // Repulsion constant
    const double c_spring = 0.1;  // Spring constant

    // Initialize positions randomly
    for (auto& node : nodes_) {
        node.position = QPointF(
            (rand() % 1000) - 500,
            (rand() % 1000) - 500
        );
    }

    // Force-directed iterations
    for (int iter = 0; iter < iterations; iter++) {
        std::vector<QPointF> forces(nodes_.size(), QPointF(0, 0));

        // Repulsive forces between all nodes
        for (size_t i = 0; i < nodes_.size(); i++) {
            for (size_t j = i + 1; j < nodes_.size(); j++) {
                QPointF delta = nodes_[i].position - nodes_[j].position;
                double dist = std::max(1.0f, QVector2D(delta).length());
                QPointF force = (c_rep / (dist * dist)) * delta / dist;

                forces[i] += force;
                forces[j] -= force;
            }
        }

        // Spring forces for edges
        for (const auto& edge : edges_) {
            // Find node indices
            int from_idx = -1, to_idx = -1;
            for (size_t i = 0; i < nodes_.size(); i++) {
                if (nodes_[i].address == edge.from) from_idx = i;
                if (nodes_[i].address == edge.to) to_idx = i;
            }

            if (from_idx >= 0 && to_idx >= 0) {
                QPointF delta = nodes_[to_idx].position - nodes_[from_idx].position;
                double dist = QVector2D(delta).length();
                double force_mag = c_spring * (dist - k);
                QPointF force = force_mag * delta / std::max(1.0, dist);

                forces[from_idx] += force;
                forces[to_idx] -= force;
            }
        }

        // Apply forces with damping
        double damping = 0.85;
        for (size_t i = 0; i < nodes_.size(); i++) {
            nodes_[i].position += forces[i] * damping;
        }
    }

    // Center the graph
    QPointF center(0, 0);
    for (const auto& node : nodes_) {
        center += node.position;
    }
    center /= nodes_.size();

    for (auto& node : nodes_) {
        node.position -= center;
    }
}

void CallGraphWidget::center_on_function(ea_t address) {
    for (const auto& node : nodes_) {
        if (node.address == address) {
            offset_ = -node.position;
            update();
            break;
        }
    }
}

void CallGraphWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get theme
    MainForm* main_form = get_main_form();
    bool is_dark_theme = false;
    if (main_form) {
        const Config* config = main_form->get_config();
        is_dark_theme = (config->ui.theme == 0 || config->ui.theme == 1);
    }

    // Background
    painter.fillRect(rect(), is_dark_theme ? QColor(40, 40, 40) : QColor(250, 250, 250));

    // Set up transformation
    painter.translate(width() / 2 + offset_.x(), height() / 2 + offset_.y());
    painter.scale(zoom_, zoom_);

    // Draw edges
    painter.setPen(QPen(is_dark_theme ? QColor(100, 100, 100) : QColor(200, 200, 200), 1));
    for (const auto& edge : edges_) {
        QPointF from_pos, to_pos;

        // Find positions
        for (const auto& node : nodes_) {
            if (node.address == edge.from) from_pos = node.position;
            if (node.address == edge.to) to_pos = node.position;
        }

        // Draw arrow
        painter.drawLine(from_pos, to_pos);

        // Arrowhead
        QLineF line(from_pos, to_pos);
        double angle = std::atan2(line.dy(), line.dx());
        QPointF arrow_p1 = to_pos - QPointF(cos(angle - M_PI/6) * 10, sin(angle - M_PI/6) * 10);
        QPointF arrow_p2 = to_pos - QPointF(cos(angle + M_PI/6) * 10, sin(angle + M_PI/6) * 10);
        painter.drawLine(to_pos, arrow_p1);
        painter.drawLine(to_pos, arrow_p2);
    }

    // Draw nodes
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    for (const auto& node : nodes_) {
        // Node color based on analysis level
        QColor node_color;
        switch (node.level) {
            case 1: node_color = QColor(180, 180, 180); break;  // Summary
            case 2: node_color = QColor(150, 200, 255); break;  // Contextual
            case 3: node_color = QColor(100, 255, 100); break;  // Analytical
            case 4: node_color = QColor(255, 200, 100); break;  // Comprehensive
            default: node_color = QColor(200, 200, 200);
        }

        // Special highlighting
        if (node.is_focus) {
            painter.setPen(QPen(Qt::red, 3));
        } else if (node.is_anchor) {
            painter.setPen(QPen(QColor(255, 200, 0), 3));
        } else {
            painter.setPen(QPen(is_dark_theme ? Qt::white : Qt::black, 1));
        }

        // Draw node circle
        painter.setBrush(node_color);
        painter.drawEllipse(node.position, 20, 20);

        // Draw label
        painter.setPen(is_dark_theme ? Qt::white : Qt::black);
        QRectF text_rect(node.position.x() - 50, node.position.y() + 25, 100, 20);
        painter.drawText(text_rect, Qt::AlignCenter, node.name);
    }
}

void CallGraphWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Transform mouse position to graph coordinates
        QPointF graph_pos = (event->pos() - QPointF(width()/2, height()/2) - offset_) / zoom_;

        Node* clicked_node = node_at_point(graph_pos);
        if (clicked_node) {
            emit node_clicked(clicked_node->address);
        }
    }
}

void CallGraphWidget::wheelEvent(QWheelEvent* event) {
    // Zoom with mouse wheel
    double zoom_factor = 1.15;
    if (event->angleDelta().y() > 0) {
        zoom_ *= zoom_factor;
    } else {
        zoom_ /= zoom_factor;
    }

    // Limit zoom
    zoom_ = std::max(0.1, std::min(5.0, zoom_));
    update();
}

CallGraphWidget::Node* CallGraphWidget::node_at_point(const QPointF& point) {
    const double node_radius = 20.0;

    for (auto& node : nodes_) {
        QPointF delta = point - node.position;
        if (QVector2D(delta).length() <= node_radius) {
            return &node;
        }
    }

    return nullptr;
}

// Helper function to parse hex addresses
ea_t CallGraphWidget::parse_hex_address(const std::string& hex_str) {
    // Remove "0x" prefix if present
    std::string clean_str = hex_str;
    if (clean_str.find("0x") == 0) {
        clean_str = clean_str.substr(2);
    }

    return std::stoull(clean_str, nullptr, 16);
}


MemoryDockWidget::MemoryDockWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    tabs_ = new QTabWidget();
    layout->addWidget(tabs_);

    // Tab 1: Function Overview
    QWidget* overview_tab = new QWidget();
    QVBoxLayout* overview_layout = new QVBoxLayout(overview_tab);

    // Filter controls
    QHBoxLayout* filter_layout = new QHBoxLayout();
    filter_layout->addWidget(new QLabel("Filter:"));

    function_filter_ = new QLineEdit();
    function_filter_->setPlaceholderText("Search functions...");
    connect(function_filter_, &QLineEdit::textChanged, this, &MemoryDockWidget::on_filter_changed);
    filter_layout->addWidget(function_filter_);

    level_filter_ = new QComboBox();
    level_filter_->addItems({"All Levels", "Summary", "Contextual", "Analytical", "Comprehensive"});
    connect(level_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDockWidget::on_filter_changed);
    filter_layout->addWidget(level_filter_);

    overview_layout->addLayout(filter_layout);

    // Create splitter for tree and analysis
    QSplitter* overview_splitter = new QSplitter(Qt::Vertical);

    // Function tree
    function_tree_ = new QTreeWidget();
    function_tree_->setHeaderLabels({"Function", "Level", "Callers", "Callees", "Strings", "Status"});
    function_tree_->setAlternatingRowColors(true);
    function_tree_->setSortingEnabled(true);
    connect(function_tree_, &QTreeWidget::itemSelectionChanged,
            this, &MemoryDockWidget::on_function_selected);
    connect(function_tree_, &QTreeWidget::itemDoubleClicked,
            [this](QTreeWidgetItem* item) {
                ea_t addr = item->data(0, Qt::UserRole).toULongLong();
                emit address_clicked(addr);
            });
    overview_splitter->addWidget(function_tree_);

    // Add analysis viewer
    function_analysis_viewer_ = new QTextEdit();
    function_analysis_viewer_->setReadOnly(true);
    function_analysis_viewer_->setMaximumHeight(200);
    overview_splitter->addWidget(function_analysis_viewer_);

    overview_layout->addWidget(overview_splitter);
    tabs_->addTab(overview_tab, "Functions");

    // Tab 2: Call Graph
    call_graph_ = new CallGraphWidget();
    connect(call_graph_, &CallGraphWidget::node_clicked,
            this, &MemoryDockWidget::function_selected);
    tabs_->addTab(call_graph_, "Call Graph");

    // Tab 3: Insights & Notes
    QWidget* insights_tab = new QWidget();
    QVBoxLayout* insights_layout = new QVBoxLayout(insights_tab);

    QHBoxLayout* insight_filter_layout = new QHBoxLayout();
    insight_filter_layout->addWidget(new QLabel("Type:"));
    insight_filter_ = new QComboBox();
    insight_filter_->addItems({"All", "Pattern", "Hypothesis", "Question", "Finding"});
    connect(insight_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDockWidget::refresh_views);
    insight_filter_layout->addWidget(insight_filter_);
    insight_filter_layout->addStretch();
    insights_layout->addLayout(insight_filter_layout);

    QSplitter* insights_splitter = new QSplitter(Qt::Vertical);

    insights_list_ = new QListWidget();
    connect(insights_list_, &QListWidget::currentRowChanged,
            this, &MemoryDockWidget::on_insight_selected);
    insights_splitter->addWidget(insights_list_);

    notes_viewer_ = new QTextEdit();
    notes_viewer_->setReadOnly(true);
    insights_splitter->addWidget(notes_viewer_);

    insights_layout->addWidget(insights_splitter);
    tabs_->addTab(insights_tab, "Insights");

    // Tab 4: Analysis Queue
    QWidget* queue_tab = new QWidget();
    QVBoxLayout* queue_layout = new QVBoxLayout(queue_tab);

    queue_table_ = new QTableWidget();
    queue_table_->setColumnCount(4);
    queue_table_->setHorizontalHeaderLabels({"Address", "Function", "Reason", "Priority"});
    queue_table_->horizontalHeader()->setStretchLastSection(true);
    queue_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    queue_layout->addWidget(queue_table_);

    analyze_next_button_ = new QPushButton("Analyze Next");
    connect(analyze_next_button_, &QPushButton::clicked,
            this, &MemoryDockWidget::on_analyze_next);
    queue_layout->addWidget(analyze_next_button_);

    tabs_->addTab(queue_tab, "Queue");

    // Tab 5: Deep Analysis
    QWidget* deep_analysis_tab = new QWidget();
    QVBoxLayout* deep_analysis_layout = new QVBoxLayout(deep_analysis_tab);

    // Header with metadata
    analysis_meta_label_ = new QLabel("Deep Analysis Results");
    analysis_meta_label_->setStyleSheet("font-weight: bold; padding: 5px;");
    deep_analysis_layout->addWidget(analysis_meta_label_);

    // Splitter for list and viewer
    QSplitter* deep_analysis_splitter = new QSplitter(Qt::Vertical);

    // List of analyses
    deep_analysis_list_ = new QListWidget();
    deep_analysis_list_->setMaximumHeight(150);
    connect(deep_analysis_list_, &QListWidget::currentRowChanged, this, &MemoryDockWidget::on_deep_analysis_selected);
    deep_analysis_splitter->addWidget(deep_analysis_list_);

    // Analysis viewer
    deep_analysis_viewer_ = new QTextEdit();
    deep_analysis_viewer_->setReadOnly(true);
    deep_analysis_viewer_->setFont(QFont("Consolas", 9));
    deep_analysis_splitter->addWidget(deep_analysis_viewer_);

    deep_analysis_layout->addWidget(deep_analysis_splitter);
    tabs_->addTab(deep_analysis_tab, "Deep Analysis");

    // Tab 6: Memory Stats
    stats_browser_ = new QTextBrowser();
    tabs_->addTab(stats_browser_, "Statistics");
}

void MemoryDockWidget::update_memory(std::shared_ptr<BinaryMemory> memory) {
    memory_ = memory;
    refresh_views();
}


void MemoryDockWidget::on_function_selected() {
    auto items = function_tree_->selectedItems();
    if (items.isEmpty()) {
        function_analysis_viewer_->clear();
        return;
    }

    QTreeWidgetItem* item = items.first();
    ea_t addr = item->data(0, Qt::UserRole).toULongLong();

    // Get the stored analysis
    QString analysis = item->data(0, Qt::UserRole + 1).toString();
    function_analysis_viewer_->setPlainText(analysis);

    // Update other views
    call_graph_->center_on_function(addr);

    // Show in notes viewer if on insights tab
    if (tabs_->currentIndex() == 2 && memory_) {
        notes_viewer_->setPlainText(analysis);
    }

    emit function_selected(addr);
}

void MemoryDockWidget::on_filter_changed() {
    // Just refresh the views with the new filter
    refresh_views();
}

    void MemoryDockWidget::on_analyze_next() {
    if (!memory_) return;

    // Check if we can continue (need access to agent state)
    MainForm* main_form = get_main_form();
    if (!main_form) return;

    // Check if task is completed
    if (!main_form->can_continue()) {
        main_form->log(LogLevel::WARNING, "Cannot analyze next item - current task is not completed yet");
        return;
    }

    auto queue = memory_->get_analysis_queue();
    if (queue.empty()) {
        main_form->log(LogLevel::INFO, "No functions in the analysis queue");
        return;
    }

    // Get the highest priority item
    auto [addr, reason, priority] = queue[0];

    // Create a continue instruction
    QString continue_instruction = QString(
        "Continue the analysis by examining the function at address 0x%1. "
        "This function was marked for analysis because: %2. "
        "Build on your previous findings and update your understanding."
    ).arg(addr, 0, 16).arg(QString::fromStdString(reason));

    main_form->log(LogLevel::INFO,
        std::format("Analyzing next in queue: 0x{:x} (priority: {})", addr, priority));

    // Request continuation with this instruction
    emit continue_requested(continue_instruction);
}

void MemoryDockWidget::on_insight_selected() {
    auto items = insights_list_->selectedItems();
    if (items.isEmpty()) return;

    QListWidgetItem* item = items.first();

    // Get the insight text
    QString insight_text = item->text();

    // Get related addresses
    QVariant data = item->data(Qt::UserRole);
    std::vector<ea_t> addresses;
    if (data.canConvert<std::vector<ea_t>>()) {
        addresses = data.value<std::vector<ea_t>>();
    }

    // Display in notes viewer
    QString display_text = "Insight: " + insight_text + "\n\n";

    if (!addresses.empty()) {
        display_text += "Related functions:\n";
        for (ea_t addr : addresses) {
            qstring func_name;
            get_func_name(&func_name, addr);
            display_text += QString("  - 0x%1 %2\n")
                .arg(addr, 0, 16)
                .arg(QString::fromStdString(func_name.c_str()));
        }
    }

    notes_viewer_->setPlainText(display_text);

    // If only one related address, select it in the function tree
    if (addresses.size() == 1) {
        // Find and select the item in the function tree
        for (int i = 0; i < function_tree_->topLevelItemCount(); i++) {
            QTreeWidgetItem* tree_item = function_tree_->topLevelItem(i);
            ea_t item_addr = tree_item->data(0, Qt::UserRole).toULongLong();
            if (item_addr == addresses[0]) {
                function_tree_->setCurrentItem(tree_item);
                break;
            }
        }
    }
}

void MemoryDockWidget::on_deep_analysis_selected() {
    QList<QListWidgetItem*> items = deep_analysis_list_->selectedItems();
    if (items.isEmpty()) {
        deep_analysis_viewer_->clear();
        analysis_meta_label_->setText("Deep Analysis Results");
        return;
    }

    QListWidgetItem* item = items.first();
    
    // Get the analysis key stored in the item
    QString analysis_key = item->data(Qt::UserRole).toString();
    
    if (!memory_) return;
    
    // Get the analysis content using the new unified system
    auto analysis_entries = memory_->get_analysis("deep_analysis_" + analysis_key.toStdString());
    auto meta_entries = memory_->get_analysis("deep_analysis_meta_" + analysis_key.toStdString());

    if (!analysis_entries.empty()) {
        // Display the analysis
        deep_analysis_viewer_->setPlainText(QString::fromStdString(analysis_entries[0].content));
    } else {
        deep_analysis_viewer_->setPlainText("Analysis content not found");
    }

    // Update metadata label
    if (!meta_entries.empty()) {
        try {
            json metadata = json::parse(meta_entries[0].content);
            QString meta_text = QString("Topic: %1 | Task: %2")
                .arg(QString::fromStdString(metadata["topic"].get<std::string>()))
                .arg(QString::fromStdString(metadata["task"].get<std::string>()));
            
            if (metadata.contains("cost_estimate")) {
                meta_text += QString(" | Cost: $%1").arg(metadata["cost_estimate"].get<double>(), 0, 'f', 4);
            }
            
            analysis_meta_label_->setText(meta_text);
        } catch (...) {
            analysis_meta_label_->setText("Deep Analysis Results");
        }
    }
}


void MemoryDockWidget::refresh_views() {
    if (!memory_) return;

    // Update function tree
    function_tree_->clear();

    auto functions = memory_->get_analyzed_functions();
    json snapshot = memory_->export_memory_snapshot();

    for (const auto& [addr, name, level] : functions) {
        // Get detailed info
        auto analysis = memory_->get_function_analysis(addr, level);

        // Get relationships from memory snapshot - FIX THE COMPARISON
        int caller_count = 0;
        int callee_count = 0;
        int string_count = 0;

        // Find this function in the snapshot
        for (const auto& func : snapshot["functions"]) {
            // Compare addresses properly
            ea_t func_addr = 0;
            std::string addr_str = func["address"].get<std::string>();
            if (addr_str.find("0x") == 0) {
                func_addr = std::stoull(addr_str.substr(2), nullptr, 16);
            } else {
                func_addr = std::stoull(addr_str, nullptr, 16);
            }

            if (func_addr == addr) {
                if (func.contains("callers")) {
                    caller_count = func["callers"].size();
                }
                if (func.contains("callees")) {
                    callee_count = func["callees"].size();
                }
                if (func.contains("string_refs")) {
                    string_count = func["string_refs"].size();
                }
                break;
            }
        }

        // Apply filters
        QString filter_text = function_filter_->text();
        if (!filter_text.isEmpty() &&
            !QString::fromStdString(name).contains(filter_text, Qt::CaseInsensitive) &&
            !QString::fromStdString(analysis).contains(filter_text, Qt::CaseInsensitive)) {
            continue;
        }

        int level_filter_idx = level_filter_->currentIndex();
        if (level_filter_idx > 0 && static_cast<int>(level) != level_filter_idx) {
            continue;
        }

        // Create tree item
        QTreeWidgetItem* item = new QTreeWidgetItem(function_tree_);
        item->setText(0, QString("0x%1 %2").arg(addr, 0, 16).arg(QString::fromStdString(name)));
        item->setData(0, Qt::UserRole, QVariant::fromValue(addr));

        // Store the full analysis in user data for display when selected
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(analysis));

        // Show analysis level with color coding
        QString level_str;
        QColor level_color;
        switch (level) {
            case DetailLevel::SUMMARY:
                level_str = "Summary";
                level_color = QColor(200, 200, 200);
                break;
            case DetailLevel::CONTEXTUAL:
                level_str = "Contextual";
                level_color = QColor(150, 200, 255);
                break;
            case DetailLevel::ANALYTICAL:
                level_str = "Analytical";
                level_color = QColor(100, 255, 100);
                break;
            case DetailLevel::COMPREHENSIVE:
                level_str = "Comprehensive";
                level_color = QColor(255, 200, 100);
                break;
        }
        item->setText(1, level_str);
        item->setBackground(1, level_color);

        item->setText(2, QString::number(caller_count));
        item->setText(3, QString::number(callee_count));
        item->setText(4, QString::number(string_count));

        // Status indicators
        QString status;
        if (memory_->is_anchor_point(addr)) {
            status = "⚓ Anchor";
            item->setForeground(0, QColor(255, 200, 0));
        }
        if (addr == memory_->get_current_focus()) {
            status += " 🎯 Focus";
            item->setBackground(0, QColor(50, 50, 150));
            item->setForeground(0, Qt::white);
        }
        item->setText(5, status);
    }

    // Update call graph
    call_graph_->update_graph(memory_);

    // Update insights using the new unified system
    insights_list_->clear();
    QString type_filter = insight_filter_->currentText().toLower();
    if (type_filter == "all") type_filter = "";

    // Get analyses of the selected type
    auto analyses = memory_->get_analysis("", std::nullopt, type_filter.toStdString(), "");

    for (const auto& entry : analyses) {
        // Only show entries that are insights (not regular analysis)
        if (entry.type == "finding" || entry.type == "hypothesis" ||
            entry.type == "question" || entry.type == "pattern") {
            QListWidgetItem* item = new QListWidgetItem(insights_list_);
            QString text = QString::fromStdString(entry.content);
            if (!entry.related_addresses.empty()) {
                text += QString(" [%1 functions]").arg(entry.related_addresses.size());
            }
            item->setText(text);
            item->setData(Qt::UserRole, QVariant::fromValue(entry.related_addresses));
        }
    }

    // Update deep analysis list using the new unified system
    deep_analysis_list_->clear();

    // Get all deep analysis entries
    auto deep_analyses = memory_->get_analysis("", std::nullopt, "deep_analysis_metadata", "");

    // Sort by timestamp (most recent first)
    std::sort(deep_analyses.begin(), deep_analyses.end(),
              [](const auto& a, const auto& b) { return a.timestamp > b.timestamp; });

    // Populate the list
    for (const auto& entry : deep_analyses) {
        // Extract key from "deep_analysis_meta_" prefix
        std::string key = entry.key;
        if (key.find("deep_analysis_meta_") == 0) {
            key = key.substr(19);
        }

        try {
            json metadata = json::parse(entry.content);
            std::string description = metadata["topic"].get<std::string>() + " - " +
                                    metadata["task"].get<std::string>();

            QListWidgetItem* item = new QListWidgetItem(deep_analysis_list_);
            item->setText(QString::fromStdString(description));
            item->setData(Qt::UserRole, QString::fromStdString(key));
            item->setToolTip(QString("Key: %1").arg(QString::fromStdString(key)));
        } catch (...) {
            // Skip malformed entries
        }
    }

    // Update queue
    queue_table_->setRowCount(0);
    auto queue = memory_->get_analysis_queue();
    for (const auto& [addr, reason, priority] : queue) {
        int row = queue_table_->rowCount();
        queue_table_->insertRow(row);

        qstring func_name;
        get_func_name(&func_name, addr);

        queue_table_->setItem(row, 0, new QTableWidgetItem(QString("0x%1").arg(addr, 0, 16)));
        queue_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(func_name.c_str())));
        queue_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(reason)));

        QTableWidgetItem* priority_item = new QTableWidgetItem(QString::number(priority));
        priority_item->setTextAlignment(Qt::AlignCenter);
        if (priority >= 8) {
            priority_item->setBackground(QColor(255, 200, 200));
        } else if (priority >= 5) {
            priority_item->setBackground(QColor(255, 255, 200));
        }
        queue_table_->setItem(row, 3, priority_item);
    }

    // Update statistics
    update_statistics();
}

void MemoryDockWidget::set_current_focus(ea_t address) {
    if (memory_) {
        memory_->set_current_focus(address);
        refresh_views();

        // Also center the call graph on this address
        call_graph_->center_on_function(address);
    }
}

void MemoryDockWidget::update_statistics() {
    if (!memory_) return;

    json snapshot = memory_->export_memory_snapshot();

    QString html = "<html><body style='font-family: Arial; padding: 10px;'>";
    html += "<h3>Memory Statistics</h3>";

    // Function statistics
    int total_functions = snapshot["functions"].size();
    std::map<int, int> level_counts;
    int anchor_count = 0;
    int total_callers = 0;
    int total_callees = 0;
    int total_strings = 0;

    for (const auto& func : snapshot["functions"]) {
        int level = func["current_level"];
        level_counts[level]++;

        if (func.contains("distance_from_anchor") && func["distance_from_anchor"] == -1) {
            anchor_count++;
        }

        total_callers += func["callers"].size();
        total_callees += func["callees"].size();
        total_strings += func["string_refs"].size();
    }

    html += QString("<p><b>Total Functions Analyzed:</b> %1</p>").arg(total_functions);
    html += QString("<p><b>Anchor Points:</b> %1</p>").arg(anchor_count);

    html += "<h4>Analysis Levels:</h4><ul>";
    for (const auto& [level, count] : level_counts) {
        QString level_name;
        switch (level) {
            case 1: level_name = "Summary"; break;
            case 2: level_name = "Contextual"; break;
            case 3: level_name = "Analytical"; break;
            case 4: level_name = "Comprehensive"; break;
        }
        html += QString("<li>%1: %2</li>").arg(level_name).arg(count);
    }
    html += "</ul>";

    html += QString("<p><b>Total Call Relationships:</b> %1 callers, %2 callees</p>")
        .arg(total_callers).arg(total_callees);
    html += QString("<p><b>Total String References:</b> %1</p>").arg(total_strings);

    // Insights statistics
    int insight_count = snapshot["insights"].size();
    std::map<std::string, int> insight_types;
    for (const auto& insight : snapshot["insights"]) {
        insight_types[insight["type"]]++;
    }

    html += QString("<h4>Insights (%1 total):</h4><ul>").arg(insight_count);
    for (const auto& [type, count] : insight_types) {
        html += QString("<li>%1: %2</li>").arg(QString::fromStdString(type)).arg(count);
    }
    html += "</ul>";

    // Global notes
    int notes_count = snapshot["global_notes"].size();
    html += QString("<p><b>Global Notes:</b> %1</p>").arg(notes_count);

    html += "</body></html>";
    stats_browser_->setHtml(html);
}

} // namespace llm_re::ui