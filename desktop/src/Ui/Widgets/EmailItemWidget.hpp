#ifndef EMAILITEMWIDGET_HPP
#define EMAILITEMWIDGET_HPP

#include <QWidget>
#include <QString>
#include <QDateTime>

#include "Email/EmailParser.hpp"

class QLabel;

namespace aurora::mail::app::email {
    struct EmailSummary;
}

/**
 * @class EmailItemWidget
 * @brief A clickable widget representing a single email in the mail list.
 *
 * Displays sender, subject, preview, and date. Supports selected and unread
 * states. Emits clicked(uid) when the row is pressed.
 *
 * Invariants:
 *   - All child widgets are constructed eagerly in the constructor; their
 *     pointers are non-null for the lifetime of this widget.
 *   - updateContent() updates text/font/palette only.
 *   - updateStyle() updates QSS-driven dynamic properties only (selected).
 */
class EmailItemWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(bool unread READ isUnread WRITE setUnread NOTIFY unreadChanged)
    Q_DISABLE_COPY_MOVE(EmailItemWidget)

public:
    explicit EmailItemWidget(const aurora::mail::app::email::EmailSummary& summary,
                             QWidget* parent = nullptr);
    explicit EmailItemWidget(QWidget* parent = nullptr);
    ~EmailItemWidget() override = default;

    [[nodiscard]] const QString& uid() const noexcept { return m_summary.uid; }
    [[nodiscard]] const aurora::mail::app::email::EmailSummary& summary() const noexcept
    {
        return m_summary;
    }

    void setEmailSummary(const aurora::mail::app::email::EmailSummary& summary);

    [[nodiscard]] bool isSelected() const noexcept { return m_selected; }
    void setSelected(bool selected);

    [[nodiscard]] bool isUnread() const noexcept { return !m_summary.isRead; }
    void setUnread(bool unread);

    /**
     * Switch the row's primary label between "From" (default, false) and "To"
     * (true). Used for outgoing folders (Sent / Drafts) where the recipient
     * is the more useful identity to display. Falls back to From if the
     * recipient field is empty.
     */
    void setShowRecipient(bool showRecipient);
    [[nodiscard]] bool showsRecipient() const noexcept { return m_showRecipient; }

signals:
    void clicked(const QString& uid);
    void selectedChanged();
    void unreadChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void createUi();
    void updateContent();   // text + per-label QFont/QPalette
    void updateStyle();     // dynamic property "selected" + repolish

    static QString formatDate(const QDateTime& dt);

private:
    aurora::mail::app::email::EmailSummary m_summary;

    bool m_selected{false};
    bool m_showRecipient{false};

    // Child widgets — constructed in the ctor and owned by Qt parent chain.
    // Non-null for the lifetime of *this*.
    QLabel* m_senderLabel{nullptr};
    QLabel* m_subjectLabel{nullptr};
    QLabel* m_previewLabel{nullptr};
    QLabel* m_dateLabel{nullptr};
    QLabel* m_avatarLabel{nullptr};

    static constexpr int PREVIEW_MAX_LENGTH{80};
    static constexpr int SENDER_FONT_SIZE{13};
    static constexpr int SUBJECT_FONT_SIZE{13};
    static constexpr int PREVIEW_FONT_SIZE{12};
    static constexpr int DATE_FONT_SIZE{11};
};

#endif // EMAILITEMWIDGET_HPP
