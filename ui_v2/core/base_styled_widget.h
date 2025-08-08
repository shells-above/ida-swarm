#pragma once

#include "ui_v2_common.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

// Base class for all themed widgets
class BaseStyledWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animationProgress READ animationProgress WRITE setAnimationProgress)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth)

public:
    explicit BaseStyledWidget(QWidget* parent = nullptr);
    ~BaseStyledWidget() override = default;

    // Theme integration
    void applyTheme();
    void setCustomStyleSheet(const QString& styleSheet);
    
    // Visual properties
    QColor backgroundColor() const { return backgroundColor_; }
    void setBackgroundColor(const QColor& color);
    
    QColor borderColor() const { return borderColor_; }
    void setBorderColor(const QColor& color);
    
    int borderRadius() const { return borderRadius_; }
    void setBorderRadius(int radius);
    
    int borderWidth() const { return borderWidth_; }
    void setBorderWidth(int width);
    
    // Shadow effects
    void setShadowEnabled(bool enabled);
    bool isShadowEnabled() const { return shadowEnabled_; }
    void setShadowBlur(int blur);
    void setShadowColor(const QColor& color);
    void setShadowOffset(const QPointF& offset);
    
    // Animation support
    qreal animationProgress() const { return animationProgress_; }
    void setAnimationProgress(qreal progress);
    
    // Hover effects
    void setHoverEnabled(bool enabled);
    bool isHoverEnabled() const { return hoverEnabled_; }
    void setHoverScale(qreal scale) { hoverScale_ = scale; }
    void setHoverOpacity(qreal opacity) { hoverOpacity_ = opacity; }
    
    // Focus effects
    void setFocusOutlineEnabled(bool enabled);
    void setFocusOutlineColor(const QColor& color);
    void setFocusOutlineWidth(int width);
    
    // Loading state
    void setLoading(bool loading);
    bool isLoading() const { return isLoading_; }
    
    // Disabled state styling
    void setDisabledOpacity(qreal opacity) { disabledOpacity_ = opacity; }

protected:
    // Override these in subclasses for custom painting
    virtual void paintBackground(QPainter* painter);
    virtual void paintBorder(QPainter* painter);
    virtual void paintContent(QPainter* painter);
    virtual void paintLoadingIndicator(QPainter* painter);
    virtual void paintFocusOutline(QPainter* painter);
    
    // Event handlers
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;
    
    // Animation helpers
    void animateProperty(const QByteArray& property, const QVariant& endValue, 
                        int duration = Design::ANIM_NORMAL);
    void stopAnimation(const QByteArray& property);
    
    // Theme change notification
    virtual void onThemeChanged();
    
    // Utility functions
    QColor effectiveBackgroundColor() const;
    QColor effectiveBorderColor() const;
    qreal effectiveOpacity() const;
    
private slots:
    void onThemeManagerChanged();

private:
    void updateShadow();
    void startHoverAnimation(bool hovering);
    void updateStyleSheet();
    
    // Visual properties
    QColor backgroundColor_;
    QColor borderColor_;
    int borderRadius_ = Design::RADIUS_MD;
    int borderWidth_ = 1;
    
    // Shadow
    bool shadowEnabled_ = false;
    std::unique_ptr<QGraphicsDropShadowEffect> shadowEffect_;
    int shadowBlur_ = 10;
    QColor shadowColor_;
    QPointF shadowOffset_ = QPointF(0, 2);
    
    // Animation
    qreal animationProgress_ = 0.0;
    std::map<QByteArray, QPropertyAnimation*> animations_;
    
    // Hover
    bool hoverEnabled_ = false;
    bool isHovered_ = false;
    qreal hoverScale_ = 1.02;
    qreal hoverOpacity_ = 0.9;
    
    // Focus
    bool focusOutlineEnabled_ = false;
    QColor focusOutlineColor_;
    int focusOutlineWidth_ = 2;
    
    // State
    bool isLoading_ = false;
    qreal disabledOpacity_ = 0.5;
    
    // Custom style
    QString customStyleSheet_;
    
    // Loading animation
    QTimer* loadingTimer_ = nullptr;
    int loadingAngle_ = 0;
};

// Convenience widget with card-like appearance
class CardWidget : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit CardWidget(QWidget* parent = nullptr);
    
    void setElevation(int level);
    int elevation() const { return elevation_; }

protected:
    void onThemeChanged() override;

private:
    int elevation_ = 1;
    void updateElevation();
};

// Panel widget with subtle styling
class PanelWidget : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit PanelWidget(QWidget* parent = nullptr);
    
    void setInset(bool inset);
    bool isInset() const { return inset_; }

protected:
    void onThemeChanged() override;

private:
    bool inset_ = false;
};

} // namespace llm_re::ui_v2