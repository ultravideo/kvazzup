#include "automaticsettings.h"
#include "ui_automaticsettings.h"

#include "settingskeys.h"
#include "logger.h"

enum TabType {
  MAIN_TAB = 0, ROI_TAB = 1
};

AutomaticSettings::AutomaticSettings(QWidget *parent):
  QDialog(parent),
  ui_(new Ui::AutomaticSettings),
  settings_(settingsFile, settingsFileFormat),
  previousBitrate_(0),
  lastTabIndex_(0)
{
  ui_->setupUi(this);
  QObject::connect(ui_->close_button, &QPushButton::clicked,
                   this,             &AutomaticSettings::finished);

  QObject::connect(ui_->reset_button, &QPushButton::clicked,
                   this,             &AutomaticSettings::reset);

  QObject::connect(ui_->tabs, &QTabWidget::currentChanged,
                   this,      &AutomaticSettings::tabChanged);

  // the signal in Qt is overloaded (because of deprication) so we need different syntax
  QObject::connect(ui_->roi_qp, QOverload<int>::of(&QSpinBox::valueChanged),
                   this,         &AutomaticSettings::updateConfigAndReset);

  QObject::connect(ui_->background_qp, QOverload<int>::of(&QSpinBox::valueChanged),
                   this,         &AutomaticSettings::updateConfigAndReset);

  QObject::connect(ui_->brush_size, QOverload<int>::of(&QSpinBox::valueChanged),
                   this,         &AutomaticSettings::updateConfig);

  QObject::connect(ui_->show_grid, &QCheckBox::stateChanged,
                   this,         &AutomaticSettings::updateConfig);

  QObject::connect(ui_->ctu_based, &QCheckBox::stateChanged,
                   this,             &AutomaticSettings::updateConfigAndReset);

  settings_.setValue(SettingsKey::manualROIStatus,          "0");

  ui_->roi_surface->enableOverlay(ui_->roi_qp->value(),
                                  ui_->background_qp->value(),
                                  ui_->brush_size->value(),
                                  ui_->show_grid->isChecked(),
                                  !ui_->ctu_based->isChecked());
}


AutomaticSettings::~AutomaticSettings()
{
  delete ui_;
}


void AutomaticSettings::updateConfigAndReset(int i)
{
  ui_->roi_surface->enableOverlay(ui_->roi_qp->value(),
                                  ui_->background_qp->value(),
                                  ui_->brush_size->value(),
                                  ui_->show_grid->isChecked(),
                                  !ui_->ctu_based->isChecked());

  // reset the whole ROI map because changing config benefits from it
  ui_->roi_surface->resetOverlay();
}


void AutomaticSettings::updateConfig(int i)
{
  ui_->roi_surface->enableOverlay(ui_->roi_qp->value(),
                                  ui_->background_qp->value(),
                                  ui_->brush_size->value(),
                                  ui_->show_grid->isChecked(),
                                  !ui_->ctu_based->isChecked());
}


void AutomaticSettings::show()
{
  if (ui_->tabs->currentIndex() == ROI_TAB)
  {
    activateROI();
  }

  QWidget::show();
}


void AutomaticSettings::finished()
{
  disableROI();
  hide();
  emit hidden();
}


void AutomaticSettings::reset()
{
  if (ui_->tabs->currentIndex() == ROI_TAB)
  {
    ui_->roi_surface->resetOverlay();
  }
}



void AutomaticSettings::tabChanged(int index)
{
  // disable the last tab
  if (lastTabIndex_ == ROI_TAB)
  {
    disableROI();
  }

  // enable the new tab
  if (index == ROI_TAB)
  {
    activateROI();
  }

  lastTabIndex_ = index;
}


void AutomaticSettings::activateROI()
{
  Logger::getLogger()->printNormal(this, "Manual ROI window opened. "
                                         "Enabling manual ROI");

  previousBitrate_ = settings_.value(SettingsKey::videoBitrate).toInt();
  if (previousBitrate_ != 0)
  {
    // bitrate must be disabled for ROI
    settings_.setValue(SettingsKey::videoBitrate,            "0");

    emit updateVideoSettings();
  }

  settings_.setValue(SettingsKey::manualROIStatus,         "1");
  emit updateAutomaticSettings();
}


void AutomaticSettings::disableROI()
{
  Logger::getLogger()->printNormal(this, "Manual ROI window closed. "
                                         "Disabling manual ROI");
  settings_.setValue(SettingsKey::manualROIStatus,          "0");
  emit updateAutomaticSettings();

  if (previousBitrate_ != 0) // only set bitrate if we had to disable it
  {
    // return bitrate to previous value
    settings_.setValue(SettingsKey::videoBitrate, previousBitrate_);

    emit updateVideoSettings();
  }
}


VideoWidget* AutomaticSettings::getRoiSelfView()
{
  return ui_->roi_surface;
}
