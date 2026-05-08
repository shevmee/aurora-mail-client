#ifndef AI_ASSISTANT_DIALOG_HPP
#define AI_ASSISTANT_DIALOG_HPP

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace aurora::mail::ui
{

  /**
   * @class AIAssistantDialog
   * @brief Dialog for AI-powered text improvements with diff view.
   *
   * Shows original and improved text side by side, allowing user to
   * accept or reject the AI suggestions.
   */
  class AIAssistantDialog : public QDialog
  {
    Q_OBJECT

   public:
    enum class Mode
    {
      GrammarCheck,
      ImproveWriting,
      MakeFormal,
      MakeConcise
    };

    explicit AIAssistantDialog(QWidget* parent = nullptr);

    /**
     * @brief Sets the original text to be improved.
     */
    void setOriginalText(const QString& text);

    /**
     * @brief Sets the improved text from AI.
     */
    void setImprovedText(const QString& text);

    /**
     * @brief Gets the text to apply (improved or original if rejected).
     */
    QString getResultText() const;

    /**
     * @brief Idle state shown when the dialog opens (and after errors).
     *        The user must select an action and press the primary button to
     *        actually issue an AI request — selecting a mode in the combo
     *        box never triggers a request on its own.
     */
    void showIdle();

    /**
     * @brief Shows loading state while waiting for AI response.
     */
    void showLoading();

    /**
     * @brief Shows error message.
     */
    void showError(const QString& message);

    /**
     * @brief Shows the comparison view.
     */
    void showComparison();

    /**
     * @brief Returns true if user accepted the changes.
     */
    bool changesAccepted() const
    {
      return m_accepted;
    }

   signals:
    void modeChanged(Mode mode);

   private:
    void setupUi();
    void highlightDifferences();
    QString getModeDescription(Mode mode) const;

    QComboBox* m_modeCombo;
    QTextEdit* m_originalText;
    QTextEdit* m_improvedText;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QPushButton* m_applyButton;
    QPushButton* m_cancelButton;
    /**
     * Primary action: triggers an AI request for the currently-selected mode.
     * Used both for the first request and for re-running with a different
     * mode. Selecting a mode in the combo box does NOT trigger a request —
     * the user must click this button explicitly.
     */
    QPushButton* m_generateButton;
    QWidget* m_comparisonWidget;
    QWidget* m_loadingWidget;

    bool m_accepted = false;
  };

}  // namespace aurora::mail::ui

#endif  // AI_ASSISTANT_DIALOG_HPP
