#pragma once

// Overrides for connectedhomeip's compiled-in example-app defaults.
// VendorName/ProductName on the Basic Information cluster are NOT served
// from esp_matter's attribute store -- connectedhomeip's
// BasicInformationCluster.cpp reads them directly from these macros via
// DeviceInstanceInfoProvider::GetVendorName()/GetProductName(), so calling
// attribute::update() on those attribute IDs at runtime is a no-op. See
// ../../MiniSplit/include/chip_project_config.h -- same fix, confirmed on
// real hardware there.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Wilkie Home"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "MiniSplit IR Thermostat"
