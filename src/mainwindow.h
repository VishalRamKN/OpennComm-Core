/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#ifndef OPENNCOMM_MAINWINDOW_H
#define OPENNCOMM_MAINWINDOW_H

#include <QWidget>

#include "camera.h"
#include "caliboverlay.h"
#include "blinkcaloverlay.h"
#include "morseview.h"
#include "nosepointer.h"
#include "optionbutton.h"
#include "openncomm/answers.h"
#include "openncomm/head.h"
#include "openncomm/morse.h"
#include "openncomm/select.h"
#include "history.h"
#include "profile.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QThread;
class QComboBox;
class QDialog;
class QProgressBar;
class QTimer;
class QStackedWidget;
class QGridLayout;
class Speech;
class Generator;
class Listener;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(const QString &model_path, QWidget *parent = nullptr);
    ~MainWindow() override;

    /* For the offscreen screenshot tool in spike/. */
    void selectMode(int index);
    /* Paints a hold in progress, for the screenshot tool. */
    void previewHold(int index, double progress);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onFrame(const FaceFrame &frame);
    void onCameraFailed(const QString &message);
    void onLowFrameRate(double fps);
    void onSpeechFinished();
    void onAskClicked();
    void onModeChanged(int index);
    void onMorseUndo();
    void onMorseSpace();
    void onMorseClear();
    void onCalibrate();
    void onCalibrated(const oc_calib &result);
    void onSpeedChanged(int index);
    void onGapChanged(int index);
    void onAutoScanChanged(int index);
    void onAutoSendChanged(int index);
    void onGeneratorLoaded(const QString &model_name);
    void onGeneratorFailed(const QString &message);
    void onAnswersProduced(quint64 request_id, const QStringList &lines, double ms);
    void onListenerReady();
    void onListenerFailed(const QString &message);
    void onTranscribed(const QString &text, double ms);
    void onListenClicked();
    void onListeningEnded();
    void onEditProfile();
    void onOpenSettings();
    void onCalibrateBlinks();
    void onBlinksCalibrated(const oc_bands &bands);
    void onVoiceChanged(int index);
    void onAnswersTimedOut();
    void onOpenHistory();

private:
    void buildUi();
    QWidget *buildOptionsPage();
    QWidget *buildMorsePage();
    void renderMorse(char preview, int autosend_remaining_ms = -1);
    void speakMorseMessage();
    void loadSettings();
    void saveBlinkBands(const oc_bands &bands);
    void applyBands(oc_bands bands);
    void showPhrasebook();
    void beginPendingAnswers();
    void updateFullScreenButton();
    void updateSubsystems();
    void saveCalibration(const oc_calib &c);
    void setOptions(const oc_options &options);
    QString patientName() const;
    void applyProfile();
    void highlight(int index);
    void setHold(int index, double progress);
    void balanceOptionText();
    void commit(int index);
    void setStatus(const QString &text);

    enum Mode { ModeBlink = 0, ModeHead = 1, ModeMorse = 2 };
    Mode mode_ = ModeBlink;

    oc_selector blink_{};
    oc_head_selector head_{};
    oc_morse morse_{};
    bool options_loaded_ = false;
    bool input_locked_ = false;
    bool model_ready_ = false;
    bool model_failed_ = false;
    bool listener_failed_ = false;
    bool camera_ok_ = false;
    bool generating_ = false;
    int committed_index_ = -1;
    qint64 last_prompt_ms_ = 0;

    QList<OptionButton *> buttons_;
    QWidget *options_page_ = nullptr;
    NosePointer *nose_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QLabel *morse_text_ = nullptr;
    MorseEntry *morse_entry_ = nullptr;
    QLabel *morse_countdown_ = nullptr;
    QLabel *morse_hint_ = nullptr;
    QList<MorseKey *> morse_keys_;
    bool speaking_morse_ = false;
    QLabel *preview_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *diagnostics_ = nullptr;
    QLabel *subsystems_ = nullptr;
    QLineEdit *question_ = nullptr;
    QPushButton *profile_button_ = nullptr;
    QPushButton *ask_ = nullptr;
    QPushButton *listen_ = nullptr;
    QPushButton *calibrate_ = nullptr;
    QPushButton *calibrate_blinks_ = nullptr;
    QPushButton *settings_toggle_ = nullptr;
    QPushButton *exit_full_ = nullptr;
    QPushButton *quit_ = nullptr;
    QPushButton *history_button_ = nullptr;
    QWidget *settings_panel_ = nullptr;
    QDialog *settings_dialog_ = nullptr;
    CalibOverlay *calib_overlay_ = nullptr;
    BlinkCalOverlay *blink_overlay_ = nullptr;
    QComboBox *mode_box_ = nullptr;
    QComboBox *speed_box_ = nullptr;
    QComboBox *gap_box_ = nullptr;
    QComboBox *scan_box_ = nullptr;
    QComboBox *send_box_ = nullptr;
    QComboBox *voice_box_ = nullptr;
    QProgressBar *level_ = nullptr;
    bool listening_ = false;

    /* Bands measured from this patient, if blink setup has been run. They
     * override the speed presets, which are only ever guesses. */
    bool custom_bands_ = false;
    oc_bands bands_{};

    /* While true the four buttons are showing "writing answers" rather than
     * anything selectable, because the model has been asked and has not
     * replied yet. See beginPendingAnswers(). */
    bool awaiting_answers_ = false;
    QTimer *answer_timeout_ = nullptr;

    /* Rises with each question asked. A reply carrying an older id belongs to a
     * question that has since been superseded and is discarded. */
    quint64 request_id_ = 0;
    oc_intent intent_ = OC_INTENT_GENERAL;
    QStringList history_;

    PatientProfile profile_;

    /* Everything the patient has said, kept on this machine. Owned here so it
     * outlives the dialog that shows it. */
    History history_db_;
    /* Where the answers on screen came from, so the record says whether the
     * patient chose something the model wrote or something built in. */
    QString options_source_ = QStringLiteral("phrasebook");
    /* The first-run form is opened once the window is actually on screen, not
     * from the constructor: a modal dialog over a window that has not been
     * painted yet shows the caregiver a grey rectangle. */
    bool asked_for_profile_ = false;

    Speech *speech_ = nullptr;
    Generator *generator_ = nullptr;
    QThread *generator_thread_ = nullptr;
    Listener *listener_ = nullptr;
    QThread *listener_thread_ = nullptr;
    bool listener_ready_ = false;
    CameraWorker *camera_ = nullptr;
    QThread *camera_thread_ = nullptr;
};

#endif
