#pragma once

#include "ui_v2_common.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

class ThemeTransitionManager : public QObject {
    Q_OBJECT

public:
    static ThemeTransitionManager& instance();
    
    enum TransitionType {
        Instant,      // No animation
        Fade,         // Fade through background
        CrossFade,    // Cross-fade between themes
        Slide,        // Slide transition
        Morph         // Smooth morphing of colors
    };
    
    // Transition settings
    void setTransitionType(TransitionType type) { transitionType_ = type; }
    TransitionType transitionType() const { return transitionType_; }
    
    void setDuration(int ms) { duration_ = ms; }
    int duration() const { return duration_; }
    
    void setEasingCurve(const QEasingCurve& curve) { easingCurve_ = curve; }
    QEasingCurve easingCurve() const { return easingCurve_; }
    
    // Perform theme transition
    void transitionToTheme(const QString& themeName);
    void transitionToTheme(ThemeManager::Theme theme);
    
    // Check if transition is in progress
    bool isTransitioning() const { return isTransitioning_; }
    
signals:
    void transitionStarted();
    void transitionFinished();
    void transitionProgress(qreal progress);

private slots:
    void onAnimationFinished();
    void onAnimationValueChanged(const QVariant& value);

private:
    ThemeTransitionManager();
    ~ThemeTransitionManager() = default;
    ThemeTransitionManager(const ThemeTransitionManager&) = delete;
    ThemeTransitionManager& operator=(const ThemeTransitionManager&) = delete;
    
    void performInstantTransition(const QString& themeName);
    void performFadeTransition(const QString& themeName);
    void performCrossFadeTransition(const QString& themeName);
    void performSlideTransition(const QString& themeName);
    void performMorphTransition(const QString& themeName);
    
    void captureCurrentTheme();
    void interpolateThemes(qreal progress);
    
    // Animation objects
    QParallelAnimationGroup* animationGroup_ = nullptr;
    std::vector<QPropertyAnimation*> colorAnimations_;
    
    // Transition state
    TransitionType transitionType_ = TransitionType::Morph;
    int duration_ = 300;
    QEasingCurve easingCurve_ = QEasingCurve::InOutQuad;
    bool isTransitioning_ = false;
    
    // Theme snapshots for transitions
    struct ThemeSnapshot {
        std::map<QString, QColor> colors;
        Typography typography;
        ComponentStyles components;
    };
    
    ThemeSnapshot sourceTheme_;
    ThemeSnapshot targetTheme_;
    QString targetThemeName_;
};

// Widget that supports theme transitions
class TransitionableWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit TransitionableWidget(QWidget* parent = nullptr);
    
    QColor backgroundColor() const { return backgroundColor_; }
    void setBackgroundColor(const QColor& color);
    
    QColor textColor() const { return textColor_; }
    void setTextColor(const QColor& color);
    
    qreal opacity() const { return opacity_; }
    void setOpacity(qreal opacity);

protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    QColor backgroundColor_;
    QColor textColor_;
    qreal opacity_ = 1.0;
};

// Overlay widget for fade transitions
class TransitionOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)

public:
    explicit TransitionOverlay(QWidget* parent = nullptr);
    
    qreal fadeOpacity() const { return fadeOpacity_; }
    void setFadeOpacity(qreal opacity);
    
    void setFadeColor(const QColor& color) { fadeColor_ = color; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    qreal fadeOpacity_ = 0.0;
    QColor fadeColor_;
};

} // namespace llm_re::ui_v2