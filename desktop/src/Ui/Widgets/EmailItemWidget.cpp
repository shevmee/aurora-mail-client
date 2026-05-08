#include "EmailItemWidget.hpp"

#include <QColor>
#include <QDate>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>
#include <QtGlobal>

#include "Utils/TextSanitizer.hpp"

using aurora::mail::app::email::EmailSummary;
using aurora::mail::app::utils::TextSanitizer;

constexpr int EmailItemWidget::PREVIEW_MAX_LENGTH;
constexpr int EmailItemWidget::SENDER_FONT_SIZE;
constexpr int EmailItemWidget::SUBJECT_FONT_SIZE;
constexpr int EmailItemWidget::PREVIEW_FONT_SIZE;
constexpr int EmailItemWidget::DATE_FONT_SIZE;

namespace
{

  QString senderInitials(const QString& from)
  {
    QString s = from.trimmed();
    if (s.isEmpty())
    {
      return QStringLiteral("?");
    }
    const qsizetype at = s.indexOf(QLatin1Char('@'));
    if (at > 0)
    {
      s = s.left(at);
    }
    const QStringList parts = s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() >= 2)
    {
      return (parts[0].left(1) + parts[1].left(1)).toUpper();
    }
    return s.left(2).toUpper();
  }

  constexpr int WIDGET_MIN_HEIGHT = 80;
  constexpr int WIDGET_MAX_HEIGHT = 100;
  constexpr int PREFERRED_WIDTH = 320;
  constexpr int MAIN_MARGIN = 16;
  constexpr int MAIN_SPACING = 12;
  constexpr int TOP_BOTTOM_MARGIN = 12;
  constexpr int CONTENT_SPACING = 2;
  constexpr int TOP_ROW_SPACING = 8;
  constexpr int TEXT_HEIGHT_LARGE = 20;
  constexpr int TEXT_HEIGHT_SMALL = 18;

  constexpr int SENDER_MAX_LENGTH = 40;
  constexpr int SUBJECT_MAX_LENGTH = 60;

  // Read/unread row colors. Read messages dim slightly; unread are full strength
  // and additionally bolded — same convention as Gmail.
  const QColor kColorTextRead("#b0b0b0");
  const QColor kColorTextUnread("#e8e8e8");
  const QColor kColorPreview("#808080");

#ifdef Q_OS_MACOS
  constexpr Qt::CursorShape kMailRowCursorShape = Qt::ArrowCursor;
#else
  constexpr Qt::CursorShape kMailRowCursorShape = Qt::PointingHandCursor;
#endif

  QLabel* makeRowLabel(
      QWidget* parent,
      const QString& objectName,
      int fixedHeight,
      int pixelSize,
      Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
  {
    auto* lbl = new QLabel(parent);
    lbl->setObjectName(objectName);
    lbl->setTextFormat(Qt::PlainText);
    lbl->setFixedHeight(fixedHeight);
    lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lbl->setAlignment(alignment);
    // Mouse events fall through to the row so clicks/hover work uniformly
    // regardless of which sub-label the cursor lands on.
    lbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    QFont f = lbl->font();
    f.setPixelSize(pixelSize);
    lbl->setFont(f);

    return lbl;
  }

  void applyRowTextStyle(QLabel* label, const QColor& color, bool bold)
  {
    QFont f = label->font();
    if (f.bold() != bold)
    {
      f.setBold(bold);
      label->setFont(f);
    }
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, color);
    label->setPalette(p);
  }

  QString truncateWithEllipsis(QString s, int maxLen)
  {
    if (s.length() > maxLen)
    {
      s = s.left(maxLen) + QStringLiteral("...");
    }
    return s;
  }

}  // namespace

EmailItemWidget::EmailItemWidget(const EmailSummary& summary, QWidget* parent) : QWidget(parent), m_summary(summary)
{
  createUi();
  updateContent();
}

EmailItemWidget::EmailItemWidget(QWidget* parent) : QWidget(parent)
{
  createUi();
  updateContent();
}

void EmailItemWidget::createUi()
{
  setObjectName(QStringLiteral("MailListItem"));
  setMinimumHeight(WIDGET_MIN_HEIGHT);
  setMaximumHeight(WIDGET_MAX_HEIGHT);
  setCursor(kMailRowCursorShape);

  auto* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(MAIN_MARGIN, TOP_BOTTOM_MARGIN, MAIN_MARGIN, TOP_BOTTOM_MARGIN);
  mainLayout->setSpacing(MAIN_SPACING);

  m_avatarLabel = new QLabel(this);
  m_avatarLabel->setObjectName(QStringLiteral("AvatarBadge"));
  m_avatarLabel->setFixedSize(40, 40);
  m_avatarLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(m_avatarLabel, 0, Qt::AlignTop);

  auto* contentLayout = new QVBoxLayout();
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(CONTENT_SPACING);

  auto* topRow = new QHBoxLayout();
  topRow->setSpacing(TOP_ROW_SPACING);

  m_senderLabel = makeRowLabel(this, QStringLiteral("SenderLabel"), TEXT_HEIGHT_LARGE, SENDER_FONT_SIZE);
  topRow->addWidget(m_senderLabel, 1);

  m_dateLabel =
      makeRowLabel(this, QStringLiteral("DateLabel"), TEXT_HEIGHT_LARGE, DATE_FONT_SIZE, Qt::AlignRight | Qt::AlignVCenter);
  m_dateLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  topRow->addWidget(m_dateLabel);

  contentLayout->addLayout(topRow);

  m_subjectLabel = makeRowLabel(this, QStringLiteral("SubjectLabel"), TEXT_HEIGHT_LARGE, SUBJECT_FONT_SIZE);
  contentLayout->addWidget(m_subjectLabel);

  m_previewLabel = makeRowLabel(this, QStringLiteral("PreviewLabel"), TEXT_HEIGHT_SMALL, PREVIEW_FONT_SIZE);
  contentLayout->addWidget(m_previewLabel);

  mainLayout->addLayout(contentLayout, 1);

  updateStyle();
}

