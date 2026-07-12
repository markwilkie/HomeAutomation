#pragma once

// Overrides for connectedhomeip's compiled-in example-app defaults (see
// CHIPDeviceConfig.h). VendorName/ProductName on the Basic Information
// cluster are NOT served from esp_matter's attribute store -- connectedhomeip's
// BasicInformationCluster.cpp reads them directly from these macros via
// DeviceInstanceInfoProvider::GetVendorName()/GetProductName(), so calling
// attribute::update() on those attribute IDs at runtime is a no-op. This is
// the only mechanism that actually changes what a commissioner sees.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Wilkie Home"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Mini-Split AC Bridge"
