/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "mainwindow.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QDateTime>
#include <QHash>
#include <QSignalBlocker>
#include <cmath>
#include <QFileInfo>
#include <vector>
#include <QFrame>
#include <QGridLayout>
#include <QStackedWidget>
#include <QSettings>
#include <QTimer>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QGuiApplication>

#include <cstdio>
#include <cstring>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "combobox.h"
#include "openncomm/ear.h"
#include "openncomm/gaze.h"
#include "generator.h"
#include "historydialog.h"
#include "listener.h"
#include "paths.h"
#include "speech.h"

namespace {
/* Throttle on the "ask something first" prompt, so a patient experimenting
 * before any question is asked is told once, not on every blink. */
constexpr qint64 kPromptCooldownMs = 8000;

/* "en_US-amy-medium" is a filename, not something to offer a caregiver who is
 * choosing how the patient will sound. */
QString voiceLabel(const QString &id)
{
    const QStringList parts = id.split(QLatin1Char('-'));
    if (parts.size() < 2) return id;
    QString name = parts[1];
    name[0] = name[0].toUpper();
    static const QHash<QString, QString> known = {
        { QStringLiteral("amy"),  QStringLiteral("Amy \u2014 woman, American") },
        { QStringLiteral("joe"),  QStringLiteral("Joe \u2014 man, American") },
    };
    return known.value(parts[1], name);
}

const char *kStyle = R"(
QWidget            { background: #0a0e18; color: #e9f0fa; font-size: 14px; }
QLabel#status      { font-size: 20px; font-weight: 500; color: #e9f0fa; padding: 2px; }
QLabel#subsys      { font-size: 12px; }
QLabel#diag        { color: #6b7a96; font-size: 11px; }
QLabel#sectionHint { color: #6b7a96; font-size: 12px; }

QProgressBar#level { background:#0f1626; border:1px solid #2a3a5e; border-radius:9px; }
QProgressBar#level::chunk { background:#49e08b; border-radius:7px; margin:2px; }

/* Destructive, and it should look it: quitting, and deleting what the patient
 * has said. */
QPushButton#exitfs { background:#4a1f2c; border-color:#9b4a60; color:#ffdde5; }
QPushButton#exitfs:hover  { background:#612839; border-color:#c4657f; }
QPushButton#exitfs:disabled { background:#241520; border-color:#3a2530; color:#6b4a55; }

QPushButton        { background:#18223a; border:1px solid #2a3a5e; border-radius:9px;
                     padding:9px 16px; color:#cfdcf0; }
QPushButton:hover  { background:#22304f; border-color:#3d5484; }
QPushButton:disabled { color:#55627c; background:#141c2f; border-color:#212c46; }
QPushButton#primary { background:#23407e; border-color:#6ea8ff; color:#ffffff; font-weight:600; }
QPushButton#primary:hover { background:#2b4d95; }
QPushButton#mic     { background:#1d2c4a; border-color:#3d5484; font-weight:600; }
QPushButton#mic:disabled { background:#141c2f; }

QLineEdit          { background:#0f1626; border:1px solid #2a3a5e; border-radius:9px;
                     padding:11px 13px; font-size:15px; selection-background-color:#23407e; }
QLineEdit:focus    { border-color:#6ea8ff; }
/* The default combo is a flat slab with a tiny system arrow that reads as
 * decoration rather than as something to press. These give it a pressable
 * right-hand well, a drawn chevron, and a popup with rows big enough to hit
 * without aiming. */
QComboBox          { background:#18223a; border:1px solid #2a3a5e; border-radius:9px;
                     padding:9px 40px 9px 14px; color:#dce7f7; font-size:14px;
                     min-height:22px; }
QComboBox:hover    { background:#22304f; border-color:#3d5484; }
QComboBox:focus    { border-color:#6ea8ff; }
QComboBox:on       { background:#22304f; border-color:#6ea8ff; }
QComboBox:disabled { color:#55627c; background:#141c2f; border-color:#212c46; }
QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right;
                       width:32px; border-left:1px solid #2a3a5e;
                       border-top-right-radius:9px; border-bottom-right-radius:9px;
                       background:#1d2842; }
QComboBox::drop-down:hover { background:#27365a; }
QComboBox::down-arrow:on { top:1px; }
QComboBox QAbstractItemView { background:#131c30; border:1px solid #3d5484;
                              border-radius:9px; padding:6px;
                              outline:none; color:#dce7f7;
                              selection-background-color:#23407e;
                              selection-color:#ffffff; }
QComboBox QAbstractItemView::item { min-height:34px; padding:4px 10px;
                                    border-radius:6px; }
QComboBox QAbstractItemView::item:hover { background:#1e2c4a; }
QPlainTextEdit     { background:#0f1626; border:1px solid #2a3a5e; border-radius:9px;
                     padding:8px 10px; font-size:14px; selection-background-color:#23407e; }
QPlainTextEdit:focus { border-color:#6ea8ff; }
QSpinBox           { background:#0f1626; border:1px solid #2a3a5e; border-radius:9px;
                     padding:10px 12px; font-size:15px; }
QSpinBox:focus     { border-color:#6ea8ff; }
QDialog            { background:#0a0e18; }

QLabel#morseText   { background:#0f1626; border:2px solid #2a3a5e; border-radius:16px;
                     padding:20px; font-size:36px; font-weight:600; letter-spacing:2px; }
QLabel#morseHint   { font-size:13px; color:#6b7a96; }
QLabel#morseCountdown { font-size:19px; font-weight:600; color:#f0b866;
                        background:#2a2010; border:1px solid #6b5220;
                        border-radius:10px; padding:8px; }
QFrame#divider     { background:#1b2540; max-height:1px; border:none; }

QTreeView          { background:#0f1626; border:1px solid #2a3a5e; border-radius:9px;
                     outline:none; font-size:14px; }
QTreeView::item    { padding:7px 6px; border:none; color:#dce7f7; }
QTreeView::item:selected { background:#23407e; color:#ffffff; }
QTreeView::branch  { background:#0f1626; }
QHeaderView::section { background:#18223a; color:#9fb4d8; border:none;
                       border-bottom:1px solid #2a3a5e; padding:9px 6px; font-size:12px; }

QScrollBar:vertical   { background:#0f1626; width:12px; margin:0; border:none; }
QScrollBar::handle:vertical { background:#2a3a5e; border-radius:6px; min-height:36px; }
QScrollBar::handle:vertical:hover { background:#3d5484; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal { background:#0f1626; height:12px; margin:0; border:none; }
QScrollBar::handle:horizontal { background:#2a3a5e; border-radius:6px; min-width:36px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:none; }
)";

}

MainWindow::MainWindow(const QString &model_path, QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("OpennComm"));
    setStyleSheet(QString::fromLatin1(kStyle));
    buildUi();

    oc_selector_init(&blink_, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
    /* mirror_x defaults to true: a front-facing camera hands back an un-flipped
     * sensor image, so the nose moves opposite to the way the patient turns. */
    oc_head_init(&head_, true);
    oc_morse_init(&morse_, oc_blink_bands_for(OC_SPEED_NORMAL), 1500, 0);

    speech_ = new Speech(this);
    connect(speech_, &Speech::finished, this, &MainWindow::onSpeechFinished);
    if (!speech_->available())
        setStatus(QStringLiteral("No speech engine found — install espeak-ng to hear answers."));
    else
        subsystems_->setToolTip(speech_->engineName());

    camera_ = new CameraWorker(model_path);
    camera_thread_ = new QThread(this);
    camera_->moveToThread(camera_thread_);
    connect(camera_thread_, &QThread::started, camera_, &CameraWorker::start);
    connect(camera_, &CameraWorker::frameReady, this, &MainWindow::onFrame);
    connect(camera_, &CameraWorker::failed, this, &MainWindow::onCameraFailed);
    connect(camera_, &CameraWorker::lowFrameRate, this, &MainWindow::onLowFrameRate);
    connect(camera_thread_, &QThread::finished, camera_, &QObject::deleteLater);
    camera_thread_->start();

    /* Model loading takes seconds and generation takes seconds more, so the
     * generator gets its own thread and the UI never blocks on either. Until it
     * reports ready -- and for good, if there is no model -- the phrasebook is
     * simply what the patient gets. */
    generator_ = new Generator(paths::model(QStringLiteral("qwen2.5-1.5b-instruct-q4_k_m.gguf")));
    generator_thread_ = new QThread(this);
    generator_->moveToThread(generator_thread_);
    connect(generator_thread_, &QThread::started, generator_, &Generator::load);
    connect(generator_, &Generator::loaded, this, &MainWindow::onGeneratorLoaded);
    connect(generator_, &Generator::failed, this, &MainWindow::onGeneratorFailed);
    connect(generator_, &Generator::produced, this, &MainWindow::onAnswersProduced);
    connect(generator_thread_, &QThread::finished, generator_, &QObject::deleteLater);
    generator_thread_->start();

    listener_ = new Listener(paths::model(QStringLiteral("ggml-base.en-q5_1.bin")));
    listener_thread_ = new QThread(this);
    listener_->moveToThread(listener_thread_);
    connect(listener_thread_, &QThread::started, listener_, &Listener::load);
    connect(listener_, &Listener::ready, this, &MainWindow::onListenerReady);
    connect(listener_, &Listener::failed, this, &MainWindow::onListenerFailed);
    connect(listener_, &Listener::transcribed, this, &MainWindow::onTranscribed);
    connect(listener_, &Listener::listeningEnded, this, &MainWindow::onListeningEnded);
    connect(listener_, &Listener::level, this, [this](float rms) {
        /* A square root, not the raw value: speech sits in the bottom tenth of
         * a linear scale and a meter that never leaves its first pixel tells
         * the caregiver nothing. */
        if (level_) level_->setValue(qBound(0, int(std::sqrt(qMin(rms, 0.3f) / 0.3f) * 100), 100));
    });
    connect(listener_thread_, &QThread::finished, listener_, &QObject::deleteLater);
    listener_thread_->start();

    blink_overlay_ = new BlinkCalOverlay(this);
    blink_overlay_->hide();
    connect(blink_overlay_, &BlinkCalOverlay::completed, this, &MainWindow::onBlinksCalibrated);
    connect(blink_overlay_, &BlinkCalOverlay::cancelled, this, [this] {
        blink_overlay_->hide();
        setStatus(QStringLiteral("Blink setup cancelled."));
    });

    calib_overlay_ = new CalibOverlay(this);
    calib_overlay_->hide();
    connect(calib_overlay_, &CalibOverlay::completed, this, &MainWindow::onCalibrated);
    connect(calib_overlay_, &CalibOverlay::cancelled, this, [this] {
        calib_overlay_->hide();
        setStatus(QStringLiteral("Calibration cancelled."));
    });

    loadSettings();
    updateSubsystems();

    oc_options opening;
    oc_phrasebook(OC_INTENT_GENERAL, nullptr, &opening);
    setOptions(opening);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (calib_overlay_) calib_overlay_->setGeometry(rect());
    if (blink_overlay_) blink_overlay_->setGeometry(rect());
    if (nose_ && options_page_) nose_->setGeometry(options_page_->rect());
    updateFullScreenButton();
    QTimer::singleShot(0, this, &MainWindow::balanceOptionText);
}

/* QSettings lands in ~/.config/OpennComm/OpennComm.conf, which is where the XDG
 * spec says it belongs. Calibration in particular is worth persisting: it takes
 * the patient forty seconds of deliberate head movement, and asking for that
 * again on every launch would be its own small cruelty. */
void MainWindow::loadSettings()
{
    QSettings s;
    profile_ = PatientProfile::load();
    applyProfile();
    const int speed = s.value(QStringLiteral("blink_speed"), OC_SPEED_NORMAL).toInt();
    speed_box_->setCurrentIndex(qBound(0, speed, OC_SPEED_COUNT - 1));
    onSpeedChanged(speed_box_->currentIndex());

    gap_box_->setCurrentIndex(s.value(QStringLiteral("morse_gap_index"), 1).toInt());
    onGapChanged(gap_box_->currentIndex());

    scan_box_->setCurrentIndex(s.value(QStringLiteral("auto_scan_index"), 0).toInt());
    onAutoScanChanged(scan_box_->currentIndex());

    send_box_->setCurrentIndex(s.value(QStringLiteral("auto_send_index"), 0).toInt());
    onAutoSendChanged(send_box_->currentIndex());

    /* Voices are listed from disk, so a build with only one installed shows
     * one rather than an option that cannot be selected. */
    {
        QSignalBlocker block(voice_box_);
        const QStringList ids = speech_->voices();
        for (const QString &id : ids) voice_box_->addItem(voiceLabel(id), id);
        if (ids.isEmpty()) {
            voice_box_->addItem(speech_->engineName());
            voice_box_->setEnabled(false);
        } else {
            const int at = voice_box_->findData(speech_->currentVoice());
            voice_box_->setCurrentIndex(at < 0 ? 0 : at);
        }
    }

    /* Bands measured from this patient beat any preset, so they are applied
     * after the preset above rather than before it. */
    if (s.value(QStringLiteral("blinkcal/valid"), false).toBool()) {
        oc_bands b{};
        b.ignore_ms    = s.value(QStringLiteral("blinkcal/ignore_ms")).toInt();
        b.short_max_ms = s.value(QStringLiteral("blinkcal/short_max_ms")).toInt();
        b.long_ms      = s.value(QStringLiteral("blinkcal/long_ms")).toInt();
        if (b.short_max_ms > 0 && b.long_ms > b.short_max_ms) {
            custom_bands_ = true;
            applyBands(b);
            speed_box_->addItem(QStringLiteral("Measured from this patient"));
            QSignalBlocker block(speed_box_);
            speed_box_->setCurrentIndex(OC_SPEED_COUNT);
        }
    }

    if (s.value(QStringLiteral("calib/valid"), false).toBool()) {
        oc_calib c{};
        c.valid   = true;
        c.min_x   = s.value(QStringLiteral("calib/min_x")).toDouble();
        c.min_y   = s.value(QStringLiteral("calib/min_y")).toDouble();
        c.range_x = s.value(QStringLiteral("calib/range_x")).toDouble();
        c.range_y = s.value(QStringLiteral("calib/range_y")).toDouble();
        c.pad_x   = s.value(QStringLiteral("calib/pad_x")).toDouble();
        c.pad_y   = s.value(QStringLiteral("calib/pad_y")).toDouble();
        oc_head_set_calibration(&head_, c);
        setStatus(QStringLiteral("Ready — using the saved head calibration."));
    }
}

/* The button carries the name, so the caregiver can see at a glance who the
 * application currently thinks it is speaking for. Getting that wrong -- one
 * patient's notes shaping another patient's answers -- is worth a glance. */
void MainWindow::applyProfile()
{
    if (!profile_button_) return;
    const QString name = profile_.name.trimmed();
    profile_button_->setText(name.isEmpty() ? QStringLiteral("Add patient details\u2026")
                                            : QStringLiteral("Patient: %1").arg(name));

    if (!history_button_) return;
    if (!history_db_.available()) {
        history_button_->setText(QStringLiteral("History unavailable"));
        history_button_->setEnabled(false);
        history_button_->setToolTip(history_db_.error());
        return;
    }
    const int kept = history_db_.count();
    history_button_->setText(kept == 0
        ? QStringLiteral("History \u2014 nothing yet")
        : QStringLiteral("History \u2014 %1 answer%2").arg(kept).arg(kept == 1 ? QString()
                                                                    : QStringLiteral("s")));
}

void MainWindow::onEditProfile()
{
    /* Nothing said during setup may be taken as an answer. */
    speech_->stop();
    input_locked_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    PatientDialog dialog(profile_, this);
    const bool saved = dialog.exec() == QDialog::Accepted;

    /* Either way the caregiver has now been asked, so the form stops opening
     * itself on every launch. Skipping is an answer. */
    if (saved) {
        profile_ = dialog.profile();
    } else {
        profile_.answered = true;
    }
    profile_.save();
    applyProfile();

    if (saved && !profile_.name.trimmed().isEmpty())
        setStatus(QStringLiteral("Ready \u2014 speaking for %1.").arg(profile_.name.trimmed()));
}

/* Ask on the first launch only, once the window is up. */
void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateFullScreenButton();
    if (asked_for_profile_ || profile_.answered) return;
    /* The offscreen platform has no one to answer a modal dialog, and exec()
     * there would simply hang -- which is what the screenshot tool in spike/
     * runs under. */
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    asked_for_profile_ = true;
    QTimer::singleShot(0, this, &MainWindow::onEditProfile);
}

/* Full screen is the working state -- the patient needs the four answers as
 * large as the screen allows -- but a caregiver must never be trapped in it,
 * so F11 toggles and Escape always returns to a window. */
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const bool full = isFullScreen();
    if (event->key() == Qt::Key_F11) {
        if (full) showNormal(); else showFullScreen();
        updateFullScreenButton();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && full) {
        showNormal();
        updateFullScreenButton();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

/* ---- settings, as a dialog ----------------------------------------------- */

void MainWindow::onOpenHistory()
{
    /* Nothing read here may be spoken, and nothing the patient does while it
     * is open may count as an answer. */
    speech_->stop();
    input_locked_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    HistoryDialog dialog(&history_db_, this);
    dialog.exec();
    applyProfile();   /* the count on the button may have changed */
}

void MainWindow::onOpenSettings()
{
    /* Nothing a caregiver does in here may be read as the patient answering. */
    speech_->stop();
    input_locked_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    if (!settings_dialog_) {
        settings_dialog_ = new QDialog(this);
        settings_dialog_->setWindowTitle(QStringLiteral("Settings"));
        settings_dialog_->setMinimumWidth(720);
        auto *root = new QVBoxLayout(settings_dialog_);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(14);
        settings_panel_->setParent(settings_dialog_);
        settings_panel_->show();
        root->addWidget(settings_panel_);

        auto *footer = new QHBoxLayout;
        footer->setSpacing(10);
        footer->addWidget(exit_full_);
        footer->addWidget(quit_);
        footer->addStretch(1);

        auto *close = new QPushButton(QStringLiteral("Done"), settings_dialog_);
        close->setObjectName(QStringLiteral("primary"));
        close->setMinimumHeight(38);
        close->setDefault(true);
        connect(close, &QPushButton::clicked, settings_dialog_, &QDialog::accept);
        footer->addWidget(close);
        root->addLayout(footer);
    }
    updateFullScreenButton();
    applyProfile();   /* refreshes the history count on its button */
    settings_dialog_->exec();
}

/* ---- blink calibration ---------------------------------------------------- */

void MainWindow::onCalibrateBlinks()
{
    speech_->stop();
    input_locked_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    blink_overlay_->setGeometry(rect());
    blink_overlay_->show();
    blink_overlay_->raise();
    setStatus(QStringLiteral("Blink setup \u2014 press Start when the patient is ready."));
}

void MainWindow::onBlinksCalibrated(const oc_bands &bands)
{
    blink_overlay_->hide();
    custom_bands_ = true;
    applyBands(bands);
    saveBlinkBands(bands);

    /* The preset dropdown would otherwise still be showing "Normal" while
     * something else entirely is in force. */
    if (speed_box_->count() == OC_SPEED_COUNT)
        speed_box_->addItem(QStringLiteral("Measured from this patient"));
    {
        QSignalBlocker block(speed_box_);
        speed_box_->setCurrentIndex(OC_SPEED_COUNT);
    }

    setStatus(QStringLiteral(
        "Blinks calibrated \u2014 short up to %1 ms, a hold past %2 ms selects.")
                  .arg(bands.short_max_ms).arg(bands.long_ms));
}

void MainWindow::applyBands(oc_bands bands)
{
    bands_ = bands;
    blink_.tracker.bands = bands;
    morse_.tracker.bands = bands;
    oc_blink_tracker_reset(&blink_.tracker);
    oc_blink_tracker_reset(&morse_.tracker);
}

void MainWindow::saveBlinkBands(const oc_bands &bands)
{
    QSettings s;
    s.setValue(QStringLiteral("blinkcal/valid"), true);
    s.setValue(QStringLiteral("blinkcal/ignore_ms"), bands.ignore_ms);
    s.setValue(QStringLiteral("blinkcal/short_max_ms"), bands.short_max_ms);
    s.setValue(QStringLiteral("blinkcal/long_ms"), bands.long_ms);
}

void MainWindow::onVoiceChanged(int index)
{
    const QString id = voice_box_->itemData(index).toString();
    if (id.isEmpty() || !speech_->setVoice(id)) return;
    subsystems_->setToolTip(speech_->engineName());
    setStatus(QStringLiteral("Voice: %1.").arg(voice_box_->itemText(index)));
}

/* Only offered when there is something to exit. */
void MainWindow::updateFullScreenButton()
{
    /* Guarded on the dialog existing, not just on full screen: before it is
     * built this button is in no layout, and showing a widget that no layout
     * owns puts it in the top-left corner of the window. */
    if (exit_full_) exit_full_->setVisible(settings_dialog_ != nullptr && isFullScreen());
}

void MainWindow::saveCalibration(const oc_calib &c)
{
    QSettings s;
    s.setValue(QStringLiteral("calib/valid"), c.valid);
    s.setValue(QStringLiteral("calib/min_x"), c.min_x);
    s.setValue(QStringLiteral("calib/min_y"), c.min_y);
    s.setValue(QStringLiteral("calib/range_x"), c.range_x);
    s.setValue(QStringLiteral("calib/range_y"), c.range_y);
    s.setValue(QStringLiteral("calib/pad_x"), c.pad_x);
    s.setValue(QStringLiteral("calib/pad_y"), c.pad_y);
}

void MainWindow::onCalibrate()
{
    /* Nothing the setup screen provokes may count as an answer, so stop any
     * speech and drop whatever the selectors were holding. */
    speech_->stop();
    input_locked_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    calib_overlay_->setGeometry(rect());
    calib_overlay_->show();
    calib_overlay_->raise();
    setStatus(QStringLiteral("Calibrating — press Start when the patient is ready."));
}

void MainWindow::onCalibrated(const oc_calib &result)
{
    oc_head_set_calibration(&head_, result);
    saveCalibration(result);
    calib_overlay_->hide();
    setStatus(QStringLiteral("Calibrated — head mode now uses this patient's own range."));
}

MainWindow::~MainWindow()
{
    if (listener_thread_) {
        listener_thread_->quit();
        listener_thread_->wait(5000);
    }
    if (generator_thread_) {
        generator_thread_->quit();
        generator_thread_->wait(5000);
    }
    if (camera_thread_) {
        QMetaObject::invokeMethod(camera_, "stop", Qt::BlockingQueuedConnection);
        camera_thread_->quit();
        camera_thread_->wait(2000);
    }
}

void MainWindow::selectMode(int index) { mode_box_->setCurrentIndex(index); }

void MainWindow::previewHold(int index, double progress)
{
    highlight(index);
    setHold(index, progress);
}

void MainWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(12);

    /* Row one is the caregiver's actual job: ask a question. Everything else
     * about running the application is smaller and further away. */
    auto *ask_row = new QHBoxLayout;
    ask_row->setSpacing(10);

    /* A geometric shape, not the microphone emoji it used to be: emoji need a
     * colour emoji font, and on a minimal install -- verified in an Ubuntu
     * container with no fonts beyond the default -- it drew as a replacement
     * box. U+25CF is in DejaVu, which every desktop Linux ships. */
    listen_ = new QPushButton(QStringLiteral("\u25CF  Ask aloud"), this);
    listen_->setObjectName(QStringLiteral("mic"));
    listen_->setEnabled(false);
    listen_->setMinimumHeight(44);
    listen_->setToolTip(QStringLiteral(
        "Press once, then ask the question. It stops listening when you stop talking."));

    question_ = new QLineEdit(this);
    question_->setPlaceholderText(QStringLiteral("\u2026 or type the question here"));
    question_->setMinimumHeight(44);

    ask_ = new QPushButton(QStringLiteral("Ask"), this);
    ask_->setObjectName(QStringLiteral("primary"));
    ask_->setMinimumHeight(44);
    ask_->setMinimumWidth(92);

    level_ = new QProgressBar(this);
    level_->setObjectName(QStringLiteral("level"));
    level_->setTextVisible(false);
    level_->setRange(0, 100);
    level_->setFixedWidth(64);
    level_->setMinimumHeight(44);
    level_->setToolTip(QStringLiteral("How loudly the microphone is hearing you."));
    level_->hide();

    ask_row->addWidget(listen_);
    ask_row->addWidget(level_);
    ask_row->addWidget(question_, 1);
    ask_row->addWidget(ask_);
    root->addLayout(ask_row);

    status_ = new QLabel(QStringLiteral("Starting camera\u2026"), this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setWordWrap(true);
    root->addWidget(status_);

    /* The answers. Everything above and below is deliberately smaller: this is
     * the part the patient reads, and the only part they act on. */
    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildOptionsPage());
    pages_->addWidget(buildMorsePage());
    root->addWidget(pages_, 1);

    auto *divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFrameShape(QFrame::HLine);
    root->addWidget(divider);

    /* Row two is everything about running the application rather than using it:
     * which mode, calibration, and a settings panel that stays shut. */
    auto *control_row = new QHBoxLayout;
    control_row->setSpacing(10);

    mode_box_ = new ComboBox(this);
    mode_box_->setMinimumWidth(190);
    mode_box_->setMinimumHeight(38);
    mode_box_->addItem(QStringLiteral("Blink only"));
    mode_box_->addItem(QStringLiteral("Head + blink"));
    mode_box_->addItem(QStringLiteral("Morse spelling"));
    /* The explanation belongs in the status line, which changes with the mode
     * and is where someone is already looking, not crammed into a dropdown. */
    mode_box_->setItemData(0, QStringLiteral("Short blink moves the highlight, long blink speaks it."), Qt::ToolTipRole);
    mode_box_->setItemData(1, QStringLiteral("Point your nose at an answer, then blink."), Qt::ToolTipRole);
    mode_box_->setItemData(2, QStringLiteral("Spell any answer: short blink is a dot, long blink a dash."), Qt::ToolTipRole);

    /* Both setup screens live in Settings. They are run once per patient and
     * then never again, so a permanent button each was two-thirds of the
     * toolbar spent on something nobody presses twice. */
    calibrate_ = new QPushButton(QStringLiteral("Head setup\u2026"), this);
    calibrate_->setMinimumHeight(38);
    calibrate_->setToolTip(QStringLiteral(
        "Teach head mode where the corners of the screen are for this patient."));
    connect(calibrate_, &QPushButton::clicked, this, &MainWindow::onCalibrate);

    settings_toggle_ = new QPushButton(QStringLiteral("Settings\u2026"), this);
    settings_toggle_->setMinimumHeight(38);

    calibrate_blinks_ = new QPushButton(QStringLiteral("Blink setup\u2026"), this);
    calibrate_blinks_->setMinimumHeight(38);
    calibrate_blinks_->setToolTip(QStringLiteral(
        "Measure this patient's own short and long blinks, instead of guessing."));
    connect(calibrate_blinks_, &QPushButton::clicked, this, &MainWindow::onCalibrateBlinks);

    subsystems_ = new QLabel(this);
    subsystems_->setObjectName(QStringLiteral("subsys"));

    /* Full screen has no title bar and no close button, so it needs its own
     * way out. In Settings, with the rest of the caregiver's controls -- the
     * patient never presses it, and Escape and F11 still work without opening
     * anything. */
    exit_full_ = new QPushButton(QStringLiteral("Exit full screen"), this);
    exit_full_->setMinimumHeight(38);
    exit_full_->setToolTip(QStringLiteral("Also Escape, or F11."));
    connect(exit_full_, &QPushButton::clicked, this, [this] {
        showNormal();
        updateFullScreenButton();
        if (settings_dialog_) settings_dialog_->accept();
    });

    quit_ = new QPushButton(QStringLiteral("Quit OpennComm"), this);
    quit_->setObjectName(QStringLiteral("exitfs"));
    quit_->setMinimumHeight(38);
    /* Both of these are parented here but live in the Settings dialog's
     * footer, which is built on first use. Until then they are in no layout,
     * and a widget in no layout paints itself over the top-left corner. */
    quit_->hide();
    exit_full_->hide();
    connect(quit_, &QPushButton::clicked, this, [this] {
        if (settings_dialog_) settings_dialog_->accept();
        close();
    });

    /* The camera preview belongs in Settings. On the main screen it was a
     * moving picture beside the four answers, competing for the attention of
     * a patient who has only their eyes to answer with. */
    preview_ = new QLabel(this);
    preview_->setFixedSize(168, 126);
    preview_->setToolTip(QStringLiteral("What the tracker sees."));
    preview_->setStyleSheet(QStringLiteral(
        "border:1px solid #2a3a5e; border-radius:10px; background:#0f1626;"));

    control_row->addWidget(mode_box_);
    control_row->addWidget(settings_toggle_);
    control_row->addStretch(1);
    control_row->addWidget(subsystems_);
    root->addLayout(control_row);

    /* Tuning and diagnostics, in a dialog of their own. These matter
     * enormously when something is wrong and are pure clutter when it is not,
     * and inline they were pushing the four answers -- the only thing on this
     * screen the patient uses -- upward every time they were opened. */
    settings_panel_ = new QWidget(this);
    auto *panel = new QHBoxLayout(settings_panel_);
    panel->setContentsMargins(0, 6, 0, 0);
    panel->setSpacing(14);

    profile_button_ = new QPushButton(settings_panel_);
    profile_button_->setMinimumHeight(38);
    profile_button_->setToolTip(QStringLiteral(
        "Name, age and anything worth knowing. Kept on this computer."));
    connect(profile_button_, &QPushButton::clicked, this, &MainWindow::onEditProfile);

    history_button_ = new QPushButton(settings_panel_);
    history_button_->setMinimumHeight(38);
    history_button_->setToolTip(QStringLiteral(
        "Everything the patient has answered, kept on this computer."));
    connect(history_button_, &QPushButton::clicked, this, &MainWindow::onOpenHistory);

    speed_box_ = new ComboBox(settings_panel_);
    speed_box_->addItem(QStringLiteral("Fast \u2014 hold 0.42 s"));
    speed_box_->addItem(QStringLiteral("Normal \u2014 hold 0.56 s"));
    speed_box_->addItem(QStringLiteral("Relaxed \u2014 hold 0.90 s"));
    speed_box_->setCurrentIndex(OC_SPEED_NORMAL);

    gap_box_ = new ComboBox(settings_panel_);
    for (int ms : { 1000, 1500, 2000, 2500, 3000 })
        gap_box_->addItem(QStringLiteral("%1.%2 s").arg(ms / 1000).arg((ms % 1000) / 100));
    gap_box_->setCurrentIndex(1);

    /* Blink mode only. For a patient who can blink but for whom every blink is
     * expensive: the highlight comes to them, so choosing costs one long blink
     * instead of up to four short ones. Off by default -- a moving target is
     * harder to hit than a still one. */
    scan_box_ = new ComboBox(settings_panel_);
    scan_box_->addItem(QStringLiteral("Off \u2014 the patient cycles"));
    for (int s : { 2, 3, 4, 5, 7, 10 })
        scan_box_->addItem(QStringLiteral("Every %1 s").arg(s));

    /* Morse only. Stillness for this long speaks the message without anyone
     * pressing anything -- for when no caregiver is watching the screen. */
    send_box_ = new ComboBox(settings_panel_);
    send_box_->addItem(QStringLiteral("Off \u2014 press Speak this"));
    for (int s : { 3, 5, 8, 12 })
        send_box_->addItem(QStringLiteral("After %1 s of stillness").arg(s));

    voice_box_ = new ComboBox(settings_panel_);
    voice_box_->setToolTip(QStringLiteral(
        "The voice other people will hear as the patient's."));

    diagnostics_ = new QLabel(settings_panel_);
    diagnostics_->setObjectName(QStringLiteral("diag"));
    diagnostics_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    const auto caption = [this](const char *text) {
        auto *l = new QLabel(QLatin1String(text), settings_panel_);
        l->setObjectName(QStringLiteral("sectionHint"));
        return l;
    };

    auto *dials = new QVBoxLayout;
    dials->setSpacing(6);
    dials->addWidget(caption("Patient"));
    dials->addWidget(profile_button_);
    dials->addWidget(history_button_);
    dials->addSpacing(8);
    dials->addWidget(caption("Voice"));
    dials->addWidget(voice_box_);
    dials->addSpacing(8);
    dials->addWidget(caption("Blink speed"));
    dials->addWidget(speed_box_);
    dials->addSpacing(8);
    dials->addWidget(caption("Auto-scan (blink mode)"));
    dials->addWidget(scan_box_);
    dials->addSpacing(8);
    dials->addWidget(caption("Morse letter gap"));
    dials->addWidget(gap_box_);
    dials->addSpacing(8);
    dials->addWidget(caption("Morse speaks by itself"));
    dials->addWidget(send_box_);
    dials->addStretch(1);

    /* Run once per patient, so they sit beside the patient's other settings
     * rather than on the main screen. */
    auto *setup = new QVBoxLayout;
    setup->setSpacing(6);
    setup->addWidget(caption("Set up for this patient"));
    setup->addWidget(calibrate_);
    setup->addWidget(calibrate_blinks_);
    setup->addSpacing(10);
    setup->addWidget(caption("Camera"));
    setup->addWidget(preview_);
    setup->addWidget(diagnostics_, 1);
    setup->addStretch(1);

    panel->addLayout(dials, 0);
    panel->addLayout(setup, 1);
    settings_panel_->hide();

    connect(settings_toggle_, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    connect(ask_, &QPushButton::clicked, this, &MainWindow::onAskClicked);
    connect(question_, &QLineEdit::returnPressed, this, &MainWindow::onAskClicked);
    connect(listen_, &QPushButton::clicked, this, &MainWindow::onListenClicked);
    connect(mode_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onModeChanged);
    connect(speed_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onSpeedChanged);
    connect(gap_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onGapChanged);
    connect(voice_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onVoiceChanged);
    connect(scan_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onAutoScanChanged);
    connect(send_box_, &QComboBox::currentIndexChanged, this, &MainWindow::onAutoSendChanged);
}

QWidget *MainWindow::buildOptionsPage()
{
    auto *page = new QWidget(this);
    options_page_ = page;
    auto *grid = new QGridLayout(page);
    grid->setSpacing(16);
    grid->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 4; i++) {
        auto *b = new OptionButton(i, page);
        /* The mouse cannot speak an answer. Only a blink, a head movement or
         * morse can -- the patient's own actions and nothing else.
         *
         * A click used to commit, as a caregiver override. Removed on request,
         * and the reasoning holds up: an answer spoken in the patient's voice
         * should have been chosen by the patient. A caregiver who needs to
         * check the wording can now read the buttons without any risk of a
         * stray click putting words in their mouth, and a knock against a
         * touchscreen at a bedside cannot say anything at all.
         *
         * Transparent to the mouse rather than disabled: disabling would grey
         * them out, and these four are what the patient reads. */
        b->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        grid->addWidget(b, i / 2, i % 2);
        buttons_ << b;
    }

    /* On top of the grid rather than in it, so it can sit anywhere including
     * across the gap between two buttons -- which is exactly where it is most
     * worth seeing. */
    nose_ = new NosePointer(page);
    nose_->hide();

    return page;
}

QWidget *MainWindow::buildMorsePage()
{
    auto *page = new QWidget(this);
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(12);

    /* The sentence so far. Sized to what it holds rather than given a fixed
     * slab: an empty message used to take a third of the window while the
     * symbols being entered took twenty pixels of it. */
    morse_text_ = new QLabel(page);
    morse_text_->setObjectName(QStringLiteral("morseText"));
    morse_text_->setMinimumHeight(86);
    morse_text_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    morse_text_->setWordWrap(true);
    col->addWidget(morse_text_, 2);

    /* A message that speaks itself with no warning is the failure this whole
     * program is arranged to prevent. When autosend is on, the countdown is
     * visible for its whole duration, and any blink cancels it. */
    morse_countdown_ = new QLabel(page);
    morse_countdown_->setObjectName(QStringLiteral("morseCountdown"));
    morse_countdown_->setAlignment(Qt::AlignCenter);
    morse_countdown_->hide();
    col->addWidget(morse_countdown_, 0);

    /* What is being entered right now, drawn rather than typeset, and given
     * the most room. It is the only part of this screen that moves. */
    morse_entry_ = new MorseEntry(page);
    col->addWidget(morse_entry_, 3);

    /* The chart is always on screen. Nobody should have to memorise morse to
     * use this, patient or caregiver.
     *
     * Eight columns rather than ten: at ten the cells were too narrow for a
     * five symbol code and the dots and dashes collapsed into a smear. */
    auto *chart = new QGridLayout;
    chart->setSpacing(5);
    const char *chars = oc_morse_table_chars();
    for (int i = 0; chars[i]; i++) {
        auto *key = new MorseKey(QChar::fromLatin1(chars[i]),
                                 QString::fromLatin1(oc_morse_code_for(chars[i])), page);
        chart->addWidget(key, i / 8, i % 8);
        morse_keys_ << key;
    }
    for (int c = 0; c < 8; c++) chart->setColumnStretch(c, 1);
    col->addLayout(chart, 0);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);

    morse_hint_ = new QLabel(
        QStringLiteral("Short blink = dot   ·   Long blink = dash   ·   "
                       "pause to finish a letter, longer for a space"), page);
    morse_hint_->setObjectName(QStringLiteral("morseHint"));
    controls->addWidget(morse_hint_);
    controls->addStretch(1);

    /* Everything corrective is a caregiver button. The patient performs exactly
     * two gestures and nothing else; crowding more bands into eyelid timing is
     * where this mode stops being usable.
     *
     * On one row at the foot of the screen rather than across the middle,
     * where they were taking the centre from the patient's own spelling. */
    struct { const char *label; void (MainWindow::*slot)(); } buttons[] = {
        { "Undo",  &MainWindow::onMorseUndo },
        { "Space", &MainWindow::onMorseSpace },
        { "Clear", &MainWindow::onMorseClear },
    };
    for (auto &b : buttons) {
        auto *btn = new QPushButton(QString::fromLatin1(b.label), page);
        btn->setMinimumHeight(40);
        btn->setMinimumWidth(86);
        connect(btn, &QPushButton::clicked, this, b.slot);
        controls->addWidget(btn);
    }
    auto *speak = new QPushButton(QStringLiteral("Speak this"), page);
    speak->setObjectName(QStringLiteral("primary"));
    speak->setMinimumHeight(40);
    speak->setMinimumWidth(130);
    connect(speak, &QPushButton::clicked, this, &MainWindow::speakMorseMessage);
    controls->addWidget(speak);
    col->addLayout(controls, 0);

    return page;
}

void MainWindow::setStatus(const QString &text) { status_->setText(text); }

/* The subsystems each take a different amount of time to come up and any of
 * them can fail quietly -- a missing model, no microphone, a camera another
 * application is holding. Without this strip the only symptom of a dead
 * language model is that the answers are a bit plainer than expected, which is
 * indistinguishable from the model simply being terse. Say it plainly instead. */
void MainWindow::updateSubsystems()
{
    struct Part { const char *name; const char *state; const char *colour; };

    const QString voice = speech_->engineName();
    const Part speech = speech_->available()
        ? Part{ "Voice", voice.startsWith(QStringLiteral("Piper")) ? "Piper" : "espeak-ng", "#49e08b" }
        : Part{ "Voice", "unavailable", "#e06a49" };

    const Part camera = camera_ok_
        ? Part{ "Camera", "ready", "#49e08b" }
        : Part{ "Camera", "starting", "#e0b849" };

    const Part mic = listener_failed_
        ? Part{ "Microphone", "unavailable", "#e06a49" }
        : (listener_ready_ ? Part{ "Microphone", "ready", "#49e08b" }
                           : Part{ "Microphone", "loading", "#e0b849" });

    const Part model = model_failed_
        ? Part{ "Answers", "built-in only", "#e06a49" }
        : (generating_ ? Part{ "Answers", "writing\u2026", "#6ea8ff" }
                       : (model_ready_ ? Part{ "Answers", "ready", "#49e08b" }
                                       : Part{ "Answers", "loading model\u2026", "#e0b849" }));

    const Part parts[] = { camera, mic, speech, model };
    QString html;
    for (const Part &p : parts) {
        if (!html.isEmpty()) html += QStringLiteral("&nbsp;&nbsp;&nbsp;");
        html += QStringLiteral("<span style='color:%1'>\u25CF</span> "
                               "<span style='color:#9fb4d8'>%2 %3</span>")
                    .arg(QString::fromUtf8(p.colour), QString::fromUtf8(p.name),
                         QString::fromUtf8(p.state));
    }
    subsystems_->setText(html);
}

QString MainWindow::patientName() const
{
    return profile_.name.trimmed();
}

void MainWindow::setOptions(const oc_options &options)
{
    for (int i = 0; i < buttons_.size() && i < OC_OPTION_COUNT; i++)
        buttons_[i]->setText(QString::fromUtf8(options.text[i]));

    options_loaded_ = true;
    awaiting_answers_ = false;
    committed_index_ = -1;
    /* Re-enabled here rather than only where they were disabled: every route
     * that produces four answers passes through this function, and a button
     * left disabled is a patient who cannot answer. */
    for (auto *b : buttons_) {
        b->setCommitted(false);
        b->setHoldProgress(-1.0);
        b->setEnabled(true);
    }

    oc_selector_set_options(&blink_, true);
    oc_head_set_options(&head_, true);
    /* Deferred to the next event-loop turn so the layout has placed the
     * buttons before their text is measured against them. */
    QTimer::singleShot(0, this, &MainWindow::balanceOptionText);
    highlight(0);
}

/* One size for all four answers. Letting each button pick its own best fit
 * makes a one-word answer tower over a seven-word one, which reads as emphasis
 * nobody intended. */
void MainWindow::balanceOptionText()
{
    if (buttons_.isEmpty()) return;
    /* Measured against the buttons' real geometry, which does not exist until
     * the layout has run. Called too early -- from the constructor, or from
     * resizeEvent before children are placed -- every button reports the
     * minimum and all four text collapses to 12pt. */
    if (buttons_.first()->height() < 40) return;
    double smallest = 1e9;
    for (auto *b : buttons_)
        if (!b->text().isEmpty()) smallest = qMin(smallest, b->fittingPointSize());
    if (smallest > 1e8) return;
    for (auto *b : buttons_) b->setUniformPointSize(smallest);
}

void MainWindow::highlight(int index)
{
    for (int i = 0; i < buttons_.size(); i++) buttons_[i]->setHighlighted(i == index);
}

/* The hold indicator belongs only to the highlighted option: it is the promise
 * that continuing to hold will speak THIS answer. Showing it anywhere else
 * would say the opposite of what it means. */
void MainWindow::setHold(int index, double progress)
{
    for (int i = 0; i < buttons_.size(); i++)
        buttons_[i]->setHoldProgress(i == index ? progress : -1.0);
}

void MainWindow::commit(int index)
{
    if (input_locked_ || !options_loaded_) return;
    if (index < 0 || index >= buttons_.size()) return;
    const QString text = buttons_[index]->text();
    if (text.isEmpty()) return;

    /* inputLocked spans the WHOLE spoken answer. While it is set the patient is
     * listening, not answering, and every closure is ignored. It is released
     * only by Speech::finished, which is guaranteed to arrive. */
    input_locked_ = true;
    blink_.input_locked = true;
    head_.input_locked = true;
    committed_index_ = index;

    history_ << QStringLiteral("Q: %1  A: %2").arg(question_->text().trimmed(), text);
    while (history_.size() > 10) history_.removeFirst();

    buttons_[index]->setCommitted(true);
    setHold(index, -1.0);
    highlight(index);
    setStatus(QStringLiteral("Speaking: “%1”").arg(text));

    speech_->speak(text);

    /* After speak(), never before. A slow or full disk must not sit between
     * the patient choosing an answer and hearing it said. */
    Exchange e;
    e.asked_at = QDateTime::currentMSecsSinceEpoch();
    e.question = question_->text().trimmed();
    e.answer   = text;
    e.mode     = mode_ == ModeHead ? QStringLiteral("head") : QStringLiteral("blink");
    e.source   = options_source_;
    history_db_.record(e);
}

void MainWindow::onSpeechFinished()
{
    input_locked_ = false;
    morse_.input_locked = false;

    if (speaking_morse_) {
        speaking_morse_ = false;
        /* The message has been delivered; clear it so the next answer starts
         * from an empty line rather than being appended to the last one. */
        oc_morse_clear(&morse_);
        renderMorse(0);
        setStatus(QStringLiteral("Spoken — spell the next answer."));
        return;
    }

    /* Both selectors discard any closure in progress as they unlock, so
     * releasing mid-blink is not read as a fresh gesture. */
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    if (committed_index_ >= 0 && committed_index_ < buttons_.size())
        buttons_[committed_index_]->setCommitted(false);
    setHold(-1, -1.0);
    committed_index_ = -1;
    setStatus(QStringLiteral("Ready — ask another question."));
}

void MainWindow::onAskClicked()
{
    const QString q = question_->text().trimmed();
    const QByteArray q_utf8 = q.toUtf8();

    const oc_intent intent = oc_detect_intent(q_utf8.constData());
    intent_ = intent;

    if (q.isEmpty()) {
        showPhrasebook();
        setStatus(QStringLiteral("Ready \u2014 four general answers offered."));
        return;
    }

    /* The phrasebook no longer pre-empts the model.
     *
     * It used to fill the buttons instantly and be replaced a few seconds
     * later when the model answered. That was defensible -- the patient could
     * start choosing at once -- but in practice it meant the four answers
     * changed under them mid-decision, which is worse than waiting: a patient
     * part-way through selecting "I am in pain" would find something else in
     * that position. Now the wait is honest and visible, and the phrasebook is
     * what it always should have been: the fallback.
     *
     * Everything that made the swap safe still applies -- the timeout below,
     * the model-failed path, and the no-model path all end at the phrasebook,
     * so the patient is never left without four answers. */
    if (!model_ready_) {
        showPhrasebook();
        setStatus(QStringLiteral("Asked: \u201c%1\u201d  (%2) \u2014 no model, offering "
                                 "general answers.").arg(q, QString::fromUtf8(oc_intent_name(intent))));
        return;
    }

    beginPendingAnswers();
    setStatus(QStringLiteral("Asked: \u201c%1\u201d \u2014 writing answers\u2026").arg(q));

    generating_ = true;
    updateSubsystems();
    const quint64 id = ++request_id_;
    QMetaObject::invokeMethod(generator_, "generate", Qt::QueuedConnection,
                              Q_ARG(quint64, id), Q_ARG(QString, q),
                              Q_ARG(QString, patientName()),
                              Q_ARG(QString, profile_.brief()),
                              Q_ARG(QStringList, history_));
}

/* The four curated answers for the current intent. Every path that gives up on
 * the model ends here. */
void MainWindow::showPhrasebook()
{
    awaiting_answers_ = false;
    if (answer_timeout_) answer_timeout_->stop();

    const QByteArray name_utf8 = patientName().toUtf8();
    oc_options options;
    oc_phrasebook(intent_, name_utf8.isEmpty() ? nullptr : name_utf8.constData(), &options);
    options_source_ = QStringLiteral("phrasebook");
    setOptions(options);
}

/* Hold the four buttons while the model writes.
 *
 * Nothing is selectable during this: showing the previous question's answers
 * as though they were live would let the patient say something that answers a
 * question nobody asked. The timeout is the safety net -- generation takes
 * about five seconds and anything past twelve is a failure, whatever the
 * model thinks it is doing. */
void MainWindow::beginPendingAnswers()
{
    awaiting_answers_ = true;
    committed_index_ = -1;
    setHold(-1, -1.0);
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);

    for (int i = 0; i < buttons_.size(); i++) {
        buttons_[i]->setText(QStringLiteral("\u2026"));
        buttons_[i]->setCommitted(false);
        buttons_[i]->setHighlighted(false);
        buttons_[i]->setEnabled(false);
    }
    balanceOptionText();

    if (!answer_timeout_) {
        answer_timeout_ = new QTimer(this);
        answer_timeout_->setSingleShot(true);
        answer_timeout_->setInterval(12000);
        connect(answer_timeout_, &QTimer::timeout, this, &MainWindow::onAnswersTimedOut);
    }
    answer_timeout_->start();
}

void MainWindow::onAnswersTimedOut()
{
    if (!awaiting_answers_) return;
    generating_ = false;
    updateSubsystems();
    showPhrasebook();
    setStatus(QStringLiteral(
        "The model is taking too long \u2014 offering general answers instead."));
}

void MainWindow::onModeChanged(int index)
{
    mode_ = (index == ModeHead) ? ModeHead : (index == ModeMorse ? ModeMorse : ModeBlink);
    speech_->stop();
    input_locked_ = false;
    speaking_morse_ = false;
    oc_selector_unlock(&blink_);
    oc_head_unlock(&head_);
    morse_.input_locked = false;
    oc_morse_clear(&morse_);
    highlight(0);

    pages_->setCurrentIndex(mode_ == ModeMorse ? 1 : 0);
    for (auto *b : buttons_) b->setShowNumber(mode_ == ModeHead);
    if (nose_) {
        nose_->setVisible(mode_ == ModeHead);
        if (mode_ == ModeHead && options_page_) {
            nose_->setGeometry(options_page_->rect());
            nose_->raise();
        }
    }
    /* Morse needs no question at all -- it is the one mode where the patient
     * composes rather than chooses, and it stays available when nothing else
     * does. Asking is meaningless here. */
    ask_->setEnabled(mode_ != ModeMorse);
    listen_->setEnabled(listener_ready_ && mode_ != ModeMorse);

    switch (mode_) {
    case ModeHead:
        setStatus(QStringLiteral("Head mode — point your nose at an option, then blink."));
        break;
    case ModeMorse:
        renderMorse(0);
        setStatus(QStringLiteral("Morse mode — short blink is a dot, long blink is a dash."));
        break;
    case ModeBlink:
    default:
        setStatus(QStringLiteral("Blink mode — short blink cycles, long blink selects."));
        break;
    }
}

void MainWindow::onGeneratorLoaded(const QString &model_name)
{
    model_ready_ = true;
    model_failed_ = false;
    diagnostics_->setToolTip(model_name);
    updateSubsystems();
    setStatus(QStringLiteral("Ready — answers will be written for each question."));
}

void MainWindow::onGeneratorFailed(const QString &message)
{
    model_ready_ = false;
    model_failed_ = true;
    generating_ = false;
    updateSubsystems();
    /* Not an error the caregiver has to act on -- but if a question was in
     * flight the patient is looking at four empty buttons, so fill them. */
    if (awaiting_answers_) showPhrasebook();
    setStatus(message);
}

void MainWindow::onAnswersProduced(quint64 request_id, const QStringList &lines, double ms)
{
    generating_ = false;
    updateSubsystems();
    if (request_id != request_id_) return;   /* a newer question has been asked */
    if (input_locked_) return;               /* never swap the options mid-answer */

    /* The timeout may have fired first and already put the phrasebook up. Let
     * the model's answers replace it: they are better, and nothing has been
     * selected yet or input_locked_ would be set. */
    awaiting_answers_ = false;
    if (answer_timeout_) answer_timeout_->stop();

    std::vector<QByteArray> storage;
    std::vector<const char *> raw;
    storage.reserve(lines.size());
    for (const QString &l : lines) { storage.push_back(l.toUtf8()); }
    for (const QByteArray &b : storage) raw.push_back(b.constData());

    const QByteArray name_utf8 = patientName().toUtf8();
    oc_options options;
    const int from_model = oc_options_from_model(
        raw.empty() ? nullptr : raw.data(), static_cast<int>(raw.size()),
        intent_, name_utf8.isEmpty() ? nullptr : name_utf8.constData(), &options);

    options_source_ = from_model > 0 ? QStringLiteral("model") : QStringLiteral("phrasebook");
    setOptions(options);
    setStatus(QStringLiteral("%1 of 4 answers written for this question (%2 ms).")
                  .arg(from_model).arg(ms, 0, 'f', 0));
}

void MainWindow::renderMorse(char preview, int autosend_remaining_ms)
{
    if (autosend_remaining_ms >= 0) {
        morse_countdown_->setText(
            QStringLiteral("Speaking in %1\u2026  \u2014  blink to keep writing")
                .arg((autosend_remaining_ms + 999) / 1000));
        morse_countdown_->show();
    } else {
        morse_countdown_->hide();
    }

    const QString text = QString::fromUtf8(morse_.text);
    morse_text_->setText(text.isEmpty()
                             ? QStringLiteral("<span style='color:#5c6b88'>spell an answer…</span>")
                             : text);

    /* The symbols committed so far plus the closure in progress, which is what
     * the candidate list and the chart are narrowed against. */
    char probe[OC_MORSE_MAX_SYMBOLS + 2] = { 0 };
    std::snprintf(probe, sizeof probe, "%s", morse_.buffer);
    if (preview) {
        const size_t n = std::strlen(probe);
        if (n + 1 < sizeof probe) { probe[n] = preview; probe[n + 1] = '\0'; }
    }

    char reachable[64];
    oc_morse_reachable(probe, reachable, sizeof reachable);
    const char exact = oc_morse_decode(probe);

    /* Never resolved to a nearest guess: spelling the wrong word on a
     * patient's behalf is worse than making them repeat a letter. An empty
     * candidate list is what MorseEntry draws as "not a letter". */
    QString candidates;
    for (int i = 0; reachable[i]; i++) {
        if (i) candidates += QLatin1Char(' ');
        candidates += QChar::fromLatin1(reachable[i]);
    }

    morse_entry_->setEntry(QString::fromLatin1(probe), preview != 0,
                           exact ? QChar::fromLatin1(exact) : QChar(),
                           candidates);

    /* With nothing entered every letter is reachable, but showing all forty
     * lit up says "all of these are chosen" rather than "nothing yet". Resting
     * state has to look like rest. */
    const char *chars = oc_morse_table_chars();
    for (int i = 0; i < morse_keys_.size(); i++) {
        MorseKey::State state = MorseKey::Idle;
        if (probe[0]) {
            if (chars[i] == exact)                          state = MorseKey::Exact;
            else if (strchr(reachable, chars[i]) == nullptr) state = MorseKey::Dim;
        }
        morse_keys_[i]->setState(state);
    }
}

void MainWindow::onSpeedChanged(int index)
{
    const auto speed = static_cast<oc_speed>(qBound(0, index, OC_SPEED_COUNT - 1));
    const oc_bands bands = oc_blink_bands_for(speed);
    blink_.tracker.bands = bands;
    morse_.tracker.bands = bands;
    oc_blink_tracker_reset(&blink_.tracker);
    oc_blink_tracker_reset(&morse_.tracker);
    QSettings().setValue(QStringLiteral("blink_speed"), index);
    setStatus(QStringLiteral("Blink speed: hold %1 ms to select, %2 ms is ignored as a short blink.")
                  .arg(bands.long_ms).arg(bands.short_max_ms));
}

namespace {
/* Index 0 is "off" in both of these; the rest are the values offered. */
int autoScanSecondsFor(int index)
{
    static const int kChoices[] = { 0, 2, 3, 4, 5, 7, 10 };
    const int n = int(sizeof kChoices / sizeof *kChoices);
    return kChoices[index < 0 ? 0 : (index >= n ? n - 1 : index)];
}

int autoSendSecondsFor(int index)
{
    static const int kChoices[] = { 0, 3, 5, 8, 12 };
    const int n = int(sizeof kChoices / sizeof *kChoices);
    return kChoices[index < 0 ? 0 : (index >= n ? n - 1 : index)];
}
} // namespace

void MainWindow::onAutoScanChanged(int index)
{
    const int seconds = autoScanSecondsFor(index);
    oc_selector_set_auto_scan(&blink_, seconds);
    QSettings().setValue(QStringLiteral("auto_scan_index"), index);
    setStatus(seconds == 0
        ? QStringLiteral("Auto-scan off \u2014 a short blink moves the highlight.")
        : QStringLiteral("Auto-scan on \u2014 the highlight moves every %1 seconds by itself.")
              .arg(seconds));
}

void MainWindow::onAutoSendChanged(int index)
{
    const int seconds = autoSendSecondsFor(index);
    morse_.autosend_seconds = seconds;
    oc_morse_cancel_autosend(&morse_);
    QSettings().setValue(QStringLiteral("auto_send_index"), index);
    setStatus(seconds == 0
        ? QStringLiteral("Morse will be spoken when someone presses Speak this.")
        : QStringLiteral("Morse will be spoken after %1 seconds of stillness.").arg(seconds));
}

void MainWindow::onGapChanged(int index)
{
    static const int kGaps[] = { 1000, 1500, 2000, 2500, 3000 };
    const int gap = kGaps[qBound(0, index, 4)];
    morse_.letter_gap_ms = gap;
    QSettings().setValue(QStringLiteral("morse_gap_index"), index);
    setStatus(QStringLiteral("Morse: pause %1 ms to finish a letter, %2 ms for a space.")
                  .arg(gap).arg(oc_morse_word_gap_ms(&morse_)));
}

void MainWindow::onMorseUndo()  { oc_morse_undo(&morse_);  renderMorse(0); }
void MainWindow::onMorseSpace() { oc_morse_space(&morse_); renderMorse(0); }
void MainWindow::onMorseClear() { oc_morse_clear(&morse_); renderMorse(0); }

void MainWindow::speakMorseMessage()
{
    const QString message = QString::fromUtf8(morse_.text).trimmed();
    if (message.isEmpty() || input_locked_) return;

    /* Mirrors commit(): the patient is listening, not spelling, for the whole
     * utterance, and the lock is released only by Speech::finished. */
    input_locked_ = true;
    morse_.input_locked = true;
    speaking_morse_ = true;
    setStatus(QStringLiteral("Speaking: \u201c%1\u201d").arg(message));
    speech_->speak(message);

    /* Spelled letter by letter, so there is usually no question behind it --
     * and it is the most laborious thing the patient can do, which makes it
     * the thing most worth keeping. */
    Exchange e;
    e.asked_at = QDateTime::currentMSecsSinceEpoch();
    e.answer   = message;
    e.mode     = QStringLiteral("morse");
    e.source   = QStringLiteral("spelled");
    history_db_.record(e);
}

void MainWindow::onListenerReady()
{
    listener_ready_ = true;
    listener_failed_ = false;
    listen_->setEnabled(true);
    updateSubsystems();
}

void MainWindow::onListenerFailed(const QString &message)
{
    /* Recording may have been in progress; the button must come back either
     * way, or the caregiver is left with no way to ask anything. */
    listening_ = false;
    listen_->setText(QStringLiteral("\u25CF  Ask aloud"));
    listen_->setEnabled(listener_ready_);
    if (level_) level_->hide();
    if (!listener_ready_) listener_failed_ = true;
    updateSubsystems();
    setStatus(message);
}

/* One press starts listening; the question ends it. Pressing again while it is
 * listening stops immediately, for the caregiver who has said their piece and
 * does not want to wait out the silence. */
void MainWindow::onListenClicked()
{
    if (!listener_ready_) return;

    if (listening_) {
        listening_ = false;
        listen_->setEnabled(false);
        setStatus(QStringLiteral("Transcribing\u2026"));
        QMetaObject::invokeMethod(listener_, "stopAndTranscribe", Qt::QueuedConnection);
        return;
    }

    /* Never record the machine speaking back to the patient. */
    speech_->stop();
    listening_ = true;
    listen_->setText(QStringLiteral("\u25A0  Stop"));
    if (level_) { level_->show(); level_->setValue(0); }
    setStatus(QStringLiteral("Listening \u2014 ask the question."));
    QMetaObject::invokeMethod(listener_, "startRecording", Qt::QueuedConnection);
}

/* Capture has stopped, by silence or by the cap. Transcription is still
 * running, so the button stays out of action until it lands. */
void MainWindow::onListeningEnded()
{
    listening_ = false;
    listen_->setText(QStringLiteral("\u25CF  Ask aloud"));
    listen_->setEnabled(false);
    if (level_) level_->hide();
    setStatus(QStringLiteral("Transcribing\u2026"));
}

void MainWindow::onTranscribed(const QString &text, double ms)
{
    listening_ = false;
    listen_->setText(QStringLiteral("\u25CF  Ask aloud"));
    listen_->setEnabled(true);
    question_->setText(text);
    setStatus(QStringLiteral("Heard: \u201c%1\u201d (%2 ms)").arg(text).arg(ms, 0, 'f', 0));
    /* Ask immediately. Making the caregiver press a second button after
     * speaking would be pure ceremony. */
    onAskClicked();
}

void MainWindow::onCameraFailed(const QString &message)
{
    setStatus(message);
}

void MainWindow::onLowFrameRate(double fps)
{
    setStatus(QStringLiteral("Camera is only managing %1 fps — blink timing will be coarse. "
                             "More light usually fixes this.").arg(fps, 0, 'f', 1));
}

void MainWindow::onFrame(const FaceFrame &frame)
{
    if (!camera_ok_) { camera_ok_ = true; updateSubsystems(); }

    if (!frame.preview.isNull())
        preview_->setPixmap(QPixmap::fromImage(frame.preview)
                                .scaled(preview_->size(), Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));

    diagnostics_->setText(
        QStringLiteral("face %1   fps %2   inference %3 ms\nEAR %4  (avg %5, threshold %6)   eyes %7\n"
                       "nose %8, %9")
            .arg(frame.face_detected ? QStringLiteral("yes") : QStringLiteral("NO"))
            .arg(frame.fps, 0, 'f', 1)
            .arg(frame.inference_ms, 0, 'f', 0)
            .arg(frame.ear, 0, 'f', 3)
            .arg(frame.ear_average, 0, 'f', 3)
            .arg(frame.ear_average * OC_EAR_THRESHOLD, 0, 'f', 3)
            .arg(frame.blink ? QStringLiteral("SHUT") : QStringLiteral("open"))
            .arg(frame.nose_x, 0, 'f', 3)
            .arg(frame.nose_y, 0, 'f', 3));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    /* While a setup screen is up, nothing the patient does may count as an
     * answer. The overlay gets every frame; the selectors get none. */
    if (calib_overlay_ && calib_overlay_->isVisible()) {
        calib_overlay_->onFrame(frame.nose_x, frame.nose_y, frame.face_detected, now);
        return;
    }
    if (blink_overlay_ && blink_overlay_->isVisible()) {
        blink_overlay_->onFrame(frame.blink, frame.face_detected, now);
        return;
    }

    if (!frame.face_detected) {
        if (nose_) nose_->setPosition(0.5, 0.5, false);
        return;
    }

    /* The answers are being written. Blinks land nowhere on purpose -- there
     * is nothing on the buttons yet to select. */
    if (awaiting_answers_ && mode_ != ModeMorse) return;

    if (mode_ == ModeMorse) {
        const oc_morse_event me = oc_morse_update(&morse_, frame.blink, now);
        renderMorse(me.preview, me.autosend_remaining_ms);
        switch (me.kind) {
        case OC_MORSE_LETTER:
            setStatus(QStringLiteral("Letter: %1").arg(QChar::fromLatin1(me.letter)));
            break;
        case OC_MORSE_UNDECODABLE:
            setStatus(QStringLiteral("Not a letter — try that one again."));
            break;
        case OC_MORSE_SPACE:
            setStatus(QStringLiteral("Word break."));
            break;
        case OC_MORSE_AUTOSEND:
            speakMorseMessage();
            break;
        default:
            break;
        }
        return;
    }

    oc_sel_event ev;
    if (mode_ == ModeHead) {
        ev = oc_head_update(&head_, frame.nose_x, frame.nose_y, frame.blink, now);
        /* Drawn from the smoothed position the selector itself uses, not the
         * raw landmark: a pointer that jitters while the highlight sits still
         * would look like the tracking is failing when it is working. */
        const oc_point p = oc_gaze_position(&head_.calib, head_.mirror_x,
                                            head_.smooth_x, head_.smooth_y);
        nose_->setPosition(p.x, p.y, true);
    }
    else
        ev = oc_selector_update(&blink_, frame.blink, now);

    /* The tracker reports how far through a hold the patient is; until now
     * nothing drew it, so the one piece of feedback telling them "keep holding
     * and this will be chosen" was being computed and thrown away. */
    setHold(ev.index, input_locked_ ? -1.0 : ev.hold_progress);

    switch (ev.kind) {
    case OC_SEL_ADVANCED:
        highlight(ev.index);
        break;
    case OC_SEL_COMMITTED:
        /* The selector has already latched its own lock; commit() sets ours. */
        commit(ev.index);
        break;
    case OC_SEL_NEEDS_QUESTION:
        if (now - last_prompt_ms_ > kPromptCooldownMs) {
            last_prompt_ms_ = now;
            setStatus(QStringLiteral("Nothing to answer yet — ask a question first."));
        }
        break;
    case OC_SEL_NOTHING:
    default:
        if (mode_ == ModeHead) highlight(head_.quadrant);
        break;
    }
}
