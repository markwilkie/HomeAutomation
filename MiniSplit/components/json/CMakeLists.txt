# Stub 'json' component for ESP-IDF 6.x compatibility.
# esp_matter 1.4.x was written against IDF 5.x which had a built-in 'json'
# component (a thin wrapper around cJSON). In IDF 6.x that component was
# removed; the managed component espressif__cjson provides the same sources.
# This stub re-exports espressif__cjson so any component that REQUIRES 'json'
# continues to compile without modification.

idf_component_register(
    REQUIRES "espressif__cjson"
)
