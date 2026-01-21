#include "ConfigManager.hpp"
#include "ArduinoJson.h"
#include "esp_log.h"
#include "rom/crc.h"
#include "TankManager.hpp"
#include <cstdint>

static const char* TAG = "ConfigManager";

const Recipe Recipe::EMPTY = { 0U, "no recipe", std::vector<RecipeIngredient>(), 0, 0, 0.0, 0, false };

ConfigManager::ConfigManager(const char* nvs_namespace) : _namespace(nvs_namespace), _nvs_handle(0) {}



bool ConfigManager::begin()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing and re-initializing.");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS Flash Initialized.");
    return err == ESP_OK;
}

bool ConfigManager::_openNVS()
{
    esp_err_t err = nvs_open(_namespace, NVS_READWRITE, &_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ConfigManager::_closeNVS()
{
    nvs_close(_nvs_handle);
}

bool ConfigManager::saveWiFiCredentials(const std::string& ssid, const std::string& password)
{
    if (!_openNVS())
        return false;
    nvs_set_str(_nvs_handle, "wifi_ssid", ssid.c_str());
    nvs_set_str(_nvs_handle, "wifi_pass", password.c_str());
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid.c_str());
    return err == ESP_OK;
}

bool ConfigManager::loadWiFiCredentials(std::string& ssid, std::string& password)
{
    if (!_openNVS())
        return false;
    bool success = false;
    size_t required_size;
    if (nvs_get_str(_nvs_handle, "wifi_ssid", NULL, &required_size) == ESP_OK) {
        char* ssid_buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "wifi_ssid", ssid_buf, &required_size) == ESP_OK) {
            ssid = std::string(ssid_buf);
            if (nvs_get_str(_nvs_handle, "wifi_pass", NULL, &required_size) == ESP_OK) {
                char* pass_buf = new char[required_size];
                if (nvs_get_str(_nvs_handle, "wifi_pass", pass_buf, &required_size) == ESP_OK) {
                    password = std::string(pass_buf);
                    success  = true;
                }
                delete[] pass_buf;
            }
        }
        delete[] ssid_buf;
    }
    _closeNVS();
    return success;
}


bool ConfigManager::saveTimezone(const std::string& tz)
{
    if (!_openNVS())
        return false;
    esp_err_t err = nvs_set_str(_nvs_handle, "timezone", tz.c_str());
    if (err == ESP_OK)
        err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

std::string ConfigManager::loadTimezone()
{
    std::string tz = "Etc/UTC"; // Default value
    if (!_openNVS())
        return tz;
    size_t required_size;
    if (nvs_get_str(_nvs_handle, "timezone", NULL, &required_size) == ESP_OK) {
        char* buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "timezone", buf, &required_size) == ESP_OK) {
            tz = std::string(buf);
        }
        delete[] buf;
    }
    _closeNVS();
    return tz;
}