void EmailItemWidget::updateContent()
{
  // Outgoing folders (Sent / Drafts) display the recipient instead of the
  // local user. Fall back to From when "To" is unknown so we never render a
  // blank row, even when an envelope is missing recipient data.
  const bool useRecipient = m_showRecipient && !m_summary.to.isEmpty();
  const QString& primaryAddress = useRecipient ? m_summary.to : m_summary.from;

  m_avatarLabel->setText(senderInitials(primaryAddress));

  const bool unread = isUnread();
  const QColor& mainColor = unread ? kColorTextUnread : kColorTextRead;

  {
    QString sender = TextSanitizer::removeSupplementaryPlaneCharacters(primaryAddress);
    sender = truncateWithEllipsis(std::move(sender), SENDER_MAX_LENGTH);
    m_senderLabel->setText(sender);
    applyRowTextStyle(m_senderLabel, mainColor, /*bold=*/unread);
  }

  {
    QString subject = m_summary.subject.isEmpty() ? QStringLiteral("(No Subject)")
                                                  : TextSanitizer::removeSupplementaryPlaneCharacters(m_summary.subject);
    subject = truncateWithEllipsis(std::move(subject), SUBJECT_MAX_LENGTH);
    m_subjectLabel->setText(subject);
    applyRowTextStyle(m_subjectLabel, mainColor, /*bold=*/unread);
  }

  {
    QString preview = TextSanitizer::removeSupplementaryPlaneCharacters(m_summary.preview);
    preview = truncateWithEllipsis(std::move(preview), PREVIEW_MAX_LENGTH);
    m_previewLabel->setText(preview);
    applyRowTextStyle(m_previewLabel, kColorPreview, /*bold=*/false);
  }

  m_dateLabel->setText(formatDate(m_summary.date));
  applyRowTextStyle(m_dateLabel, mainColor, /*bold=*/unread);
}

void EmailItemWidget::setEmailSummary(const EmailSummary& summary)
{
  const bool contentChanged =
      (m_summary.uid != summary.uid || m_summary.from != summary.from || m_summary.to != summary.to ||
       m_summary.subject != summary.subject || m_summary.preview != summary.preview || m_summary.date != summary.date ||
       m_summary.isRead != summary.isRead);

  if (!contentChanged)
  {
    return;
  }

  const bool readChanged = (m_summary.isRead != summary.isRead);
  m_summary = summary;
  updateContent();
  if (readChanged)
  {
    emit unreadChanged();
  }
}

void EmailItemWidget::setSelected(bool selected)
{
  if (m_selected == selected)
  {
    return;
  }
  m_selected = selected;
  updateStyle();
  emit selectedChanged();
}

void EmailItemWidget::setUnread(bool unread)
{
  if (isUnread() == unread)
  {
    return;
  }
  m_summary.isRead = !unread;
  updateContent();
  emit unreadChanged();
}

void EmailItemWidget::setShowRecipient(bool showRecipient)
{
  if (m_showRecipient == showRecipient)
  {
    return;
  }
  m_showRecipient = showRecipient;
  updateContent();
}

void EmailItemWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    emit clicked(m_summary.uid);
  }
  QWidget::mousePressEvent(event);
}

void EmailItemWidget::updateStyle()
{
  // The only QSS-driven dynamic property used by styles.qss for this row is
  // [selected="true"]. Hover is handled natively by Qt's :hover pseudo-class.
  setProperty("selected", m_selected);

  style()->unpolish(this);
  style()->polish(this);
  update();
}

QString EmailItemWidget::formatDate(const QDateTime& dt)
{
  if (!dt.isValid())
  {
    return QString();
  }

  const QDate today = QDate::currentDate();
  const QDate emailDate = dt.date();

  if (emailDate == today)
  {
    return dt.time().toString(QStringLiteral("h:mm AP"));
  }
  if (emailDate == today.addDays(-1))
  {
    return QStringLiteral("Yesterday");
  }
  if (emailDate.year() == today.year())
  {
    return emailDate.toString(QStringLiteral("MMM d"));
  }
  return emailDate.toString(QStringLiteral("MMM d, yyyy"));
}

QSize EmailItemWidget::sizeHint() const
{
  return QSize(PREFERRED_WIDTH, WIDGET_MAX_HEIGHT);
}

QSize EmailItemWidget::minimumSizeHint() const
{
  return QSize(200, WIDGET_MIN_HEIGHT);
}
