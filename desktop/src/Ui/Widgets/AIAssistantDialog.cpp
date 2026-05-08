#include "AIAssistantDialog.hpp"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QApplication>
#include <QSizePolicy>

namespace aurora::mail::ui {

AIAssistantDialog::AIAssistantDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
}

void AIAssistantDialog::setupUi()
{
    setWindowTitle(tr("AI Writing Assistant"));
    setMinimumSize(800, 500);
    resize(900, 600);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Mode selector
    auto* modeLayout = new QHBoxLayout();
    auto* modeLabel = new QLabel(tr("Action:"), this);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Check Grammar"), static_cast<int>(Mode::GrammarCheck));
    m_modeCombo->addItem(tr("Improve Writing"), static_cast<int>(Mode::ImproveWriting));
    m_modeCombo->addItem(tr("Make More Formal"), static_cast<int>(Mode::MakeFormal));
    m_modeCombo->addItem(tr("Make Concise"), static_cast<int>(Mode::MakeConcise));
    m_modeCombo->setMinimumWidth(200);

    // Intentionally NO connection on currentIndexChanged: changing the mode
    // must not fire a network request. The user picks an option here, then
    // clicks the Generate button below to actually invoke the AI.

    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

    // Loading / idle / error placeholder. Must expand to claim the same
    // vertical space the (hidden) comparison widget would take, otherwise
    // the status label hugs the top of the dialog instead of being centered.
    m_loadingWidget = new QWidget(this);
    m_loadingWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* loadingLayout = new QVBoxLayout(m_loadingWidget);
    loadingLayout->setContentsMargins(0, 0, 0, 0);
    loadingLayout->setSpacing(12);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // Indeterminate
    m_progressBar->setMinimumWidth(300);
    m_progressBar->setMaximumWidth(400);

    m_statusLabel = new QLabel(tr("Analyzing your text..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMaximumWidth(600);
    m_statusLabel->setStyleSheet("font-size: 14px; color: #666;");

    // Vertical stretches above and below center the content within the
    // expanded loading widget. AlignHCenter on each addWidget centers the
    // sub-widgets horizontally (their widths are capped by setMaximumWidth,
    // so without the alignment hint they'd be left-aligned by default).
    loadingLayout->addStretch(1);
    loadingLayout->addWidget(m_statusLabel, 0, Qt::AlignHCenter);
    loadingLayout->addWidget(m_progressBar, 0, Qt::AlignHCenter);
    loadingLayout->addStretch(1);

    mainLayout->addWidget(m_loadingWidget, /*stretch=*/1);

    // Comparison widget (two columns)
    m_comparisonWidget = new QWidget(this);
    auto* comparisonLayout = new QHBoxLayout(m_comparisonWidget);
    comparisonLayout->setSpacing(16);

    // Original text
    auto* originalGroup = new QGroupBox(tr("Original Text"), this);
    auto* originalLayout = new QVBoxLayout(originalGroup);
    m_originalText = new QTextEdit(this);
    m_originalText->setReadOnly(true);
    m_originalText->setStyleSheet(R"(
        QTextEdit {
            background-color: #fff8f8;
            color: #333333;
            border: 1px solid #ffcccc;
            border-radius: 4px;
            padding: 8px;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            font-size: 14px;
            line-height: 1.5;
        }
    )");
    originalLayout->addWidget(m_originalText);
    comparisonLayout->addWidget(originalGroup);

    // Improved text
    auto* improvedGroup = new QGroupBox(tr("AI Suggestion"), this);
    auto* improvedLayout = new QVBoxLayout(improvedGroup);
    m_improvedText = new QTextEdit(this);
    m_improvedText->setReadOnly(true);
    m_improvedText->setStyleSheet(R"(
        QTextEdit {
            background-color: #f8fff8;
            color: #333333;
            border: 1px solid #ccffcc;
            border-radius: 4px;
            padding: 8px;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            font-size: 14px;
            line-height: 1.5;
        }
    )");
    improvedLayout->addWidget(m_improvedText);
    comparisonLayout->addWidget(improvedGroup);

    mainLayout->addWidget(m_comparisonWidget, /*stretch=*/1);
    m_comparisonWidget->hide();

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);

    // Single primary action: requests an AI completion for the currently
    // selected mode. Used for the very first request AND for any subsequent
    // re-runs (replaces the old separate "Try Again" button — its only
    // behaviour was identical to this).
    m_generateButton = new QPushButton(tr("Generate"), this);
    m_generateButton->setDefault(true);
    m_generateButton->setStyleSheet(R"(
        QPushButton {
            background-color: #1d9bf0;
            color: white;
            border: none;
            padding: 8px 20px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #1a8cd8;
        }
        QPushButton:disabled {
            background-color: #cccccc;
        }
    )");
    connect(m_generateButton, &QPushButton::clicked, this, [this]() {
        // Switch to loading state immediately so the user gets visual
        // feedback and cannot trigger multiple concurrent AI requests by
        // clicking again. Only after this explicit click do we emit the
        // mode signal that fires off the network request.
        showLoading();
        const Mode mode = static_cast<Mode>(m_modeCombo->currentData().toInt());
        emit modeChanged(mode);
    });
    buttonLayout->addWidget(m_generateButton);

    m_applyButton = new QPushButton(tr("Apply Changes"), this);
    m_applyButton->setStyleSheet(R"(
        QPushButton {
            background-color: #4CAF50;
            color: white;
            border: none;
            padding: 8px 20px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #45a049;
        }
        QPushButton:disabled {
            background-color: #cccccc;
        }
    )");
    m_applyButton->setEnabled(false);
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        m_accepted = true;
        accept();
    });
    buttonLayout->addWidget(m_applyButton);

    mainLayout->addLayout(buttonLayout);

    // Start in idle state: user must select an action and click Generate.
    showIdle();
}