bool ConfigManager::saveScaleCalibration(float factor, long offset)
{
    if (!_openNVS())
        return false;
    nvs_set_i32(_nvs_handle, "scale_cal_f", (int32_t)(factor * 1000));
    nvs_set_i32(_nvs_handle, "scale_cal_o", offset);
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

bool ConfigManager::loadScaleCalibration(float& factor, long& offset)
{
    factor = 2280.0f; // Default value
    offset = 0; // Default value
    if (!_openNVS())
        return false;
    int32_t temp_factor;
    if (nvs_get_i32(_nvs_handle, "scale_cal_f", &temp_factor) == ESP_OK) {
        factor = (float)temp_factor / 1000.0f;
    }

    int32_t temp_offset;
    if (nvs_get_i32(_nvs_handle, "scale_cal_o", &temp_offset) == ESP_OK) {
        offset = temp_offset;
    }

    _closeNVS();
    return true;
}

bool ConfigManager::saveHopperCalibration(uint16_t closed_pwm, uint16_t open_pwm)
{
    if (!_openNVS())
        return false;
    nvs_set_u16(_nvs_handle, "hop_closed", closed_pwm);
    nvs_set_u16(_nvs_handle, "hop_open", open_pwm);
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

bool ConfigManager::loadHopperCalibration(uint16_t& closed_pwm, uint16_t& open_pwm)
{
    // Set to defaults first
    closed_pwm = DEFAULT_HOPPER_CLOSED_PWM;
    open_pwm   = DEFAULT_HOPPER_OPEN_PWM;
    if (!_openNVS())
        return false;

    // Overwrite with saved values if they exist
    nvs_get_u16(_nvs_handle, "hop_closed", &closed_pwm);
    nvs_get_u16(_nvs_handle, "hop_open", &open_pwm);
    _closeNVS();
    return true;
}

// ============================================================================
// SPIFFS Recipe Storage Helpers
// ============================================================================

uint32_t ConfigManager::_computeRecipeCRC(const std::string& jsonStr)
{
    return crc32_le(0, (const uint8_t*)jsonStr.c_str(), jsonStr.length());
}

bool ConfigManager::_saveRecipeFile(const char* path, const std::string& jsonContent)
{
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    size_t written = file.print(jsonContent.c_str());
    file.close();

    if (written != jsonContent.length()) {
        ESP_LOGE(TAG, "Incomplete write to %s: wrote %d of %d bytes",
                 path, written, jsonContent.length());
        return false;
    }

    ESP_LOGI(TAG, "Successfully wrote %d bytes to %s", written, path);
    return true;
}

bool ConfigManager::_loadRecipeFile(const char* path, std::vector<Recipe>& recipes)
{
    if (!SPIFFS.exists(path)) {
        ESP_LOGW(TAG, "Recipe file %s does not exist", path);
        return false;
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open %s for reading", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        ESP_LOGE(TAG, "JSON parse error in %s: %s", path, error.c_str());
        return false;
    }

    // Validate envelope structure
    if (!doc["crc32"].is<uint32_t>() || !doc["recipes"].is<JsonArray>()) {
        ESP_LOGE(TAG, "Invalid envelope structure in %s", path);
        return false;
    }

    // Extract and verify CRC
    uint32_t storedCRC = doc["crc32"].as<uint32_t>();

    // Re-serialize recipes array to compute CRC
    JsonArray recipesArray = doc["recipes"].as<JsonArray>();
    std::string recipesJson;
    serializeJson(recipesArray, recipesJson);
    uint32_t computedCRC = _computeRecipeCRC(recipesJson);

    if (storedCRC != computedCRC) {
        ESP_LOGE(TAG, "CRC mismatch in %s: stored=0x%08X, computed=0x%08X",
                 path, storedCRC, computedCRC);
        return false;
    }

    // Parse recipes
    recipes.clear();
    for (JsonObject recipeObj : recipesArray) {
        Recipe recipe;
        recipe.uid         = recipeObj["uid"].as<uint32_t>();
        recipe.name        = recipeObj["name"].as<std::string>();
        recipe.dailyWeight = recipeObj["dailyWeight"] | 0.0;
        recipe.servings    = recipeObj["servings"] | 1;
        recipe.created     = recipeObj["created"] | 0LL;
        recipe.lastUsed    = recipeObj["lastUsed"] | 0LL;
        recipe.isEnabled   = recipeObj["isEnabled"] | true;

        for (JsonObject ingObj : recipeObj["ingredients"].as<JsonArray>()) {
            RecipeIngredient ing;
            ing.tankUid    = ingObj["tankUid"].as<uint64_t>();
            ing.percentage = ingObj["percentage"] | 0.0f;
            recipe.ingredients.push_back(ing);
        }
        recipes.push_back(recipe);
    }

    ESP_LOGI(TAG, "Successfully loaded %d recipes from %s", recipes.size(), path);
    return true;
}

std::vector<Recipe> ConfigManager::_loadRecipesFromNVS_Legacy()
{
    std::vector<Recipe> recipes;
    if (!_openNVS())
        return recipes;

    size_t required_size;
    if (nvs_get_str(_nvs_handle, "recipes", NULL, &required_size) == ESP_OK) {
        char* buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "recipes", buf, &required_size) == ESP_OK) {
            JsonDocument doc;
            if (deserializeJson(doc, buf) == DeserializationError::Ok) {
                JsonArray array = doc.as<JsonArray>();
                for (JsonObject recipeObj : array) {
                    Recipe recipe;
                    recipe.uid         = recipeObj["id"].as<uint32_t>();  // Legacy: read "id" as uid
                    recipe.name        = recipeObj["name"].as<std::string>();
                    recipe.dailyWeight = recipeObj["dailyWeight"] | 0.0;
                    recipe.servings    = recipeObj["servings"] | 1;
                    recipe.created     = recipeObj["created"] | 0LL;
                    recipe.lastUsed    = recipeObj["lastUsed"] | 0LL;
                    recipe.isEnabled   = recipeObj["isEnabled"] | true;

                    for (JsonObject ingObj : recipeObj["ingredients"].as<JsonArray>()) {
                        RecipeIngredient ing;
                        ing.tankUid    = ingObj["tankUid"].as<uint64_t>();
                        ing.percentage = ingObj["percentage"] | 0.0f;
                        recipe.ingredients.push_back(ing);
                    }
                    recipes.push_back(recipe);
                }
            }
        }
        delete[] buf;
    }
    _closeNVS();
    ESP_LOGI(TAG, "Legacy NVS: loaded %d recipes", recipes.size());
    return recipes;
}

