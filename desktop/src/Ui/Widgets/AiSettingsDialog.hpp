#ifndef AI_SETTINGS_DIALOG_HPP
#define AI_SETTINGS_DIALOG_HPP

#include <QDialog>

namespace aurora::mail::app::ai {
class AIService;
}

namespace aurora::mail::ui {

/**
 * @brief Configure AI provider and API key (Gemini / Google AI Studio).
 */
class AiSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AiSettingsDialog(aurora::mail::app::ai::AIService* aiService, QWidget* parent = nullptr);

private slots:
    void onClearClicked();
    void openKeyHelpUrl() const;

private:
    aurora::mail::app::ai::AIService* m_ai = nullptr;
};

} // namespace aurora::mail::ui

#endif
