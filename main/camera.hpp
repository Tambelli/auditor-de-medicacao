#pragma once
#include "auditor.hpp"
#include "esp_err.h"

esp_err_t camera_start(int profile);
void camera_stop();
bool camera_gray(uint8_t *out); // out: auditor::W * auditor::H bytes