void ConfigManager::_deleteNVSRecipes()
{
    if (!_openNVS())
        return;
    nvs_erase_key(_nvs_handle, "recipes");
    nvs_commit(_nvs_handle);
    _closeNVS();
    ESP_LOGI(TAG, "Deleted legacy NVS recipes key");
}

// ============================================================================
// Public Recipe Methods (SPIFFS-based with triple redundancy)
// ============================================================================

bool ConfigManager::saveRecipes(const std::vector<Recipe>& recipes)
{
    // Build recipes JSON array
    JsonDocument recipesDoc;
    JsonArray array = recipesDoc.to<JsonArray>();

    for (const auto& recipe : recipes) {
        JsonObject recipeObj     = array.add<JsonObject>();
        recipeObj["uid"]         = recipe.uid;
        recipeObj["name"]        = recipe.name;
        recipeObj["dailyWeight"] = recipe.dailyWeight;
        recipeObj["servings"]    = recipe.servings;
        recipeObj["created"]     = recipe.created;
        recipeObj["lastUsed"]    = recipe.lastUsed;
        recipeObj["isEnabled"]   = recipe.isEnabled;

        JsonArray ingredients = recipeObj["ingredients"].to<JsonArray>();
        for (const auto& ing : recipe.ingredients) {
            JsonObject ingObj    = ingredients.add<JsonObject>();
            ingObj["tankUid"]    = ing.tankUid;
            ingObj["percentage"] = ing.percentage;
        }
    }

    // Serialize recipes array for CRC computation
    std::string recipesJson;
    serializeJson(recipesDoc, recipesJson);
    uint32_t crc = _computeRecipeCRC(recipesJson);

    // Build envelope with CRC
    JsonDocument envelopeDoc;
    envelopeDoc["crc32"]   = crc;
    envelopeDoc["recipes"] = recipesDoc;

    std::string fullJson;
    serializeJson(envelopeDoc, fullJson);

    // Write to all 3 files for redundancy
    const char* files[] = { RECIPE_FILE_PRIMARY, RECIPE_FILE_BACKUP1, RECIPE_FILE_BACKUP2 };
    int successCount = 0;

    for (const char* path : files) {
        if (_saveRecipeFile(path, fullJson)) {
            successCount++;
        }
    }

    if (successCount == 0) {
        ESP_LOGE(TAG, "CRITICAL: Failed to save recipes to any file!");
        return false;
    } else if (successCount < 3) {
        ESP_LOGW(TAG, "Saved recipes to %d of 3 files (partial success)", successCount);
    } else {
        ESP_LOGI(TAG, "Saved %d recipes to all 3 redundant files", recipes.size());
    }

    return true;
}

std::vector<Recipe> ConfigManager::loadRecipes()
{
    std::vector<Recipe> recipes;

    // Try each file in priority order
    const char* files[] = { RECIPE_FILE_PRIMARY, RECIPE_FILE_BACKUP1, RECIPE_FILE_BACKUP2 };

    for (const char* path : files) {
        if (_loadRecipeFile(path, recipes)) {
            ESP_LOGI(TAG, "Loaded recipes from %s", path);

            // If we loaded from a backup, repair the primary files
            if (path != RECIPE_FILE_PRIMARY) {
                ESP_LOGW(TAG, "Primary file was invalid, repairing from %s", path);
                saveRecipes(recipes);  // Rewrite all 3 files
            }
            return recipes;
        }
    }

    // No valid SPIFFS file found - try legacy NVS migration
    ESP_LOGW(TAG, "No valid SPIFFS recipe files found, attempting NVS migration");
    recipes = _loadRecipesFromNVS_Legacy();

    if (!recipes.empty()) {
        ESP_LOGI(TAG, "Migrating %d recipes from NVS to SPIFFS", recipes.size());
        if (saveRecipes(recipes)) {
            _deleteNVSRecipes();  // Clean up legacy storage
        }
        return recipes;
    }

    ESP_LOGI(TAG, "No recipes found in NVS or SPIFFS, returning empty list");
    return recipes;
}

bool ConfigManager::factoryReset()
{
    // Delete SPIFFS recipe files
    const char* recipeFiles[] = { RECIPE_FILE_PRIMARY, RECIPE_FILE_BACKUP1, RECIPE_FILE_BACKUP2 };
    for (const char* path : recipeFiles) {
        if (SPIFFS.exists(path)) {
            SPIFFS.remove(path);
            ESP_LOGI(TAG, "Deleted recipe file: %s", path);
        }
    }

    // Erase NVS
    if (!_openNVS())
        return false;
    esp_err_t err = nvs_erase_all(_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(_nvs_handle);
    }
    _closeNVS();
    ESP_LOGW(TAG, "NVS namespace '%s' erased.", _namespace);
    return err == ESP_OK;
}
