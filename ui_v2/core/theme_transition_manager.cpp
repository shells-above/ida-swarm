#include "ui_v2_common.h"
#include "theme_transition_manager.h"

#include "core/config.h"

namespace llm_re::ui_v2 {

// Helper function to convert ColorPalette to a map
static std::map<QString, QColor> colorPaletteToMap(const ColorPalette& palette) {
    std::map<QString, QColor> colorMap;
    
    // Brand colors
    colorMap["primary"] = palette.primary;
    colorMap["primaryHover"] = palette.primaryHover;
    colorMap["primaryActive"] = palette.primaryActive;
    
    // Semantic colors
    colorMap["success"] = palette.success;
    colorMap["warning"] = palette.warning;
    colorMap["error"] = palette.error;
    colorMap["info"] = palette.info;
    
    // Neutral colors
    colorMap["background"] = palette.background;
    colorMap["surface"] = palette.surface;
    colorMap["surfaceHover"] = palette.surfaceHover;
    colorMap["surfaceActive"] = palette.surfaceActive;
    colorMap["border"] = palette.border;
    colorMap["borderStrong"] = palette.borderStrong;
    
    // Text colors
    colorMap["textPrimary"] = palette.textPrimary;
    colorMap["textSecondary"] = palette.textSecondary;
    colorMap["textTertiary"] = palette.textTertiary;
    colorMap["textInverse"] = palette.textInverse;
    
    // Special colors
    colorMap["selection"] = palette.selection;
    colorMap["overlay"] = palette.overlay;
    colorMap["shadow"] = palette.shadow;
    colorMap["searchHighlight"] = palette.searchHighlight;
    
    // Syntax highlighting
    colorMap["syntaxKeyword"] = palette.syntaxKeyword;
    colorMap["syntaxComment"] = palette.syntaxComment;
    colorMap["syntaxString"] = palette.syntaxString;
    colorMap["syntaxNumber"] = palette.syntaxNumber;
    colorMap["syntaxFunction"] = palette.syntaxFunction;
    colorMap["syntaxVariable"] = palette.syntaxVariable;
    
    // Diff colors
    colorMap["diffAdd"] = palette.diffAdd;
    colorMap["diffRemove"] = palette.diffRemove;
    colorMap["currentLineHighlight"] = palette.currentLineHighlight;
    
    // Chart colors
    colorMap["chartGrid"] = palette.chartGrid;
    colorMap["chartAxis"] = palette.chartAxis;
    colorMap["chartLabel"] = palette.chartLabel;
    
    return colorMap;
}

ThemeTransitionManager& ThemeTransitionManager::instance() {
    static ThemeTransitionManager instance;
    return instance;
}

ThemeTransitionManager::ThemeTransitionManager() : QObject(nullptr) {
    animationGroup_ = new QParallelAnimationGroup(this);
    connect(animationGroup_, &QParallelAnimationGroup::finished,
            this, &ThemeTransitionManager::onAnimationFinished);
}

void ThemeTransitionManager::transitionToTheme(const QString& themeName) {
    if (isTransitioning_) {
        // Stop current transition
        animationGroup_->stop();
        onAnimationFinished();
    }
    
    targetThemeName_ = themeName;
    
    switch (transitionType_) {
        case Instant:
            performInstantTransition(themeName);
            break;
        case Fade:
            performFadeTransition(themeName);
            break;
        case CrossFade:
            performCrossFadeTransition(themeName);
            break;
        case Slide:
            performSlideTransition(themeName);
            break;
        case Morph:
            performMorphTransition(themeName);
            break;
    }
}

void ThemeTransitionManager::transitionToTheme(ThemeManager::Theme theme) {
    QString themeName;
    switch (theme) {
        case ThemeManager::Theme::Dark:
            themeName = "dark";
            break;
        case ThemeManager::Theme::Light:
            themeName = "light";
            break;
        default:
            themeName = "default";
            break;
    }
    transitionToTheme(themeName);
}

void ThemeTransitionManager::performInstantTransition(const QString& themeName) {
    ThemeManager::instance().loadTheme(themeName);
    emit transitionFinished();
}

void ThemeTransitionManager::performFadeTransition(const QString& themeName) {
    isTransitioning_ = true;
    emit transitionStarted();
    
    // Create fade overlay
    auto* overlay = new TransitionOverlay(qApp->activeWindow());
    overlay->setGeometry(qApp->activeWindow()->rect());
    overlay->setFadeColor(ThemeManager::instance().colors().background);
    overlay->show();
    overlay->raise();
    
    // Fade out
    auto* fadeOut = new QPropertyAnimation(overlay, "fadeOpacity");
    fadeOut->setDuration(duration_ / 2);
    fadeOut->setStartValue(0.0);
    fadeOut->setEndValue(1.0);
    fadeOut->setEasingCurve(easingCurve_);
    
    // Change theme at midpoint
    connect(fadeOut, &QPropertyAnimation::finished, [this, themeName, overlay]() {
        ThemeManager::instance().loadTheme(themeName);
        
        // Fade in
        auto* fadeIn = new QPropertyAnimation(overlay, "fadeOpacity");
        fadeIn->setDuration(duration_ / 2);
        fadeIn->setStartValue(1.0);
        fadeIn->setEndValue(0.0);
        fadeIn->setEasingCurve(easingCurve_);
        
        connect(fadeIn, &QPropertyAnimation::finished, [overlay]() {
            overlay->deleteLater();
        });
        
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });
    
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void ThemeTransitionManager::performCrossFadeTransition(const QString& themeName) {
    isTransitioning_ = true;
    emit transitionStarted();
    
    captureCurrentTheme();
    
    // Load target theme temporarily
    ThemeManager::instance().loadTheme(themeName);
    
    // Capture target theme
    targetTheme_.colors = colorPaletteToMap(ThemeManager::instance().colors());
    targetTheme_.typography = ThemeManager::instance().typography();
    targetTheme_.components = ThemeManager::instance().componentStyles();
    
    // Restore source theme
    auto currentInfo = ThemeManager::instance().getCurrentThemeInfo();
    ThemeManager::instance().loadTheme(currentInfo.name);
    
    // Animate the transition
    auto* progress = new QPropertyAnimation(this, "progress");
    progress->setDuration(duration_);
    progress->setStartValue(0.0);
    progress->setEndValue(1.0);
    progress->setEasingCurve(easingCurve_);
    
    connect(progress, &QPropertyAnimation::valueChanged,
            this, &ThemeTransitionManager::onAnimationValueChanged);
    
    animationGroup_->addAnimation(progress);
    animationGroup_->start();
}

void ThemeTransitionManager::performSlideTransition(const QString& themeName) {
    // Similar to fade but with sliding motion
    performFadeTransition(themeName);
}

void ThemeTransitionManager::performMorphTransition(const QString& themeName) {
    isTransitioning_ = true;
    emit transitionStarted();
    
    captureCurrentTheme();
    
    // Create property animations for each color
    auto& tm = ThemeManager::instance();
    
    // Temporarily load target theme to get colors
    QString currentTheme = QString::fromStdString(Config::instance().ui.theme_name);
    tm.loadTheme(themeName);
    
    // Capture target colors
    for (const auto& [name, colorPtr] : tm.colorMap_) {
        if (colorPtr) {
            targetTheme_.colors[QString::fromStdString(name)] = *colorPtr;
        }
    }
    
    // Restore current theme
    tm.loadTheme(currentTheme);
    
    // Create animations for each color
    colorAnimations_.clear();
    
    for (const auto& [name, targetColor] : targetTheme_.colors) {
        if (sourceTheme_.colors.count(name)) {
            QColor sourceColor = sourceTheme_.colors[name];
            
            // Create color animation using a dummy property
            auto* animation = new QPropertyAnimation(this, "dummy");
            animation->setDuration(duration_);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(easingCurve_);
            
            // Use lambda to interpolate colors
            connect(animation, &QPropertyAnimation::valueChanged,
                    [this, name, sourceColor, targetColor](const QVariant& value) {
                qreal t = value.toReal();
                
                // Interpolate in HSV space for smoother transitions
                int h1, s1, v1, a1;
                int h2, s2, v2, a2;
                sourceColor.getHsv(&h1, &s1, &v1, &a1);
                targetColor.getHsv(&h2, &s2, &v2, &a2);
                
                // Handle hue wrap-around
                if (abs(h2 - h1) > 180) {
                    if (h2 > h1) h1 += 360;
                    else h2 += 360;
                }
                
                QColor interpolated;
                interpolated.setHsv(
                    int(h1 + (h2 - h1) * t) % 360,
                    int(s1 + (s2 - s1) * t),
                    int(v1 + (v2 - v1) * t),
                    int(a1 + (a2 - a1) * t)
                );
                
                ThemeManager::instance().setColor(name, interpolated);
            });
            
            colorAnimations_.push_back(animation);
            animationGroup_->addAnimation(animation);
        }
    }
    
    // Start the animation group
    animationGroup_->start();
}

void ThemeTransitionManager::captureCurrentTheme() {
    auto& tm = ThemeManager::instance();
    
    sourceTheme_.colors = colorPaletteToMap(tm.colors());
    sourceTheme_.typography = tm.typography();
    sourceTheme_.components = tm.componentStyles();
}

void ThemeTransitionManager::interpolateThemes(qreal progress) {
    // This is called during cross-fade transitions
    // Interpolate between source and target themes
    
    auto& tm = ThemeManager::instance();
    
    for (const auto& [name, sourceColor] : sourceTheme_.colors) {
        if (targetTheme_.colors.count(name)) {
            QColor targetColor = targetTheme_.colors[name];
            
            // Interpolate colors
            QColor interpolated = QColor(
                sourceColor.red() + (targetColor.red() - sourceColor.red()) * progress,
                sourceColor.green() + (targetColor.green() - sourceColor.green()) * progress,
                sourceColor.blue() + (targetColor.blue() - sourceColor.blue()) * progress,
                sourceColor.alpha() + (targetColor.alpha() - sourceColor.alpha()) * progress
            );
            
            tm.setColor(name, interpolated);
        }
    }
    
    emit transitionProgress(progress);
}

void ThemeTransitionManager::onAnimationFinished() {
    // Ensure we're on the target theme
    ThemeManager::instance().loadTheme(targetThemeName_);
    
    // Clean up animations
    animationGroup_->clear();
    colorAnimations_.clear();
    
    isTransitioning_ = false;
    emit transitionFinished();
}

void ThemeTransitionManager::onAnimationValueChanged(const QVariant& value) {
    qreal progress = value.toReal();
    interpolateThemes(progress);
}

// TransitionableWidget implementation

TransitionableWidget::TransitionableWidget(QWidget* parent)
    : QWidget(parent) {
    const auto& colors = ThemeManager::instance().colors();
    backgroundColor_ = colors.surface;
    textColor_ = colors.textPrimary;
}

void TransitionableWidget::setBackgroundColor(const QColor& color) {
    backgroundColor_ = color;
    update();
}

void TransitionableWidget::setTextColor(const QColor& color) {
    textColor_ = color;
    update();
}

void TransitionableWidget::setOpacity(qreal opacity) {
    opacity_ = qBound(0.0, opacity, 1.0);
    update();
}

void TransitionableWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setOpacity(opacity_);
    painter.fillRect(rect(), backgroundColor_);
}

// TransitionOverlay implementation

TransitionOverlay::TransitionOverlay(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
}

void TransitionOverlay::setFadeOpacity(qreal opacity) {
    fadeOpacity_ = qBound(0.0, opacity, 1.0);
    update();
}

void TransitionOverlay::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setOpacity(fadeOpacity_);
    painter.fillRect(rect(), fadeColor_);
}

} // namespace llm_re::ui_v2