void AIAssistantDialog::setOriginalText(const QString& text)
{
    m_originalText->setPlainText(text);
}

void AIAssistantDialog::setImprovedText(const QString& text)
{
    m_improvedText->setPlainText(text);
    highlightDifferences();
}

QString AIAssistantDialog::getResultText() const
{
    if (m_accepted) {
        return m_improvedText->toPlainText();
    }
    return m_originalText->toPlainText();
}

void AIAssistantDialog::showIdle()
{
    m_loadingWidget->show();
    m_comparisonWidget->hide();
    m_progressBar->hide();
    m_statusLabel->setText(tr("Choose an option above and click Generate to ask the AI."));
    m_statusLabel->setStyleSheet("font-size: 14px; color: #666;");
    m_applyButton->setEnabled(false);
    m_generateButton->setEnabled(true);
    m_generateButton->setText(tr("Generate"));
    m_generateButton->show();
}

void AIAssistantDialog::showLoading()
{
    m_loadingWidget->show();
    m_comparisonWidget->hide();
    m_applyButton->setEnabled(false);
    // Disable (rather than hide) the primary button while a request is in
    // flight so its position stays stable and double-clicks can't queue a
    // second concurrent request.
    m_generateButton->setEnabled(false);
    m_statusLabel->setText(tr("Analyzing your text..."));
    m_statusLabel->setStyleSheet("font-size: 14px; color: #666;");
    m_progressBar->show();
}

void AIAssistantDialog::showError(const QString& message)
{
    m_loadingWidget->show();
    m_comparisonWidget->hide();
    m_progressBar->hide();
    m_statusLabel->setText(tr("Error: %1").arg(message));
    m_statusLabel->setStyleSheet("font-size: 14px; color: #d32f2f;");
    m_applyButton->setEnabled(false);
    m_generateButton->setEnabled(true);
    m_generateButton->setText(tr("Try Again"));
    m_generateButton->show();
}

void AIAssistantDialog::showComparison()
{
    m_loadingWidget->hide();
    m_comparisonWidget->show();
    m_applyButton->setEnabled(true);
    m_generateButton->setEnabled(true);
    m_generateButton->setText(tr("Try Again"));
    m_generateButton->show();
}

void AIAssistantDialog::highlightDifferences()
{
    // Simple highlighting - could be enhanced with a proper diff algorithm
    QString original = m_originalText->toPlainText();
    QString improved = m_improvedText->toPlainText();
    
    if (original == improved) {
        m_statusLabel->setText(tr("No changes needed - your text is already great!"));
        m_statusLabel->setStyleSheet("font-size: 14px; color: #4CAF50;");
        m_loadingWidget->show();
        m_progressBar->hide();
        m_comparisonWidget->hide();
        m_applyButton->setEnabled(false);
        return;
    }
    
    // For now, just show the texts as-is
    // A more sophisticated implementation would use a diff algorithm
    // to highlight specific changes
}

QString AIAssistantDialog::getModeDescription(Mode mode) const
{
    switch (mode) {
        case Mode::GrammarCheck:
            return "Checking grammar and spelling...";
        case Mode::ImproveWriting:
            return "Improving clarity and style...";
        case Mode::MakeFormal:
            return "Making text more professional...";
        case Mode::MakeConcise:
            return "Making text more concise...";
    }
    return "Processing...";
}

} // namespace aurora::mail::ui
