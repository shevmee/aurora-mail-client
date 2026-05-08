#include "AiSettingsDialog.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "AI/AIService.hpp"

using aurora::mail::app::ai::AiProviderKind;
using aurora::mail::app::ai::AIService;

namespace aurora::mail::ui
{

  AiSettingsDialog::AiSettingsDialog(AIService* aiService, QWidget* parent) : QDialog(parent), m_ai(aiService)
  {
    setWindowTitle(tr("AI settings"));
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("Paste your API key from Google AI Studio. Keys are stored in the system keychain when "
           "available, not in the project source."));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("TextSecondary"));
    layout->addWidget(hint);

    auto* form = new QFormLayout();

    auto* providerCombo = new QComboBox(this);
    providerCombo->addItem(tr("Google Gemini (AI Studio)"), static_cast<int>(AiProviderKind::Gemini));
    providerCombo->setEnabled(false);
    form->addRow(tr("Provider:"), providerCombo);

    auto* keyEdit = new QLineEdit(this);
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setPlaceholderText(tr("API key"));
    keyEdit->setObjectName(QStringLiteral("AiApiKeyEdit"));
    form->addRow(tr("API key:"), keyEdit);

    auto* linkRow = new QHBoxLayout();
    auto* linkBtn = new QPushButton(tr("Open Google AI Studio (get API key)"), this);
    linkBtn->setObjectName(QStringLiteral("SecondaryButton"));
    linkRow->addWidget(linkBtn);
    linkRow->addStretch();
    form->addRow(QString(), linkRow);

    layout->addLayout(form);

    auto* status = new QLabel(this);
    status->setObjectName(QStringLiteral("TextMuted"));
    if (m_ai && m_ai->isConfigured())
    {
      status->setText(tr("Saved key: %1").arg(m_ai->getMaskedApiKey()));
      keyEdit->setPlaceholderText(tr("Leave empty to keep existing key; paste to replace."));
    }
    else
    {
      status->setText(tr("No API key saved."));
    }
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* saveBtn = new QPushButton(tr("Save"));
    auto* clearBtn = new QPushButton(tr("Clear key"));
    saveBtn->setDefault(true);
    buttons->addButton(saveBtn, QDialogButtonBox::AcceptRole);
    buttons->addButton(clearBtn, QDialogButtonBox::DestructiveRole);
    layout->addWidget(buttons);

    connect(
        saveBtn,
        &QPushButton::clicked,
        this,
        [this, keyEdit, status]()
        {
          if (!m_ai)
          {
            return;
          }
          const QString trimmed = keyEdit->text().trimmed();
          if (trimmed.isEmpty())
          {
            if (m_ai->isConfigured())
            {
              status->setText(tr("Existing key unchanged."));
              accept();
              return;
            }
            status->setText(tr("Enter an API key."));
            return;
          }
          m_ai->setApiKey(trimmed);
          keyEdit->clear();
          status->setText(tr("Saved: %1").arg(m_ai->getMaskedApiKey()));
          accept();
        });

    connect(clearBtn, &QPushButton::clicked, this, &AiSettingsDialog::onClearClicked);
    connect(linkBtn, &QPushButton::clicked, this, &AiSettingsDialog::openKeyHelpUrl);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  }

  void AiSettingsDialog::onClearClicked()
  {
    if (!m_ai)
    {
      return;
    }
    m_ai->clearApiKey();
    accept();
  }

  void AiSettingsDialog::openKeyHelpUrl() const
  {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://aistudio.google.com/apikey")));
  }

}  // namespace aurora::mail::ui
