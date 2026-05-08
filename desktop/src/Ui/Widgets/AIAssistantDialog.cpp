#include "AIAssistantDialog.hpp"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QApplication>

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
    
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        Mode mode = static_cast<Mode>(m_modeCombo->currentData().toInt());
        emit modeChanged(mode);
    });

    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

    // Loading widget
    m_loadingWidget = new QWidget(this);
    auto* loadingLayout = new QVBoxLayout(m_loadingWidget);
    loadingLayout->setAlignment(Qt::AlignCenter);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // Indeterminate
    m_progressBar->setMinimumWidth(300);
    
    m_statusLabel = new QLabel(tr("Analyzing your text..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMaximumWidth(600);
    m_statusLabel->setStyleSheet("font-size: 14px; color: #666;");
    
    loadingLayout->addWidget(m_statusLabel);
    loadingLayout->addWidget(m_progressBar);
    mainLayout->addWidget(m_loadingWidget);

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

    mainLayout->addWidget(m_comparisonWidget);
    m_comparisonWidget->hide();

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_retryButton = new QPushButton(tr("Try Again"), this);
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        // Switch to loading state immediately so the user gets visual feedback
        // and cannot trigger multiple concurrent AI requests by clicking again.
        showLoading();
        Mode mode = static_cast<Mode>(m_modeCombo->currentData().toInt());
        emit modeChanged(mode);
    });
    buttonLayout->addWidget(m_retryButton);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);

    m_applyButton = new QPushButton(tr("Apply Changes"), this);
    m_applyButton->setDefault(true);
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

    // Start in loading state
    showLoading();
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

void AIAssistantDialog::showLoading()
{
    m_loadingWidget->show();
    m_comparisonWidget->hide();
    m_applyButton->setEnabled(false);
    m_retryButton->hide();
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
    m_retryButton->show();
}

void AIAssistantDialog::showComparison()
{
    m_loadingWidget->hide();
    m_comparisonWidget->show();
    m_applyButton->setEnabled(true);
    m_retryButton->show();
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
