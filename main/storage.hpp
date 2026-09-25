#pragma once
#include "auditor.hpp"
#include "esp_err.h"
esp_err_t load_settings(auditor::Settings &s, auditor::Calibration &c);
esp_err_t save_settings(const auditor::Settings &s, const auditor::Calibration &c);
